#include "fmu_runner.h"

#include <windows.h>

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {

void* allocate_memory(std::size_t nobj, std::size_t size) {
    return std::calloc(nobj, size);
}

void free_memory(void* pointer) {
    std::free(pointer);
}

void logger(
    fmi2ComponentEnvironment,
    fmi2String instance,
    fmi2Status status,
    fmi2String category,
    fmi2String message,
    ...) {
    if (status < fmi2Warning) return;
    std::cerr << "[FMU][" << (instance ? instance : "usv") << "]["
              << (category ? category : "log") << "][" << status << "] "
              << (message ? message : "") << '\n';
}

template <typename T>
T load_symbol(HMODULE library, const char* name) {
    auto raw = GetProcAddress(library, name);
    if (!raw) {
        throw std::runtime_error(std::string("Missing FMU symbol: ") + name);
    }
    return reinterpret_cast<T>(raw);
}

}

FmuRunner::FmuRunner(std::filesystem::path extracted_fmu_dir,
                     std::string dll_name,
                     std::string model_identifier,
                     std::string guid)
    : fmu_dir_(std::move(extracted_fmu_dir)),
      dll_name_(std::move(dll_name)),
      model_identifier_(std::move(model_identifier)),
      guid_(std::move(guid)) {
    load_library();
}

FmuRunner::~FmuRunner() {
    if (component_) {
        if (initialized_ && terminate_) {
            terminate_(component_);
        }
        if (free_instance_) {
            free_instance_(component_);
        }
    }
    unload_library();
}

void FmuRunner::load_library() {
    const auto dll = fmu_dir_ / "binaries" / "win64" / dll_name_;
    if (!std::filesystem::exists(dll)) {
        throw std::runtime_error("FMU DLL not found: " + dll.string());
    }

    const auto wide = dll.wstring();
    library_ = LoadLibraryW(wide.c_str());
    if (!library_) {
        throw std::runtime_error("LoadLibrary failed for: " + dll.string());
    }
    auto module = static_cast<HMODULE>(library_);

    get_version_ = load_symbol<fmi2GetVersionTYPE>(module, "fmi2GetVersion");
    instantiate_ = load_symbol<fmi2InstantiateTYPE>(module, "fmi2Instantiate");
    free_instance_ = load_symbol<fmi2FreeInstanceTYPE>(module, "fmi2FreeInstance");
    setup_experiment_ = load_symbol<fmi2SetupExperimentTYPE>(module, "fmi2SetupExperiment");
    enter_initialization_mode_ = load_symbol<fmi2EnterInitializationModeTYPE>(module, "fmi2EnterInitializationMode");
    exit_initialization_mode_ = load_symbol<fmi2ExitInitializationModeTYPE>(module, "fmi2ExitInitializationMode");
    terminate_ = load_symbol<fmi2TerminateTYPE>(module, "fmi2Terminate");
    set_real_ = load_symbol<fmi2SetRealTYPE>(module, "fmi2SetReal");
    set_time_ = load_symbol<fmi2SetTimeTYPE>(module, "fmi2SetTime");
    get_real_ = load_symbol<fmi2GetRealTYPE>(module, "fmi2GetReal");
    do_step_ = load_symbol<fmi2DoStepTYPE>(module, "fmi2DoStep");

    if (std::string(get_version_()) != "2.0") {
        throw std::runtime_error("Expected FMI 2.0 FMU");
    }

    callbacks_.logger = logger;
    callbacks_.allocateMemory = allocate_memory;
    callbacks_.freeMemory = free_memory;
    callbacks_.stepFinished = nullptr;
    callbacks_.componentEnvironment = nullptr;

    const auto resource = file_uri(fmu_dir_ / "resources");
    component_ = instantiate_("usv_instance", fmi2CoSimulation,
                              guid_.c_str(),
                              resource.c_str(), &callbacks_, 0, 0);
    if (!component_) {
        throw std::runtime_error("fmi2Instantiate returned null");
    }
}

void FmuRunner::initialize(const UsvInitialState& initial) {
    require(setup_experiment_(component_, 0, 0.0, 0.0, 1, 10.0), "fmi2SetupExperiment");
    require(enter_initialization_mode_(component_), "fmi2EnterInitializationMode");

    const fmi2ValueReference refs[] = {0, 1, 2, 3, 4, 5, 6};
    const fmi2Real values[] = {0.0, 0.0, 0.0,
                               initial.u0, initial.v0, initial.r0, initial.yaw_deg0};
    require(set_real_(component_, refs, 7, values), "fmi2SetReal(initial inputs)");
    require(exit_initialization_mode_(component_), "fmi2ExitInitializationMode");

    simulation_time_ = 0.0;
    set_time_(component_, simulation_time_);
    initialized_ = true;
}

UsvState FmuRunner::step(const UsvCommand& command, double dt) {
    if (!initialized_) {
        throw std::runtime_error("FMU is not initialized");
    }

    const fmi2ValueReference input_refs[] = {0, 1, 2};
    const fmi2Real input_values[] = {
        command.rudder_percent, command.throttle_percent, command.tip_buck_percent};
    require(set_real_(component_, input_refs, 3, input_values), "fmi2SetReal(control)");
    set_time_(component_, simulation_time_);
    require(do_step_(component_, simulation_time_, dt, 1), "fmi2DoStep");

    const fmi2ValueReference output_refs[] = {7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17};
    fmi2Real output_values[11]{};
    require(get_real_(component_, output_refs, 11, output_values), "fmi2GetReal");

    simulation_time_ += dt;
    return UsvState{
        output_values[0], output_values[1], output_values[2], output_values[3],
        output_values[4], output_values[5], output_values[6], output_values[7],
        output_values[8], output_values[9], output_values[10]};
}

void FmuRunner::unload_library() {
    if (library_) {
        FreeLibrary(static_cast<HMODULE>(library_));
        library_ = nullptr;
    }
}

void FmuRunner::require(fmi2Status status, const char* operation) const {
    if (status > fmi2Warning) {
        throw std::runtime_error(std::string(operation) + " failed with FMI status " + std::to_string(status));
    }
}

std::string FmuRunner::file_uri(const std::filesystem::path& path) {
    auto text = std::filesystem::absolute(path).generic_string();
    return "file:///" + text;
}
