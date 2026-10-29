#include "olp/algo/CollisionAndSim.h"
#include "olp/algo/FclCollisionQueries.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace olp::algo {

namespace {

// FIX-EXPORT-GATE-002 / GATE-005: mid-segment writeback into target.cache.status.
// Policy lives in CollisionAndSimEngine::shouldUpgradeCacheStatus (Hard > Near > Soft;
// within Hard, Collision replaces ConfigJump/Unreachable/JointLimitExceeded).
void upgradeTargetCacheFromSegmentIssue(
    core::Target& target,
    core::TargetValidationState newStatus,
    const std::string& message) {
    if (CollisionAndSimEngine::shouldUpgradeCacheStatus(target.cache.status, newStatus)) {
        target.cache.status = newStatus;
        target.cache.issueMessage = message;
        // Intentionally leave cache.valid unchanged: it means IK solution exists.
    }
}

} // namespace

CollisionAndSimEngine::CollisionAndSimEngine(
    const OpwKinematics& kinematics,
    const core::UnitsSettings& settings)
    : kinematics_(kinematics), settings_(settings) {}

bool CollisionAndSimEngine::shouldUpgradeCacheStatus(
    core::TargetValidationState current,
    core::TargetValidationState incoming) noexcept {
    using S = core::TargetValidationState;
    const auto tierOf = [](S s) noexcept -> int {
        switch (s) {
        case S::Unreachable:
        case S::Collision:
        case S::ConfigJump:
        case S::JointLimitExceeded:
            return 2; // Hard
        case S::NearCollision:
            return 1; // Near
        default:
            return 0; // Soft
        }
    };
    const int inTier = tierOf(incoming);
    const int curTier = tierOf(current);
    if (inTier > curTier) {
        return true;
    }
    // Same Hard tier: prefer Collision so export gate sees hard hits over ConfigJump/etc.
    if (inTier == curTier && inTier == 2 &&
        incoming == S::Collision && current != S::Collision) {
        return true;
    }
    return false;
}

double CollisionAndSimEngine::pointToAabbSignedDistance(
    const core::Vec3& pt,
    const core::Vec3& boxMin,
    const core::Vec3& boxMax) noexcept {
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

bool CollisionAndSimEngine::checkRobotAndTorchCollision(
    const core::Transform4d& tcpWorldPose,
    const std::optional<std::array<double, 6>>& jointsRad,
    const core::Transform4d& robotBaseInWorld,
    const core::Transform4d& utoolOffset,
    const std::vector<core::SceneNode>& sceneNodes,
    const std::string& activeWorkpieceId,
    bool isWeldingRole,
    double& outMinClearanceMm,
    std::string& outCollidingPartName,
    const std::vector<std::string>& exemptionPartIds) const noexcept {
    return queryRobotAndTorchCollision(
        kinematics_, settings_, tcpWorldPose, jointsRad, robotBaseInWorld, utoolOffset,
        sceneNodes, activeWorkpieceId, isWeldingRole, outMinClearanceMm, outCollidingPartName, exemptionPartIds);
}

bool CollisionAndSimEngine::checkTorchPoseCollision(
    const core::Transform4d& tcpWorldPose,
    const std::vector<core::SceneNode>& sceneNodes,
    const std::string& activeWorkpieceId,
    bool isWeldingRole,
    double& outMinClearanceMm,
    std::string& outCollidingPartName,
    const std::vector<std::string>& exemptionPartIds) const noexcept {
    return checkRobotAndTorchCollision(
        tcpWorldPose, std::nullopt, core::Transform4d::identity(), core::Transform4d::identity(),
        sceneNodes, activeWorkpieceId, isWeldingRole, outMinClearanceMm, outCollidingPartName, exemptionPartIds);
}

std::vector<TwistScanSample> CollisionAndSimEngine::scanTwistAngles(
    const core::Transform4d& baseTargetWorldPose,
    const core::Transform4d& robotBaseInWorld,
    const core::Transform4d& utoolOffset,
    const std::array<double, 6>& refJointsRad,
    const std::optional<core::FanucConfig>& requiredConfig,
    const std::vector<core::SceneNode>& sceneNodes,
    const std::string& activeWorkpieceId,
    double stepDeg,
    const std::vector<std::string>& exemptionPartIds,
    bool isWeldingRole,
    const std::shared_ptr<std::atomic<bool>>& cancelToken) const {
    std::vector<TwistScanSample> samples;
    const core::Transform4d worldToBase = robotBaseInWorld.inverse();

    for (double deg = -180.0; deg < 180.0 - 1e-9; deg += stepDeg) {
        if (cancelToken && cancelToken->load()) {
            return {};
        }

        TwistScanSample s;
        s.twistAngleDeg = deg;

        core::Transform4d twistedWorld = baseTargetWorldPose;
        twistedWorld.rot = baseTargetWorldPose.rot * core::Mat3::rotZ(core::deg2rad(deg));

        const core::Transform4d tcpInBase = worldToBase * twistedWorld;
        const auto ikSols = kinematics_.inverseKinematics(tcpInBase, utoolOffset, false);
        const auto bestSol = kinematics_.selectBestSolution(
            ikSols, refJointsRad, requiredConfig, requiredConfig.has_value());

        if (!bestSol.has_value()) {
            s.reachable = false;
            samples.push_back(s);
            continue;
        }

        s.reachable = true;
        s.nearSingularityOrLimit = bestSol->nearLimits || bestSol->isSingular;

        double minClearance = 0.0;
        std::string hitPart;
        const bool collided = checkRobotAndTorchCollision(
            twistedWorld, bestSol->jointsRad, robotBaseInWorld, utoolOffset,
            sceneNodes, activeWorkpieceId, isWeldingRole, minClearance, hitPart, exemptionPartIds);
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
            const bool collided = checkRobotAndTorchCollision(
                targetWorld, chosen->jointsRad, robotBaseInWorld, utoolOffset,
                sceneNodes, op.targetWorkpieceId, isWeldRole,
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
            // FIX-EXPORT-GATE-002: NearCollision must not short-circuit later hard-collision checks.
            bool postedHardSweepCollision = false;
            bool postedNearSweepIssue = false;

            for (int step = 1; step <= numSteps; ++step) {
                const double alpha = static_cast<double>(step) / static_cast<double>(numSteps);
                std::array<double, 6> interpJoints{};
                bool sampleIsCommanded = true;
                core::Transform4d sampleWorld = targetWorld;

                if (target.motion == core::MotionType::J) {
                    for (size_t k = 0; k < 6; ++k) {
                        interpJoints[k] = currentJoints[k] + alpha * (chosen->jointsRad[k] - currentJoints[k]);
                    }
                } else {
                    // Section 6.3: Linear Cartesian interpolation + SLERP with configuration consistency check
                    sampleWorld = core::Transform4d::interpolateSlerp(currentTcpWorld, targetWorld, alpha);
                    const core::Transform4d interpBase = worldToBase * sampleWorld;
                    const auto stepSols = kinematics_.inverseKinematics(interpBase, utoolOffset, false);
                    const auto stepChosen = kinematics_.selectBestSolution(
                        stepSols, prevStepJoints, chosen->config, true);

                    if (!stepChosen.has_value()) {
                        sampleIsCommanded = false;
                        sim.hasConfigJumpInLinear = true;
                        interpJoints = chosen->jointsRad;
                        if (!postedSegmentIssue) {
                            postedSegmentIssue = true;
                            const std::string msg =
                                "Linear segment has no inverse solution; motion was not interpolated";
                            sim.issues.push_back({
                                inOutProgram.id, op.id, target.id, target.name,
                                core::TargetValidationState::Unreachable,
                                msg
                            });
                            upgradeTargetCacheFromSegmentIssue(
                                target, core::TargetValidationState::Unreachable, msg);
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
                                const std::string msg =
                                    "Linear segment joint step exceeds 15 degrees";
                                sim.issues.push_back({
                                    inOutProgram.id, op.id, target.id, target.name,
                                    core::TargetValidationState::ConfigJump,
                                    msg
                                });
                                upgradeTargetCacheFromSegmentIssue(
                                    target, core::TargetValidationState::ConfigJump, msg);
                            }
                        }
                        interpJoints = stepChosen->jointsRad;
                    }
                }

                prevStepJoints = interpJoints;
                const auto fk = kinematics_.forwardKinematics(interpJoints, utoolOffset);
                const core::Transform4d fkTcpWorld = robotBaseInWorld * fk.tcpPoseInBase;
                if (target.motion == core::MotionType::J) {
                    sampleWorld = fkTcpWorld;
                }

                // The last sample is the target pose, already checked above.
                const int sweepCheckInterval = std::max(1, numSteps / 25);
                const bool doSweepCheck = (step % sweepCheckInterval == 0) || (step == numSteps / 2);
                // Keep scanning after NearCollision; only hard Collision short-circuits further sweep posts.
                if (sampleIsCommanded && step < numSteps && !postedHardSweepCollision && doSweepCheck) {
                    double sweepClearance = 0.0;
                    std::string sweepHit;
                    const bool sweepCollided = checkRobotAndTorchCollision(
                        sampleWorld, interpJoints, robotBaseInWorld, utoolOffset,
                        sceneNodes, op.targetWorkpieceId, isWeldRole,
                        sweepClearance, sweepHit, op.exemptionPartIds);
                    if (sweepCollided) {
                        postedHardSweepCollision = true;
                        const std::string msg = "Sweep collides with " + sweepHit;
                        sim.issues.push_back({
                            inOutProgram.id, op.id, target.id, target.name,
                            core::TargetValidationState::Collision,
                            msg
                        });
                        upgradeTargetCacheFromSegmentIssue(
                            target, core::TargetValidationState::Collision, msg);
                    } else if (sweepClearance < settings_.collisionSafeClearanceMm) {
                        if (!postedNearSweepIssue) {
                            postedNearSweepIssue = true;
                            const std::string msg =
                                "Sweep clearance below safe distance (" + sweepHit + ")";
                            sim.issues.push_back({
                                inOutProgram.id, op.id, target.id, target.name,
                                core::TargetValidationState::NearCollision,
                                msg
                            });
                            upgradeTargetCacheFromSegmentIssue(
                                target, core::TargetValidationState::NearCollision, msg);
                        }
                    }
                }

                BakedFrame frame;
                frame.timeSec = currentTime + alpha * segDuration;
                frame.jointsRad = interpJoints;
                for (size_t k = 0; k < 7; ++k) {
                    frame.linkWorldTransforms[k] = robotBaseInWorld * fk.linkTransforms[k];
                }
                frame.tcpWorldPos = fkTcpWorld.pos;
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

OperationTorchClearance CollisionAndSimEngine::measureOperationTorchClearance(
    const core::Operation& operation,
    const core::Transform4d& uframeInWorld,
    const std::vector<core::SceneNode>& sceneNodes) const {
    OperationTorchClearance out;
    out.minClearanceMm = 10000.0;
    bool sawTarget = false;
    for (const auto& target : operation.targets) {
        sawTarget = true;
        const core::Transform4d targetWorld = uframeInWorld * target.poseInUFrame;
        const bool isWeldRole =
            target.role == core::TargetRole::WELD_START ||
            target.role == core::TargetRole::WELD_VIA ||
            target.role == core::TargetRole::WELD_END;
        double clearance = 0.0;
        std::string hitName;
        checkTorchPoseCollision(
            targetWorld, sceneNodes, operation.targetWorkpieceId, isWeldRole,
            clearance, hitName, operation.exemptionPartIds);
        if (clearance < out.minClearanceMm) {
            out.minClearanceMm = clearance;
            out.targetName = target.name;
            out.partName = hitName;
        }
    }
    if (!sawTarget) {
        out.minClearanceMm = -1.0;
        out.meetsSafeDistance = false;
        return out;
    }
    out.meetsSafeDistance = out.minClearanceMm >= settings_.collisionSafeClearanceMm;
    return out;
}

namespace {

struct TorchSphereSpec {
    double offsetAlongNegZMm;
    double radiusMm;
};

constexpr std::array<TorchSphereSpec, 4> kTorchSpheres{{
    {0.0, 2.0},
    {45.0, 16.0},
    {95.0, 22.0},
    {150.0, 25.0}
}};

double lowestSphereBelowTcp(const core::Transform4d& tcpWorld) noexcept {
    const core::Vec3 retreatDir = -tcpWorld.rot.colZ();
    double lowest = 0.0;
    for (const auto& sph : kTorchSpheres) {
        lowest = std::min(lowest, retreatDir.z * sph.offsetAlongNegZMm - sph.radiusMm);
    }
    return lowest;
}

core::Vec3 horizontalUnit(const core::Vec3& dir, const core::Vec3& fallback) noexcept {
    core::Vec3 flat{dir.x, dir.y, 0.0};
    if (flat.norm() < 1e-6) {
        flat = {fallback.x, fallback.y, 0.0};
    }
    if (flat.norm() < 1e-6) {
        flat = {1.0, 0.0, 0.0};
    }
    return flat.normalized();
}

} // namespace

CollisionAndSimEngine::TransitProbeKind CollisionAndSimEngine::probeTorchTransit(
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
    std::array<double, 6>& outEndJoints) const {
    const core::Transform4d worldToBase = robotBaseInWorld.inverse();
    const auto ikCandidates = kinematics_.inverseKinematics(worldToBase * endWorld, utoolOffset, false);
    const auto chosen = kinematics_.selectBestSolution(
        ikCandidates, startJoints, std::nullopt, true);
    if (!chosen.has_value()) {
        return TransitProbeKind::Blocked;
    }
    outEndJoints = chosen->jointsRad;

    double endClearance = 0.0;
    std::string endHit;
    const bool endHits = checkRobotAndTorchCollision(
        endWorld, chosen->jointsRad, robotBaseInWorld, utoolOffset,
        sceneNodes, activeWorkpieceId, false, endClearance, endHit, exemptionPartIds);
    if (endHits) {
        return TransitProbeKind::Blocked;
    }

    double segDuration = 0.5;
    if (motion == core::MotionType::L) {
        const double distMm = (endWorld.pos - startTcpWorld.pos).norm();
        const double spd = std::max(1.0, speedValue);
        segDuration = std::max(0.1, distMm / spd);
    } else {
        const double jDist = OpwKinematics::weightedJointDistance(startJoints, chosen->jointsRad);
        segDuration = std::max(0.2, jDist / 1.5);
    }
    const double stepSec = std::max(1e-3, sampleStepSec);
    const int numSteps = std::max(2, static_cast<int>(std::ceil(segDuration / stepSec)));

    bool near = endClearance < settings_.collisionSafeClearanceMm;
    std::array<double, 6> prevStepJoints = startJoints;
    for (int step = 1; step < numSteps; ++step) {
        const double alpha = static_cast<double>(step) / static_cast<double>(numSteps);
        core::Transform4d sampleWorld = endWorld;
        std::array<double, 6> interpJoints{};

        if (motion == core::MotionType::J) {
            for (size_t k = 0; k < 6; ++k) {
                interpJoints[k] = startJoints[k] + alpha * (chosen->jointsRad[k] - startJoints[k]);
            }
        } else {
            sampleWorld = core::Transform4d::interpolateSlerp(startTcpWorld, endWorld, alpha);
            const auto stepSols = kinematics_.inverseKinematics(worldToBase * sampleWorld, utoolOffset, false);
            const auto stepChosen = kinematics_.selectBestSolution(
                stepSols, prevStepJoints, chosen->config, true);
            if (!stepChosen.has_value()) {
                return TransitProbeKind::Blocked;
            }
            double maxStep = 0.0;
            for (size_t k = 0; k < 6; ++k) {
                maxStep = std::max(
                    maxStep,
                    std::abs(core::normalizeAngleRad(stepChosen->jointsRad[k] - prevStepJoints[k])));
            }
            if (maxStep > core::deg2rad(15.0)) {
                return TransitProbeKind::Blocked;
            }
            interpJoints = stepChosen->jointsRad;
        }

        prevStepJoints = interpJoints;
        if (motion == core::MotionType::J) {
            const auto fk = kinematics_.forwardKinematics(interpJoints, utoolOffset);
            sampleWorld = robotBaseInWorld * fk.tcpPoseInBase;
        }

        double sweepClearance = 0.0;
        std::string sweepHit;
        const bool sweepHits = checkRobotAndTorchCollision(
            sampleWorld, interpJoints, robotBaseInWorld, utoolOffset,
            sceneNodes, activeWorkpieceId, false,
            sweepClearance, sweepHit, exemptionPartIds);
        if (sweepHits) {
            return TransitProbeKind::Blocked;
        }
        if (sweepClearance < settings_.collisionSafeClearanceMm) {
            near = true;
        }
    }
    return near ? TransitProbeKind::Near : TransitProbeKind::Clear;
}

bool CollisionAndSimEngine::insertSafeTransitBeforeApproach(
    core::Program& inOutProgram,
    const core::Transform4d& robotBaseInWorld,
    const core::Transform4d& uframeInWorld,
    const core::Transform4d& utoolOffset,
    const std::vector<core::SceneNode>& sceneNodes,
    double sampleStepSec) const {
    if (inOutProgram.operations.empty()) {
        return true;
    }
    core::Operation* approachOp = nullptr;
    int approachIndex = -1;
    for (auto& op : inOutProgram.operations) {
        if (!op.enabled) {
            continue;
        }
        for (int i = 0; i < static_cast<int>(op.targets.size()); ++i) {
            if (op.targets[static_cast<size_t>(i)].role == core::TargetRole::APPROACH) {
                approachOp = &op;
                approachIndex = i;
                break;
            }
        }
        if (approachOp != nullptr) {
            break;
        }
    }
    if (approachOp == nullptr || approachIndex < 0) {
        return true;
    }
    for (int i = 0; i < approachIndex; ++i) {
        const std::string& name = approachOp->targets[static_cast<size_t>(i)].name;
        if (name.rfind("CLEARANCE_", 0) != 0) {
            return false;
        }
    }

    const double stepSec = std::max(1e-3, sampleStepSec);
    const std::array<double, 6> homeJoints = inOutProgram.startState.homeJointsRad;
    core::Transform4d tcp =
        robotBaseInWorld * kinematics_.forwardKinematics(homeJoints, utoolOffset).tcpPoseInBase;
    std::array<double, 6> joints = homeJoints;
    bool chainBlocked = false;
    for (int i = 0; i <= approachIndex; ++i) {
        const auto& target = approachOp->targets[static_cast<size_t>(i)];
        const core::Transform4d world = uframeInWorld * target.poseInUFrame;
        std::array<double, 6> endJoints{};
        const auto kind = probeTorchTransit(
            joints, tcp, world, target.motion, target.speedValue,
            robotBaseInWorld, utoolOffset, sceneNodes,
            approachOp->targetWorkpieceId, approachOp->exemptionPartIds,
            stepSec, endJoints);
        if (kind == TransitProbeKind::Blocked) {
            chainBlocked = true;
            break;
        }
        joints = endJoints;
        tcp = world;
    }
    if (!chainBlocked) {
        return true;
    }

    const core::Transform4d homeTcp =
        robotBaseInWorld * kinematics_.forwardKinematics(homeJoints, utoolOffset).tcpPoseInBase;
    const core::Transform4d approachWorld =
        uframeInWorld * approachOp->targets[static_cast<size_t>(approachIndex)].poseInUFrame;

    double partTop = homeTcp.pos.z;
    for (const auto& node : sceneNodes) {
        if (!node.visible || node.type != core::SceneNodeType::Part) {
            continue;
        }
        if (node.collisionRole == core::CollisionRole::Ignored ||
            node.collisionRole == core::CollisionRole::Environment) {
            continue;
        }
        partTop = std::max(partTop, node.collisionMesh.bboxMax.z);
    }
    const double hang = std::min(lowestSphereBelowTcp(homeTcp), lowestSphereBelowTcp(approachWorld));
    const double safeZ = partTop - hang + settings_.collisionSafeClearanceMm + 25.0;
    const double floorZ = std::max(homeTcp.pos.z, std::max(approachWorld.pos.z, safeZ));
    const core::Vec3 retreat = horizontalUnit(-approachWorld.rot.colZ(), -homeTcp.rot.colZ());

    struct TransitPoint {
        core::Transform4d world{};
        core::MotionType motion{core::MotionType::L};
        double speed{200.0};
        bool isApproach{false};
    };
    std::vector<TransitPoint> chosen;
    auto tryPath = [&](std::vector<TransitPoint> points) -> bool {
        if (points.empty() || !points.back().isApproach) {
            return false;
        }
        std::array<double, 6> cursorJoints = homeJoints;
        core::Transform4d cursorTcp = homeTcp;
        for (const auto& pt : points) {
            std::array<double, 6> endJoints{};
            const auto kind = probeTorchTransit(
                cursorJoints, cursorTcp, pt.world, pt.motion, pt.speed,
                robotBaseInWorld, utoolOffset, sceneNodes,
                approachOp->targetWorkpieceId, approachOp->exemptionPartIds,
                stepSec, endJoints);
            if (kind == TransitProbeKind::Blocked) {
                return false;
            }
            cursorJoints = endJoints;
            cursorTcp = pt.world;
        }
        chosen = std::move(points);
        return true;
    };

    TransitPoint approachStep;
    approachStep.world = approachWorld;
    approachStep.motion = core::MotionType::L;
    approachStep.speed = 200.0;
    approachStep.isApproach = true;
    if (tryPath({approachStep})) {
        chosen = std::vector<TransitPoint>{approachStep};
    }

    const double lifts[] = {0.0, 200.0, 450.0, 800.0};
    const double retreats[] = {0.0, 120.0, 280.0};
    auto makePose = [](const core::Vec3& pos, const core::Mat3& rot) {
        core::Transform4d pose;
        pose.pos = pos;
        pose.rot = rot;
        return pose;
    };
    if (chosen.empty()) {
        for (double extra : lifts) {
            const double z = floorZ + extra;
            for (double retreatDist : retreats) {
                const core::Vec3 xy = approachWorld.pos + retreat * retreatDist;
                if ((core::Vec3{xy.x, xy.y, z} - approachWorld.pos).norm() < 5.0) {
                    continue;
                }
                TransitPoint via;
                via.speed = 200.0;
                via.motion = core::MotionType::L;
                via.world = makePose({xy.x, xy.y, z}, approachWorld.rot);
                if (tryPath({via, approachStep})) {
                    break;
                }
                via.world = makePose({xy.x, xy.y, z}, homeTcp.rot);
                if (tryPath({via, approachStep})) {
                    break;
                }
            }
            if (!chosen.empty()) {
                break;
            }

            std::vector<TransitPoint> lifted;
            TransitPoint hover;
            hover.world = makePose({homeTcp.pos.x, homeTcp.pos.y, z}, homeTcp.rot);
            hover.motion = core::MotionType::L;
            hover.speed = 200.0;
            if ((hover.world.pos - homeTcp.pos).norm() >= 5.0) {
                lifted.push_back(hover);
            }
            TransitPoint cruise;
            cruise.world = makePose({approachWorld.pos.x, approachWorld.pos.y, z}, approachWorld.rot);
            cruise.motion = core::MotionType::L;
            cruise.speed = 200.0;
            if ((cruise.world.pos - approachWorld.pos).norm() >= 5.0) {
                lifted.push_back(cruise);
            }
            lifted.push_back(approachStep);
            if (tryPath(lifted)) {
                break;
            }

            const core::Vec3 parked = approachWorld.pos + retreat * 200.0;
            TransitPoint park;
            park.world = makePose({parked.x, parked.y, z}, approachWorld.rot);
            park.motion = core::MotionType::L;
            park.speed = 200.0;
            std::vector<TransitPoint> parkedPath;
            if ((hover.world.pos - homeTcp.pos).norm() >= 5.0) {
                parkedPath.push_back(hover);
            }
            if ((park.world.pos - approachWorld.pos).norm() >= 5.0) {
                parkedPath.push_back(park);
            }
            if ((cruise.world.pos - approachWorld.pos).norm() >= 5.0 &&
                (cruise.world.pos - park.world.pos).norm() >= 5.0) {
                parkedPath.push_back(cruise);
            }
            parkedPath.push_back(approachStep);
            if (tryPath(parkedPath)) {
                break;
            }
        }
    }
    if (chosen.empty()) {
        for (double extra : lifts) {
            const double z = floorZ + extra;
            if ((core::Vec3{approachWorld.pos.x, approachWorld.pos.y, z} - approachWorld.pos).norm() < 5.0) {
                continue;
            }
            TransitPoint via;
            via.world = makePose(
                {approachWorld.pos.x, approachWorld.pos.y, z}, approachWorld.rot);
            via.motion = core::MotionType::J;
            via.speed = 50.0;
            if (tryPath({via, approachStep})) {
                break;
            }
        }
    }
    if (chosen.empty()) {
        return false;
    }

    core::Target approach = approachOp->targets[static_cast<size_t>(approachIndex)];
    std::vector<core::Target> rebuilt;
    rebuilt.reserve(approachOp->targets.size() + chosen.size());
    int ordinal = 1;
    for (const auto& pt : chosen) {
        if (pt.isApproach) {
            approach.motion = pt.motion;
            approach.speedValue = pt.speed;
            rebuilt.push_back(approach);
            continue;
        }
        core::Target via;
        via.id = approachOp->id + "_T_CLEARANCE_" + std::to_string(ordinal);
        via.name = "CLEARANCE_" + std::to_string(ordinal);
        via.role = core::TargetRole::VIA;
        via.motion = pt.motion;
        via.speedValue = pt.speed;
        via.termination = core::TerminationType::FINE;
        via.poseInUFrame = uframeInWorld.inverse() * pt.world;
        rebuilt.push_back(std::move(via));
        ++ordinal;
    }
    for (size_t i = static_cast<size_t>(approachIndex) + 1; i < approachOp->targets.size(); ++i) {
        rebuilt.push_back(approachOp->targets[i]);
    }
    approachOp->targets = std::move(rebuilt);
    return true;
}

} // namespace olp::algo
