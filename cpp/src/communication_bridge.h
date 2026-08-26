#pragma once

#include "fmu_runner.h"

class ICommunicationBridge {
public:
    virtual ~ICommunicationBridge() = default;

    virtual void start() = 0;
    virtual void stop() = 0;
    virtual UsvCommand latest_command() const = 0;
    virtual void publish_state(double simulation_time, const UsvState& state) = 0;
};
