#pragma once

#include "olp/core/ProjectModel.h"
#include "olp/device/RobotModel.h"
#include <array>
#include <optional>
#include <vector>

namespace olp::algo {

struct FkResult {
    std::array<core::Transform4d, 7> linkTransforms{}; // base + link1..6 in robot base frame
    core::Transform4d flangePoseInBase{};
    core::Transform4d tcpPoseInBase{};
    core::FanucConfig config{};
};

struct IkSolution {
    std::array<double, 6> jointsRad{}; // FANUC display/controller joint angles (with J2/J3 coupling applied)
    core::FanucConfig config{};
    bool withinLimits{true};
    bool nearLimits{false};
    bool isSingular{false};
    double manipulability{1.0};
};

// M06: OPW Forward & Analytical 8-Solution Inverse Kinematics + FANUC Config + J2/J3 Coupling
class OpwKinematics {
public:
    explicit OpwKinematics(const device::RobotModel& model);

    // Convert between internal decoupled OPW model angles and FANUC controller display angles (J2/J3 coupling)
    std::array<double, 6> modelToDisplayJoints(const std::array<double, 6>& modelQ) const noexcept;
    std::array<double, 6> displayToModelJoints(const std::array<double, 6>& displayJ) const noexcept;

    // Forward Kinematics: Display joint angles (rad) + UTOOL offset -> Flange & TCP pose in Robot Base frame
    FkResult forwardKinematics(
        const std::array<double, 6>& displayJointsRad,
        const core::Transform4d& utoolOffset = core::Transform4d::identity()) const noexcept;

    // Analytical OPW Inverse Kinematics: Returns up to 8 base solutions (+ optional +/-360 deg turn expansion)
    std::vector<IkSolution> inverseKinematics(
        const core::Transform4d& targetTcpInBase,
        const core::Transform4d& utoolOffset = core::Transform4d::identity(),
        bool expandTurns = false) const;

    // Section 6.2: Select optimal solution (locked config > nearest previous joint distance > away from limits/singularity)
    std::optional<IkSolution> selectBestSolution(
        const std::vector<IkSolution>& candidates,
        const std::array<double, 6>& prevDisplayJointsRad,
        const std::optional<core::FanucConfig>& lockedConfig = std::nullopt,
        bool forbidConfigChangeInWeld = false) const;

    // Compute FANUC Configuration ('N U T, 0, 0, 0') from display joint angles
    core::FanucConfig computeConfig(const std::array<double, 6>& displayJointsRad) const noexcept;

    // Check joint soft limits & near-limit warning (< 5 deg)
    void checkJointLimits(const std::array<double, 6>& displayJointsRad, bool& outWithin, bool& outNear) const noexcept;

    // Singularity detection (wrist J5 ~ 0, elbow, shoulder) & manipulability index
    bool checkSingularity(const std::array<double, 6>& displayJointsRad, double& outManipulability) const noexcept;

    // Weighted joint distance between two 6-axis postures
    static double weightedJointDistance(
        const std::array<double, 6>& j1,
        const std::array<double, 6>& j2) noexcept;

    const device::RobotModel& robotModel() const noexcept { return model_; }

private:
    device::RobotModel model_;
};

} // namespace olp::algo
