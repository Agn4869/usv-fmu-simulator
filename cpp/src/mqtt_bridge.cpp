#include "mqtt_bridge.h"

#include <mqtt.h>

#include <winsock2.h>
#include <ws2tcpip.h>

#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

SOCKET connect_socket(const std::string& host, int port) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo* addresses = nullptr;
    const auto service = std::to_string(port);
    if (getaddrinfo(host.c_str(), service.c_str(), &hints, &addresses) != 0) {
        return INVALID_SOCKET;
    }

    SOCKET socket_handle = INVALID_SOCKET;
    for (auto* address = addresses; address; address = address->ai_next) {
        socket_handle = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (socket_handle == INVALID_SOCKET) continue;
        if (connect(socket_handle, address->ai_addr,
                    static_cast<int>(address->ai_addrlen)) == 0) {
            break;
        }
        closesocket(socket_handle);
        socket_handle = INVALID_SOCKET;
    }
    freeaddrinfo(addresses);

    if (socket_handle != INVALID_SOCKET) {
        u_long non_blocking = 1;
        ioctlsocket(socket_handle, FIONBIO, &non_blocking);
    }
    return socket_handle;
}

}

struct MqttBridge::Impl {
    std::string host;
    int port;
    std::string vessel_id;
    std::string client_id;
    ProtocolConfig protocol;
    SOCKET socket_handle = INVALID_SOCKET;
    mqtt_client client{};
    std::vector<std::uint8_t> send_buffer = std::vector<std::uint8_t>(8192);
    std::vector<std::uint8_t> receive_buffer = std::vector<std::uint8_t>(8192);

    mutable std::mutex command_mutex;
    std::mutex client_mutex;
    UsvCommand command;
    
    std::atomic<bool> running{false};
    std::atomic<bool> connected{false};
    std::thread network_thread;
    std::uint64_t state_sequence = 0;
    std::uint64_t last_command_sequence = 0;
    bool has_command_sequence = false;
    std::chrono::steady_clock::time_point next_reconnect{};
    int reconnect_delay_seconds = 1;

    Impl(std::string host_value, int port_value, std::string vessel,
         UsvCommand defaults, ProtocolConfig protocol_value)
        : host(std::move(host_value)), port(port_value), vessel_id(std::move(vessel)),
          client_id("usv-simulator-" + vessel_id),
          protocol(std::move(protocol_value)), command(defaults) {}

    void close_socket() {
        connected = false;
        if (socket_handle != INVALID_SOCKET) {
            closesocket(socket_handle);
            socket_handle = INVALID_SOCKET;
        }
    }

    static void reconnect_callback(mqtt_client* client, void** state) {
        auto* self = static_cast<Impl*>(*state);
        if (!self || !self->running) return;

        self->connected = false;
        const auto now = std::chrono::steady_clock::now();
        if (now < self->next_reconnect) return;
        self->close_socket();

        self->socket_handle = connect_socket(self->host, self->port);
        if (self->socket_handle == INVALID_SOCKET) {
            std::cerr << "MQTT connect failed; retry in "
                      << self->reconnect_delay_seconds << " s\n";
            self->next_reconnect = now + std::chrono::seconds(self->reconnect_delay_seconds);
            self->reconnect_delay_seconds = std::min(self->reconnect_delay_seconds * 2, 30);
            return;
        }

        mqtt_reinit(client, self->socket_handle,
                    self->send_buffer.data(), self->send_buffer.size(),
                    self->receive_buffer.data(), self->receive_buffer.size());
        mqtt_connect(client, self->client_id.c_str(), nullptr, nullptr, 0,
                     nullptr, nullptr, MQTT_CONNECT_CLEAN_SESSION, 30);
        mqtt_subscribe(client, self->protocol.command_topic.c_str(), 1);
        if (client->error != MQTT_OK) {
            std::cerr << "MQTT session setup failed: " << mqtt_error_str(client->error) << '\n';
            self->close_socket();
            self->next_reconnect = now + std::chrono::seconds(self->reconnect_delay_seconds);
            self->reconnect_delay_seconds = std::min(self->reconnect_delay_seconds * 2, 30);
            return;
        }

        self->connected = true;
        self->reconnect_delay_seconds = 1;
        self->next_reconnect = {};
        std::cout << "MQTT connected; subscribed to "
                  << self->protocol.command_topic << '\n';
    }

    static void receive_callback(void** state, mqtt_response_publish* published) {
        auto* self = static_cast<Impl*>(*state);
        if (!self || !published) return;

        const std::string topic(static_cast<const char*>(published->topic_name),
                                published->topic_name_size);
        if (topic != self->protocol.command_topic) return;
        const std::string payload(
            static_cast<const char*>(published->application_message),
            published->application_message_size);

        std::string decode_error;
        const auto decoded = ProtocolCodec::decode_control_1234(payload, &decode_error);
        if (!decoded) {
            std::cerr << "Ignored invalid 1234 message: " << decode_error << '\n';
            return;
        }

        if (decoded->package_sequence) {
            const auto integer_sequence = *decoded->package_sequence;
            if (self->has_command_sequence && integer_sequence <= self->last_command_sequence) {
                std::cerr << "Ignored stale 1234 packageSeq=" << integer_sequence << '\n';
                return;
            }
            self->last_command_sequence = integer_sequence;
            self->has_command_sequence = true;
        }

        std::lock_guard<std::mutex> lock(self->command_mutex);
        if (decoded->throttle_percent) {
            self->command.throttle_percent = *decoded->throttle_percent;
        }
        if (decoded->rudder_percent) {
            self->command.rudder_percent = *decoded->rudder_percent;
        }
        if (decoded->tip_buck_percent) {
            self->command.tip_buck_percent = *decoded->tip_buck_percent;
        }
    }
};

MqttBridge::MqttBridge(std::string host, int port, std::string vessel_id,
                       UsvCommand defaults, ProtocolConfig protocol)
    : impl_(std::make_unique<Impl>(std::move(host), port, std::move(vessel_id),
                                   defaults, std::move(protocol))) {
    WSADATA winsock{};
    if (WSAStartup(MAKEWORD(2, 2), &winsock) != 0) {
        throw std::runtime_error("WSAStartup failed");
    }
}

MqttBridge::~MqttBridge() {
    stop();
    WSACleanup();
}

void MqttBridge::start() {
    if (impl_->running) return;
    impl_->running = true;
    mqtt_init_reconnect(&impl_->client, &Impl::reconnect_callback,
                        impl_.get(), &Impl::receive_callback);
    impl_->client.publish_response_callback_state = impl_.get();

    impl_->network_thread = std::thread([this] {
        while (impl_->running) {
            {
                std::lock_guard<std::mutex> lock(impl_->client_mutex);
                mqtt_sync(&impl_->client);
                if (impl_->client.error != MQTT_OK &&
                    impl_->client.error != MQTT_ERROR_INITIAL_RECONNECT) {
                    impl_->connected = false;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    });
}

void MqttBridge::stop() {
    if (!impl_) return;
    impl_->running = false;
    if (impl_->network_thread.joinable()) impl_->network_thread.join();
    impl_->close_socket();
}

UsvCommand MqttBridge::latest_command() const {
    std::lock_guard<std::mutex> lock(impl_->command_mutex);
    return impl_->command;
}

void MqttBridge::publish_state(double, const UsvState& s) {
    if (!impl_->connected) return;
    const auto payload = ProtocolCodec::encode_state_4321(
        s, impl_->protocol, ++impl_->state_sequence);
    std::lock_guard<std::mutex> lock(impl_->client_mutex);
    if (!impl_->connected) return;
    const auto error = mqtt_publish(&impl_->client,
                                    impl_->protocol.state_topic.c_str(),
                                    payload.data(), payload.size(),
                                    MQTT_PUBLISH_QOS_0);
    if (error != MQTT_OK) {
        impl_->connected = false;
        std::cerr << "MQTT publish queue error: " << mqtt_error_str(error) << '\n';
    }
}
