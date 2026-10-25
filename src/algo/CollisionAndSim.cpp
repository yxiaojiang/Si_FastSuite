#include "olp/algo/CollisionAndSim.h"

#include <algorithm>
#include <cmath>

namespace olp::algo {

CollisionAndSimEngine::CollisionAndSimEngine(
    const OpwKinematics& kinematics,
    const core::UnitsSettings& settings)
    : kinematics_(kinematics), settings_(settings) {}

double CollisionAndSimEngine::pointToAabbSignedDistance(
    const core::Vec3& pt,
    const core::Vec3& boxMin,
    const core::Vec3& boxMax) noexcept {
    // Outside distance (> 0) or inside penetration depth (<= 0)
    const double dxOut = std::max({boxMin.x - pt.x, 0.0, pt.x - boxMax.x});
    const double dyOut = std::max({boxMin.y - pt.y, 0.0, pt.y - boxMax.y});
    const double dzOut = std::max({boxMin.z - pt.z, 0.0, pt.z - boxMax.z});
    const double outsideDist = std::sqrt(dxOut * dxOut + dyOut * dyOut + dzOut * dzOut);
    if (outsideDist > 0.0) {
        return outsideDist;
    }
    const double dxIn = std::min(pt.x - boxMin.x, boxMax.x - pt.x);
    const double dyIn = std::min(pt.y - boxMin.y, boxMax.y - pt.y);
    const double dzIn = std::min(pt.z - boxMin.z, boxMax.z - pt.z);
    return -std::min({dxIn, dyIn, dzIn});
}

bool CollisionAndSimEngine::checkTorchPoseCollision(
    const core::Transform4d& tcpWorldPose,
    const std::vector<core::SceneNode>& sceneNodes,
    const std::string& activeWorkpieceId,
    bool isWeldingRole,
    double& outMinClearanceMm,
    std::string& outCollidingPartName,
    const std::vector<std::string>& exemptionPartIds) const noexcept {
    outMinClearanceMm = 10000.0;
    outCollidingPartName.clear();

    // Model welding torch as a capsule chain along -Z_tcp (from wire tip s=0 up to torch body s=180 mm)
    // Sample points along torch axis:
    // s = 0 mm (TCP wire tip, radius = 2mm) -> EXEMPTED against activeWorkpieceId during welding!
    // s = 40..160 mm (Nozzle & torch neck body, radius = 22mm) -> Always checked against all parts & clamps!
    struct TorchSphere {
        double offsetAlongNegZMm;
        double radiusMm;
        bool isWireTip;
    };
    constexpr std::array<TorchSphere, 4> torchSpheres{{
        {0.0,   2.0,  true},
        {45.0,  16.0, false},
        {95.0,  22.0, false},
        {150.0, 25.0, false}
    }};

    const core::Vec3 retreatDir = -tcpWorldPose.rot.colZ();

    for (const auto& node : sceneNodes) {
        if (!node.visible || node.type != core::SceneNodeType::Part) {
            continue;
        }
        if (node.collisionRole == core::CollisionRole::Ignored ||
            node.collisionRole == core::CollisionRole::Environment) {
            continue;
        }

        const bool isExemptBaseMetal = [&]() {
            if (node.id == activeWorkpieceId) return true;
            for (const auto& id : exemptionPartIds) {
                if (node.id == id) return true;
            }
            return false;
        }();

        for (const auto& sph : torchSpheres) {
            const core::Vec3 center = tcpWorldPose.pos + retreatDir * sph.offsetAlongNegZMm;
            const double rawDist = pointToAabbSignedDistance(
                center, node.collisionMesh.bboxMin, node.collisionMesh.bboxMax);
            // Wire tip may touch the weld's own base metals inside a small radius.
            // Other workpieces, clamps, and a tip buried deeper than that radius still collide.
            if (sph.isWireTip && isWeldingRole && isExemptBaseMetal &&
                rawDist >= -settings_.wireTipExemptionRadiusMm) {
                continue;
            }

            const double dist = rawDist - sph.radiusMm;

            if (dist < outMinClearanceMm) {
                outMinClearanceMm = dist;
                if (dist <= 0.0) {
                    outCollidingPartName = node.name;
                }
            }
        }
    }

    return (outMinClearanceMm <= 0.0);
}

std::vector<TwistScanSample> CollisionAndSimEngine::scanTwistAngles(
    const core::Transform4d& baseTargetWorldPose,
    const core::Transform4d& robotBaseInWorld,
    const core::Transform4d& utoolOffset,
    const std::array<double, 6>& refJointsRad,
    const core::FanucConfig& requiredConfig,
    const std::vector<core::SceneNode>& sceneNodes,
    const std::string& activeWorkpieceId,
    double stepDeg,
    const std::vector<std::string>& exemptionPartIds) const {
    std::vector<TwistScanSample> samples;
    const core::Transform4d worldToBase = robotBaseInWorld.inverse();

    for (double deg = -180.0; deg < 180.0 - 1e-9; deg += stepDeg) {
        TwistScanSample s;
        s.twistAngleDeg = deg;

        core::Transform4d twistedWorld = baseTargetWorldPose;
        twistedWorld.rot = baseTargetWorldPose.rot * core::Mat3::rotZ(core::deg2rad(deg));

        const core::Transform4d tcpInBase = worldToBase * twistedWorld;
        const auto ikSols = kinematics_.inverseKinematics(tcpInBase, utoolOffset, false);
        const auto bestSol = kinematics_.selectBestSolution(ikSols, refJointsRad, requiredConfig, true);

        if (!bestSol.has_value()) {
            s.reachable = false;
            samples.push_back(s);
            continue;
        }

        s.reachable = true;
        s.nearSingularityOrLimit = bestSol->nearLimits || bestSol->isSingular;

        double minClearance = 0.0;
        std::string hitPart;
        const bool collided = checkTorchPoseCollision(
            twistedWorld, sceneNodes, activeWorkpieceId, true, minClearance, hitPart, exemptionPartIds);
        s.collisionFree = !collided;
        s.minClearanceMm = minClearance;
        samples.push_back(s);
    }
    return samples;
}

SimulationResult CollisionAndSimEngine::validateAndSimulateProgram(
    core::Program& inOutProgram,
    const core::Transform4d& robotBaseInWorld,
    const core::Transform4d& uframeInWorld,
    const core::Transform4d& utoolOffset,
    const std::vector<core::SceneNode>& sceneNodes,
    double sampleStepSec) const {
    SimulationResult sim;
    const core::Transform4d worldToBase = robotBaseInWorld.inverse();

    std::array<double, 6> currentJoints = inOutProgram.startState.homeJointsRad;
    core::Transform4d currentTcpWorld =
        robotBaseInWorld * kinematics_.forwardKinematics(currentJoints, utoolOffset).tcpPoseInBase;
    double currentTime = 0.0;
    bool arcOn = false;

    for (auto& op : inOutProgram.operations) {
        if (!op.enabled) continue;

        std::optional<core::FanucConfig> weldSegmentLockedConfig;

        for (auto& target : op.targets) {
            const core::Transform4d targetWorld = uframeInWorld * target.poseInUFrame;
            const core::Transform4d targetInBase = worldToBase * targetWorld;

            const bool isWeldRole =
                (target.role == core::TargetRole::WELD_START ||
                 target.role == core::TargetRole::WELD_VIA ||
                 target.role == core::TargetRole::WELD_END);

            const auto ikCandidates = kinematics_.inverseKinematics(targetInBase, utoolOffset, false);
            const bool userLocked = target.lockConfig;
            const auto chosen = kinematics_.selectBestSolution(
                ikCandidates,
                currentJoints,
                isWeldRole
                    ? weldSegmentLockedConfig
                    : (userLocked ? std::optional(target.config) : std::nullopt),
                true);

            if (!chosen.has_value()) {
                target.cache.valid = false;
                bool anyInside = false;
                for (const auto& cand : ikCandidates) {
                    if (cand.withinLimits) anyInside = true;
                }
                if (!ikCandidates.empty() && !anyInside) {
                    target.cache.status = core::TargetValidationState::JointLimitExceeded;
                    target.cache.issueMessage = "All inverse solutions exceed a joint limit";
                } else if ((isWeldRole && weldSegmentLockedConfig.has_value()) || userLocked) {
                    target.cache.status = core::TargetValidationState::ConfigJump;
                    target.cache.issueMessage = "No solution keeps the locked configuration";
                } else {
                    target.cache.status = core::TargetValidationState::Unreachable;
                    target.cache.issueMessage = "Target unreachable";
                }
                sim.issues.push_back({
                    inOutProgram.id, op.id, target.id, target.name,
                    target.cache.status, target.cache.issueMessage
                });
                continue;
            }

            if (target.role == core::TargetRole::WELD_START) {
                weldSegmentLockedConfig = chosen->config;
            }

            double minClearance = 1000.0;
            std::string hitName;
            const bool collided = checkTorchPoseCollision(
                targetWorld, sceneNodes, op.targetWorkpieceId, isWeldRole,
                minClearance, hitName, op.exemptionPartIds);

            target.cache.valid = true;
            target.cache.jointsRad = chosen->jointsRad;
            target.cache.solvedConfig = chosen->config;
            target.cache.minClearanceMm = minClearance;
            target.cache.manipulabilityIndex = chosen->manipulability;

            if (collided) {
                target.cache.status = core::TargetValidationState::Collision;
                target.cache.issueMessage = "Collision with " + hitName;
            } else if (minClearance < settings_.collisionSafeClearanceMm) {
                target.cache.status = core::TargetValidationState::NearCollision;
                target.cache.issueMessage = "Clearance below safe distance (" + hitName + ")";
            } else if (chosen->isSingular) {
                target.cache.status = core::TargetValidationState::Singularity;
                target.cache.issueMessage = "Near a wrist, elbow, or shoulder singularity";
            } else if (chosen->nearLimits) {
                target.cache.status = core::TargetValidationState::NearJointLimit;
                target.cache.issueMessage = "A joint is inside the 5 degree warning margin";
            } else {
                target.cache.status = core::TargetValidationState::Valid;
                target.cache.issueMessage.clear();
            }
            if (target.cache.status != core::TargetValidationState::Valid) {
                sim.issues.push_back({
                    inOutProgram.id, op.id, target.id, target.name,
                    target.cache.status, target.cache.issueMessage
                });
            }

            // Segment duration & interpolation (M11)
            const double distMm = (targetWorld.pos - currentTcpWorld.pos).norm();
            double segDuration = 0.5;
            if (target.motion == core::MotionType::L) {
                const double spd = std::max(1.0, target.speedValue);
                segDuration = std::max(0.1, distMm / spd);
            } else {
                const double jDist = OpwKinematics::weightedJointDistance(currentJoints, chosen->jointsRad);
                segDuration = std::max(0.2, jDist / 1.5);
            }

            const int numSteps = std::max(2, static_cast<int>( std::ceil(segDuration / sampleStepSec) ));
            std::array<double, 6> prevStepJoints = currentJoints;
            bool postedSegmentIssue = false;

            for (int step = 1; step <= numSteps; ++step) {
                const double alpha = static_cast<double>(step) / static_cast<double>(numSteps);
                std::array<double, 6> interpJoints{};

                if (target.motion == core::MotionType::J) {
                    for (size_t k = 0; k < 6; ++k) {
                        interpJoints[k] = currentJoints[k] + alpha * (chosen->jointsRad[k] - currentJoints[k]);
                    }
                } else {
                    // Section 6.3: Linear Cartesian interpolation + SLERP with configuration consistency check
                    const core::Transform4d interpWorld =
                        core::Transform4d::interpolateSlerp(currentTcpWorld, targetWorld, alpha);
                    const core::Transform4d interpBase = worldToBase * interpWorld;
                    const auto stepSols = kinematics_.inverseKinematics(interpBase, utoolOffset, false);
                    const auto stepChosen = kinematics_.selectBestSolution(
                        stepSols, prevStepJoints, chosen->config, true);

                    if (!stepChosen.has_value()) {
                        sim.hasConfigJumpInLinear = true;
                        interpJoints = chosen->jointsRad;
                        if (!postedSegmentIssue) {
                            postedSegmentIssue = true;
                            sim.issues.push_back({
                                inOutProgram.id, op.id, target.id, target.name,
                                core::TargetValidationState::Unreachable,
                                "Linear segment has no inverse solution; motion was not interpolated"
                            });
                        }
                    } else {
                        double maxStep = 0.0;
                        for (size_t k = 0; k < 6; ++k) {
                            maxStep = std::max(
                                maxStep,
                                std::abs(core::normalizeAngleRad(stepChosen->jointsRad[k] - prevStepJoints[k])));
                        }
                        if (maxStep > core::deg2rad(15.0)) {
                            sim.hasConfigJumpInLinear = true;
                            if (!postedSegmentIssue) {
                                postedSegmentIssue = true;
                                sim.issues.push_back({
                                    inOutProgram.id, op.id, target.id, target.name,
                                    core::TargetValidationState::ConfigJump,
                                    "Linear segment joint step exceeds 15 degrees"
                                });
                            }
                        }
                        interpJoints = stepChosen->jointsRad;
                    }
                }

                prevStepJoints = interpJoints;
                const auto fk = kinematics_.forwardKinematics(interpJoints, utoolOffset);

                BakedFrame frame;
                frame.timeSec = currentTime + alpha * segDuration;
                frame.jointsRad = interpJoints;
                for (size_t k = 0; k < 7; ++k) {
                    frame.linkWorldTransforms[k] = robotBaseInWorld * fk.linkTransforms[k];
                }
                frame.tcpWorldPos = (robotBaseInWorld * fk.tcpPoseInBase).pos;
                frame.arcActive = arcOn;
                sim.frames.push_back(frame);
            }

            currentTime += segDuration;
            if (arcOn) {
                sim.arcWeldTimeSec += segDuration;
            } else {
                sim.airCutTimeSec += segDuration;
            }

            currentJoints = chosen->jointsRad;
            currentTcpWorld = targetWorld;

            for (const auto& ev : target.events) {
                if (ev.type == core::ProcessEventType::ArcStart) {
                    arcOn = true;
                } else if (ev.type == core::ProcessEventType::ArcEnd) {
                    arcOn = false;
                    weldSegmentLockedConfig.reset();
                }
            }
        }
    }

    sim.totalCycleTimeSec = sim.arcWeldTimeSec + sim.airCutTimeSec;
    return sim;
}

} // namespace olp::algo
