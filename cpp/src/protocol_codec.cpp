#include "protocol_codec.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <initializer_list>
#include <sstream>
#include <vector>

namespace {

std::optional<double> find_json_number(const std::string& json, const std::string& key) {
    const auto key_pos = json.find('"' + key + '"');
    if (key_pos == std::string::npos) return std::nullopt;
    const auto colon = json.find(':', key_pos + key.size() + 2);
    if (colon == std::string::npos) return std::nullopt;

    const char* start = json.c_str() + colon + 1;
    char* end = nullptr;
    const double value = std::strtod(start, &end);
    if (end == start || !std::isfinite(value)) return std::nullopt;
    return value;
}

std::optional<double> average_percent(
    const std::string& json, std::initializer_list<const char*> keys, std::string* error) {
    std::vector<double> values;
    for (const auto* key : keys) {
        if (const auto value = find_json_number(json, key)) {
            if (*value < -100.0 || *value > 100.0) {
                if (error) *error = std::string(key) + " must be in [-100, 100]";
                return std::nullopt;
            }
            values.push_back(*value);
        }
    }
    if (values.empty()) return std::nullopt;
    double total = 0.0;
    for (const auto value : values) total += value;
    return total / static_cast<double>(values.size());
}

void append_number(std::ostringstream& out, const char* name, double value) {
    out << '"' << name << "\":";
    if (std::isfinite(value)) out << value;
    else out << "null";
}

std::string protocol_time() {
    const auto now = std::chrono::system_clock::now();
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    const std::time_t raw = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
    localtime_s(&local, &raw);

    std::ostringstream out;
    out << std::put_time(&local, "%Y-%m-%d %H:%M:%S")
        << '.' << std::setw(3) << std::setfill('0') << milliseconds.count();
    return out.str();
}

}

std::optional<DecodedControlMessage> ProtocolCodec::decode_control_1234(
    const std::string& payload, std::string* error) {
    DecodedControlMessage decoded;
    if (const auto sequence = find_json_number(payload, "packageSeq")) {
        if (*sequence < 0.0 || *sequence > static_cast<double>(UINT64_MAX)) {
            if (error) *error = "packageSeq is out of range";
            return std::nullopt;
        }
        decoded.package_sequence = static_cast<std::uint64_t>(*sequence);
    }

    const auto throttle = average_percent(payload, {"accL", "accM", "accR"}, error);
    const auto rudder = average_percent(payload, {"rudderL", "rudderM", "rudderR"}, error);
    const auto tip_buck = average_percent(payload, {"tipBuckL", "tipBuckR"}, error);
    if (error && !error->empty()) return std::nullopt;

    decoded.throttle_percent = throttle;
    decoded.rudder_percent = rudder;
    decoded.tip_buck_percent = tip_buck;
    if (!throttle && !rudder && !tip_buck) {
        if (error) *error = "1234 contains no supported control fields";
        return std::nullopt;
    }
    return decoded;
}

std::string ProtocolCodec::encode_state_4321(
    const UsvState& s, const ProtocolConfig& config, std::uint64_t package_sequence) {
    constexpr double pi = 3.14159265358979323846;
    constexpr double semi_major_axis = 6'378'137.0;
    constexpr double flattening = 1.0 / 298.257223563;
    constexpr double eccentricity_squared = flattening * (2.0 - flattening);
    const double latitude_radians = config.origin_latitude * pi / 180.0;
    const double sin_latitude = std::sin(latitude_radians);
    const double cos_latitude = std::cos(latitude_radians);
    const double denominator = std::sqrt(
        1.0 - eccentricity_squared * sin_latitude * sin_latitude);
    const double prime_vertical_radius = semi_major_axis / denominator;
    const double meridian_radius = semi_major_axis * (1.0 - eccentricity_squared)
        / (denominator * denominator * denominator);
    const double latitude = config.origin_latitude
        + (s.x / (meridian_radius + config.origin_height)) * 180.0 / pi;
    double longitude = config.origin_longitude;
    if (std::abs(cos_latitude) > 1e-12) {
        longitude += (s.y / ((prime_vertical_radius + config.origin_height)
            * cos_latitude)) * 180.0 / pi;
    }

    std::ostringstream out;
    out << std::setprecision(12)
        << "{\"head\":{"
        << "\"packageSeq\":" << package_sequence << ','
        << "\"packageType\":" << config.package_type << ','
        << "\"time\":\"" << protocol_time() << "\"},"
        << "\"content\":{";
    append_number(out, "longitude", longitude); out << ',';
    append_number(out, "latitude", latitude); out << ',';
    append_number(out, "height", config.origin_height); out << ',';
    append_number(out, "speed", std::hypot(s.north_speed, s.east_speed)); out << ',';
    append_number(out, "course", s.true_course_deg); out << ',';
    append_number(out, "heading", s.yaw_deg); out << ',';
    append_number(out, "headingAcc", s.heading_acc); out << ',';
    append_number(out, "eastSpeed", s.east_speed); out << ',';
    append_number(out, "northSpeed", s.north_speed); out << ',';
    append_number(out, "verticalSpeed", 0.0); out << ',';
    append_number(out, "pitch", 0.0); out << ',';
    append_number(out, "rolling", 0.0); out << ',';
    append_number(out, "angularX", 0.0); out << ',';
    append_number(out, "angularY", 0.0); out << ',';
    append_number(out, "angularZ", s.r); out << ',';
    append_number(out, "accX", 0.0); out << ',';
    append_number(out, "accY", 0.0); out << ',';
    append_number(out, "accZ", 0.0); out << ',';
    out << "\"deviceState\":" << config.device_state << "}}";
    return out.str();
}
