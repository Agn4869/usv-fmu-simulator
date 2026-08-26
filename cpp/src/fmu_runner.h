#pragma once

#include "fmi2_minimal.h"

#include <filesystem>
#include <string>

struct UsvCommand {
    double rudder_percent = 0.0;
    double throttle_percent = 0.0;
    double tip_buck_percent = 0.0;
};

struct UsvInitialState {
    double u0 = 0.0;
    double v0 = 0.0;
    double r0 = 0.0;
    double yaw_deg0 = 0.0;
};

struct UsvState {
    double u_r = 0.0;
    double v_r = 0.0;
    double r = 0.0;
    double x = 0.0;
    double y = 0.0;
    double heading_acc = 0.0;
    double north_speed = 0.0;
    double east_speed = 0.0;
    double true_course_deg = 0.0;
    double yaw_deg = 0.0;
    double beta_deg = 0.0;
};

class FmuRunner {
public:
    explicit FmuRunner(std::filesystem::path extracted_fmu_dir,
                       std::string dll_name = "usv.dll",
                       std::string model_identifier = "usv7",
                       std::string guid = "{6c037dfd-a5ad-56ca-c0a6-8daf5ce5d007}");
    ~FmuRunner();

    FmuRunner(const FmuRunner&) = delete;
    FmuRunner& operator=(const FmuRunner&) = delete;

    void initialize(const UsvInitialState& initial);
    UsvState step(const UsvCommand& command, double dt = 0.2);
    double time() const { return simulation_time_; }

private:
    void load_library();
    void unload_library();
    void require(fmi2Status status, const char* operation) const;
    static std::string file_uri(const std::filesystem::path& path);

    std::filesystem::path fmu_dir_;
    std::string dll_name_;
    std::string model_identifier_;
    std::string guid_;
    void* library_ = nullptr;
    fmi2Component component_ = nullptr;
    double simulation_time_ = 0.0;
    bool initialized_ = false;
    fmi2CallbackFunctions callbacks_{};

    fmi2GetVersionTYPE get_version_ = nullptr;
    fmi2InstantiateTYPE instantiate_ = nullptr;
    fmi2FreeInstanceTYPE free_instance_ = nullptr;
    fmi2SetupExperimentTYPE setup_experiment_ = nullptr;
    fmi2EnterInitializationModeTYPE enter_initialization_mode_ = nullptr;
    fmi2ExitInitializationModeTYPE exit_initialization_mode_ = nullptr;
    fmi2TerminateTYPE terminate_ = nullptr;
    fmi2SetRealTYPE set_real_ = nullptr;
    fmi2SetTimeTYPE set_time_ = nullptr;
    fmi2GetRealTYPE get_real_ = nullptr;
    fmi2DoStepTYPE do_step_ = nullptr;
};
