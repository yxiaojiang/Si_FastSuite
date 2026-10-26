#pragma once

#include "olp/algo/OpwKinematics.h"
#include "olp/core/ProjectModel.h"
#include <array>
#include <string>
#include <vector>

namespace olp::algo {

// M10: Twist Angle Scan Sample (Section 6.5: [-180, 180] deg around wire Z axis)
struct TwistScanSample {
    double twistAngleDeg{0.0};
    bool reachable{false};
    bool collisionFree{false};
    bool nearSingularityOrLimit{false};
    double minClearanceMm{0.0};
};

// M10: Issue Item for the clickable Issues Panel
struct ValidationIssue {
    std::string programId{};
    std::string operationId{};
    std::string targetId{};
    std::string targetName{};
    core::TargetValidationState state{core::TargetValidationState::Valid};
    std::string description{};
};

// M11: Baked Animation Frame for 3D Viewport, HTML5 Player & 3D PDF
struct BakedFrame {
    double timeSec{0.0};
    std::array<double, 6> jointsRad{};
    std::array<core::Transform4d, 7> linkWorldTransforms{};
    core::Vec3 tcpWorldPos{};
    bool arcActive{false};
};

struct SimulationResult {
    double totalCycleTimeSec{0.0};
    double arcWeldTimeSec{0.0};
    double airCutTimeSec{0.0};
    bool hasConfigJumpInLinear{false};
    std::vector<BakedFrame> frames{};
    std::vector<ValidationIssue> issues{};
};

struct OperationTorchClearance {
    double minClearanceMm{0.0};
    std::string targetName{};
    std::string partName{};
    bool meetsSafeDistance{false};
};

// M10 & M11: Reachability, Collision (with Wire-Tip Exemption) & Trajectory Simulation
class CollisionAndSimEngine {
public:
    CollisionAndSimEngine(
        const OpwKinematics& kinematics,
        const core::UnitsSettings& settings);

    // M10: Check single point collision between torch/robot and scene obstacles
    // Critical Pitfall Fix: Wire tip sphere around TCP is exempted from colliding with targetWorkpieceId
    // while Torch Body upper cylinder is always checked against fixtures/clamps and workpieces.
    bool checkTorchPoseCollision(
        const core::Transform4d& tcpWorldPose,
        const std::vector<core::SceneNode>& sceneNodes,
        const std::string& activeWorkpieceId,
        bool isWeldingRole,
        double& outMinClearanceMm,
        std::string& outCollidingPartName,
        const std::vector<std::string>& exemptionPartIds = {}) const noexcept;

    // Section 6.5: Scan twist angle in [-180, 180] deg around wire Z axis
    std::vector<TwistScanSample> scanTwistAngles(
        const core::Transform4d& baseTargetWorldPose,
        const core::Transform4d& robotBaseInWorld,
        const core::Transform4d& utoolOffset,
        const std::array<double, 6>& refJointsRad,
        const std::optional<core::FanucConfig>& requiredConfig = std::nullopt,
        const std::vector<core::SceneNode>& sceneNodes = {},
        const std::string& activeWorkpieceId = "",
        double stepDeg = 5.0,
        const std::vector<std::string>& exemptionPartIds = {},
        bool isWeldingRole = true) const;

    // Minimum torch-sphere clearance of every target. Welding roles use the wire-tip exemption.
    OperationTorchClearance measureOperationTorchClearance(
        const core::Operation& operation,
        const core::Transform4d& uframeInWorld,
        const std::vector<core::SceneNode>& sceneNodes) const;

    // M10 & M11: Validate entire program, run J/L interpolation & bake animation frames
    SimulationResult validateAndSimulateProgram(
        core::Program& inOutProgram,
        const core::Transform4d& robotBaseInWorld,
        const core::Transform4d& uframeInWorld,
        const core::Transform4d& utoolOffset,
        const std::vector<core::SceneNode>& sceneNodes,
        double sampleStepSec = 0.05) const;

    // Joint motion from home to APPROACH can arc the torch through a part even when
    // both poses are clear. Insert linear clearance points, or switch that move to
    // linear, until the same sweep check used by simulation is not a hard hit.
    // Returns false when an APPROACH exists and no sampled transit stays clear.
    bool insertSafeTransitBeforeApproach(
        core::Program& inOutProgram,
        const core::Transform4d& robotBaseInWorld,
        const core::Transform4d& uframeInWorld,
        const core::Transform4d& utoolOffset,
        const std::vector<core::SceneNode>& sceneNodes,
        double sampleStepSec = 0.05) const;

private:
    const OpwKinematics& kinematics_;
    core::UnitsSettings settings_;

    enum class TransitProbeKind { Clear, Near, Blocked };

    // Same sample count, IK lock, and torch spheres as validateAndSimulateProgram.
    TransitProbeKind probeTorchTransit(
        const std::array<double, 6>& startJoints,
        const core::Transform4d& startTcpWorld,
        const core::Transform4d& endWorld,
        core::MotionType motion,
        double speedValue,
        const core::Transform4d& robotBaseInWorld,
        const core::Transform4d& utoolOffset,
        const std::vector<core::SceneNode>& sceneNodes,
        const std::string& activeWorkpieceId,
        const std::vector<std::string>& exemptionPartIds,
        double sampleStepSec,
        std::array<double, 6>& outEndJoints) const;

    static double pointToAabbSignedDistance(
        const core::Vec3& pt,
        const core::Vec3& boxMin,
        const core::Vec3& boxMax) noexcept;
};

} // namespace olp::algo
