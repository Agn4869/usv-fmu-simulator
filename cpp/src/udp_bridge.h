#pragma once

#include "communication_bridge.h"
#include "protocol_codec.h"

#include <memory>
#include <string>

class UdpBridge final : public ICommunicationBridge {
public:
    UdpBridge(std::string bind_host, int listen_port,
              std::string remote_host, int remote_port,
              UsvCommand defaults, ProtocolConfig protocol);
    ~UdpBridge() override;

    UdpBridge(const UdpBridge&) = delete;
    UdpBridge& operator=(const UdpBridge&) = delete;

    void start() override;
    void stop() override;
    UsvCommand latest_command() const override;
    void publish_state(double simulation_time, const UsvState& state) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
