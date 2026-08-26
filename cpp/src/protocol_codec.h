#pragma once

#include "fmu_runner.h"

#include <cstdint>
#include <optional>
#include <string>

struct ProtocolConfig {
    std::string command_topic = "1234";
    std::string state_topic = "4321";
    int package_type = 0;
    double origin_longitude = 0.0;
    double origin_latitude = 0.0;
    double origin_height = 0.0;
    int device_state = 1;
};

struct DecodedControlMessage {
    std::optional<double> rudder_percent;
    std::optional<double> throttle_percent;
    std::optional<double> tip_buck_percent;
    std::optional<std::uint64_t> package_sequence;
};

class ProtocolCodec {
public:
    static std::optional<DecodedControlMessage> decode_control_1234(
        const std::string& payload, std::string* error = nullptr);

    static std::string encode_state_4321(
        const UsvState& state,
        const ProtocolConfig& config,
        std::uint64_t package_sequence);
};
