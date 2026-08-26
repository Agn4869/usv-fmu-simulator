#pragma once

#include "communication_bridge.h"
#include "protocol_codec.h"

#include <memory>
#include <string>

class MqttBridge final : public ICommunicationBridge {
public:
    MqttBridge(std::string host, int port, std::string vessel_id,
               UsvCommand defaults, ProtocolConfig protocol);
    ~MqttBridge() override;

    MqttBridge(const MqttBridge&) = delete;
    MqttBridge& operator=(const MqttBridge&) = delete;

    void start() override;
    void stop() override;
    UsvCommand latest_command() const override;
    void publish_state(double simulation_time, const UsvState& state) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
