#pragma once

#include "olp/core/MathTypes.h"
#include <array>
#include <optional>
#include <string>
#include <vector>

namespace olp::device {

// M05 & M06: OPW 7 Geometric Parameters (in mm) + axis sign/offset conventions
struct OpwParams {
    double a1{150.0};
    double a2{-150.0};
    double b{0.0};
    double c1{450.0};
    double c2{600.0};
    double c3{640.0};
    double c4{100.0};
    std::array<double, 6> offsetsRad{0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    std::array<int, 6> signCorrections{1, 1, -1, -1, -1, -1};
};

struct JointLimit {
    std::string name{"J1"};
    double lowerRad{-core::kPi};
    double upperRad{core::kPi};
    double maxVelRadPerSec{3.0};
    double maxAccRadPerSec2{10.0};
    double warningMarginRad{core::deg2rad(5.0)};
};

struct RobotModel {
    std::string modelId{"fanuc_m10ia"};
    std::string displayName{"FANUC M-10iA / ARC Mate 100iC"};
    std::string manufacturer{"FANUC"};
    OpwParams opw{};
    std::array<JointLimit, 6> jointLimits{};
    bool hasJ2J3Coupling{true};
    double j2J3CouplingCoeff{1.0}; // FANUC J3_display = J3_world - J2_display
    std::array<double, 6> homeJointsRad{0.0, 0.0, 0.0, 0.0, -core::kPi * 0.5, 0.0};
    std::array<std::string, 7> visualMeshFiles{};
    std::array<std::string, 7> collisionMeshFiles{};

    // Built-in standard FANUC M-10iA / ARC Mate 100iC model
    static RobotModel createFanucM10iA(const std::string& workspaceRoot = "");

    // Parse URDF file (e.g. SampleScenes/.../fanuc_m10ia_standalone.urdf)
    static std::optional<RobotModel> loadFromUrdf(const std::string& urdfFilePath);
};

} // namespace olp::device
