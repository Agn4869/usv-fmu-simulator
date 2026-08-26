#pragma once

#include <cstddef>
#include <cstdint>

extern "C" {

using fmi2Component = void*;
using fmi2ComponentEnvironment = void*;
using fmi2String = const char*;
using fmi2ValueReference = std::uint32_t;
using fmi2Real = double;
using fmi2Integer = std::int32_t;
using fmi2Boolean = std::int32_t;
using fmi2Byte = std::uint8_t;

enum fmi2Status : std::int32_t {
    fmi2OK = 0,
    fmi2Warning = 1,
    fmi2Discard = 2,
    fmi2Error = 3,
    fmi2Fatal = 4,
    fmi2Pending = 5
};

enum fmi2Type : std::int32_t {
    fmi2ModelExchange = 0,
    fmi2CoSimulation = 1
};

using fmi2CallbackLogger = void (*)(
    fmi2ComponentEnvironment,
    fmi2String,
    fmi2Status,
    fmi2String,
    fmi2String,
    ...);
using fmi2CallbackAllocateMemory = void* (*)(std::size_t, std::size_t);
using fmi2CallbackFreeMemory = void (*)(void*);
using fmi2StepFinished = void (*)(fmi2ComponentEnvironment, fmi2Status);

struct fmi2CallbackFunctions {
    fmi2CallbackLogger logger;
    fmi2CallbackAllocateMemory allocateMemory;
    fmi2CallbackFreeMemory freeMemory;
    fmi2StepFinished stepFinished;
    fmi2ComponentEnvironment componentEnvironment;
};

using fmi2GetVersionTYPE = fmi2String (*)();
using fmi2InstantiateTYPE = fmi2Component (*) (
    fmi2String, fmi2Type, fmi2String, fmi2String,
    const fmi2CallbackFunctions*, fmi2Boolean, fmi2Boolean);
using fmi2FreeInstanceTYPE = void (*)(fmi2Component);
using fmi2SetupExperimentTYPE = fmi2Status (*) (
    fmi2Component, fmi2Boolean, fmi2Real, fmi2Real,
    fmi2Boolean, fmi2Real);
using fmi2EnterInitializationModeTYPE = fmi2Status (*)(fmi2Component);
using fmi2ExitInitializationModeTYPE = fmi2Status (*)(fmi2Component);
using fmi2TerminateTYPE = fmi2Status (*)(fmi2Component);
using fmi2SetRealTYPE = fmi2Status (*) (
    fmi2Component, const fmi2ValueReference[], std::size_t, const fmi2Real[]);
using fmi2SetTimeTYPE = void (*)(fmi2Component, fmi2Real);
using fmi2GetRealTYPE = fmi2Status (*) (
    fmi2Component, const fmi2ValueReference[], std::size_t, fmi2Real[]);
using fmi2DoStepTYPE = fmi2Status (*) (
    fmi2Component, fmi2Real, fmi2Real, fmi2Boolean);

}
