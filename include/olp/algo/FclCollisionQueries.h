#pragma once

#include "olp/algo/OpwKinematics.h"
#include "olp/core/ProjectModel.h"
#include <array>
#include <optional>
#include <string>
#include <vector>

namespace olp::algo {

// Computes forward kinematics for robot links 0..6 (link 0 = robotBaseInWorld)
// including FANUC J2/J3 coupling.
std::array<core::Transform4d, 7> forwardRobotLinkWorldFrames(
    const std::array<double, 6>& displayJointsRad,
    const core::Transform4d& robotBaseInWorld) noexcept;

// Narrowphase FCL exact distance query between robot links 1..6 + 4 torch spheres and scene obstacles.
// Broadphase AABB discards distant pairs.
// Contact/penetration yields dist <= 0.0 without distortion from AABB centers.
// Wire-tip sphere around TCP is exempted against activeWorkpieceId only during welding segment within 10mm.
bool queryRobotAndTorchCollision(
    const OpwKinematics& kinematics,
    const core::UnitsSettings& settings,
    const core::Transform4d& tcpWorldPose,
    const std::optional<std::array<double, 6>>& jointsRad,
    const core::Transform4d& robotBaseInWorld,
    const core::Transform4d& utoolOffset,
    const std::vector<core::SceneNode>& sceneNodes,
    const std::string& activeWorkpieceId,
    bool isWeldingRole,
    double& outMinClearanceMm,
    std::string& outCollidingPartName,
    const std::vector<std::string>& exemptionPartIds = {}) noexcept;

} // namespace olp::algo
