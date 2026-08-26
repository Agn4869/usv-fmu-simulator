#include "fmu_runner.h"
#include "mqtt_bridge.h"
#include "udp_bridge.h"

#include <atomic>
#include <chrono>
#include <cctype>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

std::atomic<bool> running{true};

void stop_handler(int) { running = false; }

std::string trim(std::string text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

class IniConfig {
public:
    explicit IniConfig(const std::filesystem::path & path) : path_(path) {
        std::ifstream input(path);
        if (!input) throw std::runtime_error("Cannot open config: " + path.string());
        std::string section;
        std::string line;
        while (std::getline(input, line)) {
            line = trim(line);
            if (line.empty() || line[0] == '#' || line[0] == ';') continue;
            if (line.front() == '[' && line.back() == ']') {
                section = trim(line.substr(1, line.size() - 2));
                continue;
            }
            const std::string::size_type equals = line.find('=');
            if (equals == std::string::npos) continue;
            values_[section + '.' + trim(line.substr(0, equals))] = trim(line.substr(equals + 1));
        }
    }

    std::string text(const std::string& key, const std::string& fallback) const {
        const auto it = values_.find(key);
        return it == values_.end() ? fallback : it->second;
    }

    double number(const std::string& key, double fallback) const {
        const auto value = text(key, "");
        return value.empty() ? fallback : std::stod(value);
    }

    int integer(const std::string& key, int fallback) const {
        const auto value = text(key, "");
        return value.empty() ? fallback : std::stoi(value);
    }

    bool boolean(const std::string& key, bool fallback) const {
        auto value = text(key, "");
        for (auto& ch : value) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        if (value == "true" || value == "1" || value == "yes" || value == "on") return true;
        if (value == "false" || value == "0" || value == "no" || value == "off") return false;
        return fallback;
    }

    std::filesystem::path resolve(const std::string& key, const std::string& fallback) const {
        std::filesystem::path value = text(key, fallback);
        if (value.is_relative()) value = path_.parent_path() / value;
        return std::filesystem::weakly_canonical(value);
    }

private:
    std::filesystem::path path_;
    std::map<std::string, std::string> values_;
};

}

int main(int argc, char** argv) {
    try {
        std::signal(SIGINT, stop_handler);
        std::signal(SIGTERM, stop_handler);
        const std::filesystem::path config_path = argc >= 2 ? argv[1] : "config/usv.ini";
        const IniConfig config(config_path);

        const auto fmu_dir = config.resolve("simulation.fmu_dir", "../runtime/usv7");
        const auto dll_name = config.text("simulation.dll_name", "usv.dll");
        const auto model_identifier = config.text("simulation.model_identifier", "usv7");
        const auto guid = config.text(
            "simulation.guid", "{6c037dfd-a5ad-56ca-c0a6-8daf5ce5d007}");
        const double dt = config.number("simulation.step_size", 0.2);
        const double duration = config.number("simulation.duration", 10.0);
        const bool realtime = config.boolean("simulation.realtime", true);

        UsvInitialState initial{
            config.number("initial.u0", 0.0),
            config.number("initial.v0", 0.0),
            config.number("initial.r0", 0.0),
            config.number("initial.yaw_deg0", 0.0)};

        const auto origin_longitude = config.number("initial.longitude", 0.0);
        const auto origin_latitude = config.number("initial.latitude", 0.0);
        const auto origin_height = config.number("initial.height", 0.0);

        UsvCommand defaults{
            config.number("control.rudder_percent", 0.0),
            config.number("control.throttle_percent", 0.0),
            config.number("control.tip_buck_percent", 0.0)};
        ProtocolConfig protocol{
            config.text("protocol.command_topic", "1234"),
            config.text("protocol.state_topic", "4321"),
            config.integer("protocol.package_type", 0),
            origin_longitude,
            origin_latitude,
            origin_height,
            config.integer("protocol.device_state", 1)};

        FmuRunner runner(fmu_dir, dll_name, model_identifier, guid);
        runner.initialize(initial);

        std::unique_ptr<ICommunicationBridge> communication;
        const auto communication_type = config.text("communication.type", "mqtt");
        if (config.boolean("communication.enabled", true)) {
            if (communication_type == "mqtt") {
                communication = std::make_unique<MqttBridge>(
                    config.text("mqtt.host", "127.0.0.1"),
                    config.integer("mqtt.port", 1884),
                    config.text("mqtt.vessel_id", "usv_001"), defaults, protocol);
            } else if (communication_type == "udp") {
                communication = std::make_unique<UdpBridge>(
                    config.text("udp.bind_host", "0.0.0.0"),
                    config.integer("udp.listen_port", 9001),
                    config.text("udp.remote_host", "127.0.0.1"),
                    config.integer("udp.remote_port", 9002),
                    defaults, protocol);
            } else {
                throw std::runtime_error(
                    "Unknown communication.type: " + communication_type +
                    " (expected mqtt or udp)");
            }
            communication->start();
        }

        const auto csv_path = config.resolve("output.csv", "../usv_state.csv");
        std::ofstream csv(csv_path);
        if (!csv) throw std::runtime_error("Cannot create CSV: " + csv_path.string());
        csv << "time,u_r,v_r,r,x,y,headingAcc,northSpeed,eastSpeed,trueCourseDeg,yawDeg,betaDeg\n";
        csv << std::fixed << std::setprecision(9);

        auto next_tick = std::chrono::steady_clock::now();
        while (running && (duration <= 0.0 || runner.time() < duration - 1e-12)) {
            const auto command = communication ? communication->latest_command() : defaults;
            const auto state = runner.step(command, dt);
            csv << runner.time() << ','
                << state.u_r << ',' << state.v_r << ',' << state.r << ','
                << state.x << ',' << state.y << ',' << state.heading_acc << ','
                << state.north_speed << ',' << state.east_speed << ','
                << state.true_course_deg << ',' << state.yaw_deg << ','
                << state.beta_deg << '\n';
            csv.flush();
            if (communication) communication->publish_state(runner.time(), state);

            if (realtime) {
                next_tick += std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                    std::chrono::duration<double>(dt));
                std::this_thread::sleep_until(next_tick);
            }
        }

        if (communication) communication->stop();
        std::cout << "Simulation finished at t=" << runner.time()
                  << " s; wrote " << csv_path << '\n';
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "ERROR: " << ex.what() << '\n';
        return 1;
    }
}
