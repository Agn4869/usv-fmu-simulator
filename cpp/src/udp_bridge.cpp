#include "udp_bridge.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

struct UdpBridge::Impl {
    std::string bind_host;
    int listen_port;
    std::string remote_host;
    int remote_port;
    ProtocolConfig protocol;
    SOCKET socket_handle = INVALID_SOCKET;
    sockaddr_storage remote_address{};
    int remote_address_length = 0;
    UsvCommand command;
    mutable std::mutex command_mutex;
    std::atomic<bool> running{false};
    std::atomic<bool> connected{false};
    std::thread network_thread;
    std::uint64_t state_sequence = 0;
    std::uint64_t last_command_sequence = 0;
    bool has_command_sequence = false;

    Impl(std::string bind_host_value, int listen_port_value,
         std::string remote_host_value, int remote_port_value,
         UsvCommand defaults, ProtocolConfig protocol_value)
        : bind_host(std::move(bind_host_value)), listen_port(listen_port_value),
          remote_host(std::move(remote_host_value)), remote_port(remote_port_value),
          protocol(std::move(protocol_value)), command(defaults) {}

    void close_socket() {
        connected = false;
        if (socket_handle != INVALID_SOCKET) {
            closesocket(socket_handle);
            socket_handle = INVALID_SOCKET;
        }
    }

    bool resolve_remote() {
        if (remote_host.empty() || remote_port <= 0) return false;
        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        addrinfo* addresses = nullptr;
        const auto service = std::to_string(remote_port);
        if (getaddrinfo(remote_host.c_str(), service.c_str(), &hints, &addresses) != 0) {
            return false;
        }
        bool success = false;
        if (addresses && addresses->ai_addrlen <= sizeof(remote_address)) {
            std::memcpy(&remote_address, addresses->ai_addr, addresses->ai_addrlen);
            remote_address_length = static_cast<int>(addresses->ai_addrlen);
            success = true;
        }
        freeaddrinfo(addresses);
        return success;
    }

    bool open_socket() {
        socket_handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (socket_handle == INVALID_SOCKET) return false;

        sockaddr_in local{};
        local.sin_family = AF_INET;
        local.sin_port = htons(static_cast<u_short>(listen_port));
        if (bind_host.empty() || bind_host == "0.0.0.0") {
            local.sin_addr.s_addr = htonl(INADDR_ANY);
        } else if (inet_pton(AF_INET, bind_host.c_str(), &local.sin_addr) != 1) {
            close_socket();
            return false;
        }
        if (bind(socket_handle, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) != 0) {
            close_socket();
            return false;
        }

        u_long non_blocking = 1;
        ioctlsocket(socket_handle, FIONBIO, &non_blocking);
        resolve_remote();
        connected = true;
        return true;
    }

    void receive_once() {
        std::vector<char> buffer(64 * 1024);
        sockaddr_storage source{};
        int source_length = sizeof(source);
        const int count = recvfrom(
            socket_handle, buffer.data(), static_cast<int>(buffer.size()), 0,
            reinterpret_cast<sockaddr*>(&source), &source_length);
        if (count <= 0) return;

        std::string payload(buffer.data(), static_cast<std::size_t>(count));
        std::string decode_error;
        const auto decoded = ProtocolCodec::decode_control_1234(payload, &decode_error);
        if (!decoded) {
            std::cerr << "Ignored invalid UDP 1234 message: " << decode_error << '\n';
            return;
        }
        if (decoded->package_sequence) {
            const auto sequence = *decoded->package_sequence;
            if (has_command_sequence && sequence <= last_command_sequence) return;
            last_command_sequence = sequence;
            has_command_sequence = true;
        }
        std::lock_guard<std::mutex> lock(command_mutex);
        if (decoded->throttle_percent) {
            command.throttle_percent = *decoded->throttle_percent;
        }
        if (decoded->rudder_percent) {
            command.rudder_percent = *decoded->rudder_percent;
        }
        if (decoded->tip_buck_percent) {
            command.tip_buck_percent = *decoded->tip_buck_percent;
        }
    }
};

UdpBridge::UdpBridge(std::string bind_host, int listen_port,
                     std::string remote_host, int remote_port,
                     UsvCommand defaults, ProtocolConfig protocol)
    : impl_(std::make_unique<Impl>(std::move(bind_host), listen_port,
                                   std::move(remote_host), remote_port,
                                   defaults, std::move(protocol))) {
    WSADATA winsock{};
    if (WSAStartup(MAKEWORD(2, 2), &winsock) != 0) {
        throw std::runtime_error("WSAStartup failed");
    }
}

UdpBridge::~UdpBridge() {
    stop();
    WSACleanup();
}

void UdpBridge::start() {
    if (impl_->running) return;
    if (!impl_->open_socket()) {
        throw std::runtime_error("Cannot open UDP listen socket");
    }
    impl_->running = true;
    std::cout << "UDP listening on " << impl_->bind_host << ':' << impl_->listen_port << '\n';
    impl_->network_thread = std::thread([this] {
        while (impl_->running) {
            impl_->receive_once();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });
}

void UdpBridge::stop() {
    if (!impl_) return;
    impl_->running = false;
    if (impl_->network_thread.joinable()) impl_->network_thread.join();
    impl_->close_socket();
}

UsvCommand UdpBridge::latest_command() const {
    std::lock_guard<std::mutex> lock(impl_->command_mutex);
    return impl_->command;
}

void UdpBridge::publish_state(double, const UsvState& state) {
    if (!impl_->connected || impl_->remote_address_length == 0) return;
    const auto payload = ProtocolCodec::encode_state_4321(
        state, impl_->protocol, ++impl_->state_sequence);
    const int sent = sendto(
        impl_->socket_handle, payload.data(), static_cast<int>(payload.size()), 0,
        reinterpret_cast<const sockaddr*>(&impl_->remote_address),
        impl_->remote_address_length);
    if (sent == SOCKET_ERROR) {
        std::cerr << "UDP state send failed: " << WSAGetLastError() << '\n';
    }
}
