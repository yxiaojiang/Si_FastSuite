#include "olp/algo/TrajectorySimulator.h"
#include "olp/algo/FclCollisionQueries.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace olp::algo {
namespace {

constexpr double kTimeEps = 1e-9;
constexpr int kMaxLinearSamples = 240;

double clampSpeedPercent(double speedValue) noexcept {
    return std::clamp(speedValue, 1.0, 100.0) / 100.0;
}

double rawAbsDelta(double a, double b) noexcept {
    return std::abs(b - a);
}

// Shortest revolute step. Linear samples store this continuation so the 15° check,
// the trapezoid, and jointsAt all see one increment. Joint moves keep rawAbsDelta.
struct ShortJointStep {
    std::array<double, 6> jointsRad{};
    double maxAbsRad{0.0};
};

ShortJointStep shortestJointStep(
    const std::array<double, 6>& prev,
    const std::array<double, 6>& candidate) noexcept {
    ShortJointStep step;
    for (size_t axis = 0; axis < 6; ++axis) {
        const double delta = core::normalizeAngleRad(candidate[axis] - prev[axis]);
        step.jointsRad[axis] = prev[axis] + delta;
        step.maxAbsRad = std::max(step.maxAbsRad, std::abs(delta));
    }
    return step;
}

// Locked turns miss a joint folded to exactly +180 degrees. Among the same arm
// posture, the nearest raw candidate can be a different branch; rank by the short step.
const IkSolution* samePostureByShortStep(
    const std::vector<IkSolution>& candidates,
    const std::array<double, 6>& prev,
    const core::FanucConfig& locked) {
    const IkSolution* best = nullptr;
    double bestCost = std::numeric_limits<double>::infinity();
    for (const auto& sol : candidates) {
        if (!sol.withinLimits) {
            continue;
        }
        if (sol.config.flip != locked.flip || sol.config.up != locked.up || sol.config.top != locked.top) {
            continue;
        }
        const ShortJointStep stepped = shortestJointStep(prev, sol.jointsRad);
        double cost = 0.0;
        for (size_t axis = 0; axis < 6; ++axis) {
            const double delta = stepped.jointsRad[axis] - prev[axis];
            cost += delta * delta;
        }
        if (cost < bestCost) {
            bestCost = cost;
            best = &sol;
        }
    }
    return best;
}

std::array<double, 6> lerpJoints(
    const std::array<double, 6>& a,
    const std::array<double, 6>& b,
    double u) noexcept {
    std::array<double, 6> out{};
    for (size_t k = 0; k < 6; ++k) {
        out[k] = a[k] + u * (b[k] - a[k]);
    }
    return out;
}

std::array<double, 6> jointsAt(const TrajectoryPlan::Segment& segment, double s) {
    if (segment.samples.empty()) {
        return {};
    }
    if (segment.samples.size() == 1 || s <= segment.samples.front().s) {
        return segment.samples.front().jointsRad;
    }
    if (s >= segment.samples.back().s) {
        return segment.samples.back().jointsRad;
    }
    for (size_t i = 1; i < segment.samples.size(); ++i) {
        const auto& right = segment.samples[i];
        if (s > right.s) {
            continue;
        }
        const auto& left = segment.samples[i - 1];
        const double span = right.s - left.s;
        const double u = span > kTimeEps ? (s - left.s) / span : 1.0;
        return lerpJoints(left.jointsRad, right.jointsRad, std::clamp(u, 0.0, 1.0));
    }
    return segment.samples.back().jointsRad;
}

const TrajectoryPlan::Segment* segmentAt(const TrajectoryPlan& plan, double timeSec) {
    const TrajectoryPlan::Segment* active = nullptr;
    for (const auto& segment : plan.segments) {
        if (segment.startTimeSec <= timeSec + 1e-12) {
            active = &segment;
        } else {
            break;
        }
    }
    return active;
}

void limitByJoints(
    const std::vector<TrajectoryPlan::Sample>& samples,
    const std::array<device::JointLimit, 6>& limits,
    double velocityScale,
    double& vS,
    double& aS) {
    for (size_t i = 1; i < samples.size(); ++i) {
        const double ds = samples[i].s - samples[i - 1].s;
        if (ds < 1e-9) {
            continue;
        }
        for (size_t axis = 0; axis < 6; ++axis) {
            const double dq = rawAbsDelta(samples[i - 1].jointsRad[axis], samples[i].jointsRad[axis]);
            if (dq < 1e-9) {
                continue;
            }
            const double dqds = dq / ds;
            const double vMax = std::max(1e-6, limits[axis].maxVelRadPerSec * velocityScale);
            const double aMax = std::max(1e-6, limits[axis].maxAccRadPerSec2);
            vS = std::min(vS, vMax / dqds);
            aS = std::min(aS, aMax / dqds);
        }
    }
}

OperationTiming& timingFor(TrajectoryPlan& plan, const core::Operation& operation) {
    for (auto& row : plan.operations) {
        if (row.operationId == operation.id) {
            return row;
        }
    }
    OperationTiming row;
    row.operationId = operation.id;
    row.operationName = operation.name;
    plan.operations.push_back(row);
    return plan.operations.back();
}

} // namespace

double TrapezoidProfile::position(double t) const noexcept {
    if (distance <= 0.0 || duration <= kTimeEps) {
        return distance;
    }
    if (t <= 0.0) {
        return 0.0;
    }
    if (t >= duration) {
        return distance;
    }
    if (t < tAcc) {
        return 0.5 * aMax * t * t;
    }
    if (t < tAcc + tCruise) {
        const double dAcc = 0.5 * aMax * tAcc * tAcc;
        return dAcc + vPeak * (t - tAcc);
    }
    const double remaining = duration - t;
    return distance - 0.5 * aMax * remaining * remaining;
}

double TrapezoidProfile::velocity(double t) const noexcept {
    if (duration <= kTimeEps || t <= 0.0 || t >= duration) {
        return 0.0;
    }
    if (t < tAcc) {
        return aMax * t;
    }
    if (t < tAcc + tCruise) {
        return vPeak;
    }
    return aMax * (duration - t);
}

TrapezoidProfile makeTrapezoid(double distance, double vMax, double aMax) noexcept {
    TrapezoidProfile profile;
    profile.distance = std::max(0.0, distance);
    if (profile.distance < 1e-12) {
        return profile;
    }
    vMax = std::max(vMax, 1e-9);
    aMax = std::max(aMax, 1e-9);
    const double tToV = vMax / aMax;
    const double accelDistance = 0.5 * aMax * tToV * tToV;
    if (2.0 * accelDistance >= profile.distance) {
        profile.tAcc = std::sqrt(profile.distance / aMax);
        profile.tCruise = 0.0;
        profile.vPeak = aMax * profile.tAcc;
        profile.aMax = aMax;
        profile.duration = 2.0 * profile.tAcc;
        return profile;
    }
    profile.tAcc = tToV;
    profile.vPeak = vMax;
    profile.aMax = aMax;
    profile.tCruise = (profile.distance - 2.0 * accelDistance) / vMax;
    profile.duration = 2.0 * profile.tAcc + profile.tCruise;
    return profile;
}

TrajectorySimulator::TrajectorySimulator(
    const OpwKinematics& kinematics,
    const device::RobotModel& model)
    : kinematics_(kinematics), limits_(model.jointLimits) {}

TrajectoryPlan TrajectorySimulator::plan(
    const core::Program& program,
    const core::Transform4d& robotBaseInWorld,
    const core::Transform4d& uframeInWorld,
    const core::Transform4d& utoolOffset,
    double framesPerSec) const {
    TrajectoryPlan plan;
    plan.startJointsRad = program.startState.homeJointsRad;
    plan.robotBaseInWorld = robotBaseInWorld;
    plan.utoolOffset = utoolOffset;

    const double fps = framesPerSec < 1.0 ? 30.0 : framesPerSec;
    const core::Transform4d worldToBase = robotBaseInWorld.inverse();
    std::array<double, 6> currentJoints = plan.startJointsRad;
    core::Transform4d currentTcpWorld =
        robotBaseInWorld * kinematics_.forwardKinematics(currentJoints, utoolOffset).tcpPoseInBase;
    double currentTime = 0.0;
    bool arcOn = false;
    std::optional<core::FanucConfig> weldLock;

    for (const auto& operation : program.operations) {
        if (!operation.enabled) {
            continue;
        }
        for (const auto& target : operation.targets) {
            const bool isWeldRole =
                target.role == core::TargetRole::WELD_START ||
                target.role == core::TargetRole::WELD_VIA ||
                target.role == core::TargetRole::WELD_END;
            const core::Transform4d targetWorld = uframeInWorld * target.poseInUFrame;
            const core::Transform4d targetInBase = worldToBase * targetWorld;
            const auto candidates = kinematics_.inverseKinematics(targetInBase, utoolOffset, false);
            const auto chosen = kinematics_.selectBestSolution(
                candidates,
                currentJoints,
                isWeldRole ? weldLock : (target.lockConfig ? std::optional(target.config) : std::nullopt),
                true);

            if (!chosen.has_value()) {
                plan.issues.push_back({
                    program.id, operation.id, target.id, target.name,
                    core::TargetValidationState::Unreachable,
                    "M11 target has no inverse solution; the segment was skipped"
                });
                continue;
            }
            if (target.role == core::TargetRole::WELD_START) {
                weldLock = chosen->config;
            }

            TrajectoryPlan::Segment segment;
            segment.operationId = operation.id;
            segment.targetId = target.id;
            segment.motion = target.motion == core::MotionType::C ? core::MotionType::L : target.motion;
            segment.startTimeSec = currentTime;
            segment.arcDuring = arcOn;
            segment.samples.push_back({0.0, currentJoints});

            if (target.motion == core::MotionType::C) {
                plan.issues.push_back({
                    program.id, operation.id, target.id, target.name,
                    core::TargetValidationState::Valid,
                    "Circular motion is approximated as a straight line in this estimate"
                });
            }

            if (segment.motion == core::MotionType::J) {
                segment.samples.push_back({1.0, chosen->jointsRad});
                double vS = std::numeric_limits<double>::infinity();
                double aS = std::numeric_limits<double>::infinity();
                limitByJoints(segment.samples, limits_, clampSpeedPercent(target.speedValue), vS, aS);
                if (std::isfinite(vS) && std::isfinite(aS)) {
                    segment.profile = makeTrapezoid(1.0, vS, aS);
                }
            } else {
                const double distMm = (targetWorld.pos - currentTcpWorld.pos).norm();
                const double angleRad = currentTcpWorld.rot.angularDistanceRad(targetWorld.rot);
                const int steps = std::clamp(
                    static_cast<int>(std::ceil(std::max(
                        distMm / kLinearSampleStepMm,
                        core::rad2deg(angleRad) / kLinearSampleStepDeg))),
                    2,
                    kMaxLinearSamples);
                std::array<double, 6> prev = currentJoints;
                bool postedJump = false;
                const core::FanucConfig locked = chosen->config;
                const auto noteLinearIssue = [&](core::TargetValidationState state, const char* text) {
                    plan.hasConfigJumpInLinear = true;
                    if (postedJump) {
                        return;
                    }
                    postedJump = true;
                    plan.issues.push_back({
                        program.id, operation.id, target.id, target.name, state, text
                    });
                };
                for (int step = 1; step < steps; ++step) {
                    const double s = static_cast<double>(step) / static_cast<double>(steps);
                    const core::Transform4d sampleWorld =
                        core::Transform4d::interpolateSlerp(currentTcpWorld, targetWorld, s);
                    const auto stepSols = kinematics_.inverseKinematics(
                        worldToBase * sampleWorld, utoolOffset, false);
                    auto stepChosen = kinematics_.selectBestSolution(
                        stepSols, prev, locked, true);
                    if (!stepChosen.has_value()) {
                        if (const IkSolution* folded = samePostureByShortStep(stepSols, prev, locked)) {
                            stepChosen = *folded;
                        }
                    }
                    if (!stepChosen.has_value()) {
                        noteLinearIssue(
                            core::TargetValidationState::Unreachable,
                            "Linear segment has no inverse solution; motion was not interpolated");
                        segment.samples.push_back({s, prev});
                        continue;
                    }
                    const ShortJointStep stepped = shortestJointStep(prev, stepChosen->jointsRad);
                    if (stepped.maxAbsRad > core::deg2rad(kConfigJumpLimitDeg)) {
                        noteLinearIssue(
                            core::TargetValidationState::ConfigJump,
                            "Linear segment joint step exceeds 15 degrees");
                    }
                    prev = stepped.jointsRad;
                    segment.samples.push_back({s, prev});
                }
                const ShortJointStep aligned = shortestJointStep(prev, chosen->jointsRad);
                if (aligned.maxAbsRad > core::deg2rad(kConfigJumpLimitDeg)) {
                    noteLinearIssue(
                        core::TargetValidationState::ConfigJump,
                        "Linear segment joint step exceeds 15 degrees");
                }
                segment.samples.push_back({1.0, aligned.jointsRad});

                double vS = std::numeric_limits<double>::infinity();
                double aS = std::numeric_limits<double>::infinity();
                limitByJoints(segment.samples, limits_, 1.0, vS, aS);
                if (distMm > 1e-4) {
                    const double speed = std::max(1.0, target.speedValue);
                    vS = std::min(vS, speed / distMm);
                    aS = std::min(aS, kEstimatedCartesianAccelMmPerSec2 / distMm);
                }
                if (std::isfinite(vS) && std::isfinite(aS)) {
                    segment.profile = makeTrapezoid(1.0, vS, aS);
                }
            }

            segment.durationSec = segment.profile.duration;
            for (const auto& event : target.events) {
                if (event.type == core::ProcessEventType::ArcStart) {
                    arcOn = true;
                } else if (event.type == core::ProcessEventType::ArcEnd) {
                    arcOn = false;
                    weldLock.reset();
                }
            }
            segment.arcAfter = arcOn;

            auto& timing = timingFor(plan, operation);
            if (segment.arcDuring) {
                timing.weldTimeSec += segment.durationSec;
                plan.arcWeldTimeSec += segment.durationSec;
            } else {
                timing.airTimeSec += segment.durationSec;
                plan.airCutTimeSec += segment.durationSec;
            }
            timing.totalTimeSec += segment.durationSec;

            currentTime += segment.durationSec;
            currentJoints = chosen->jointsRad;
            currentTcpWorld = targetWorld;
            plan.segments.push_back(std::move(segment));
        }
    }

    plan.totalCycleTimeSec = plan.arcWeldTimeSec + plan.airCutTimeSec;

    std::vector<double> times{0.0};
    if (plan.totalCycleTimeSec > kTimeEps) {
        const double dt = 1.0 / fps;
        for (double t = dt; t < plan.totalCycleTimeSec - 1e-8; t += dt) {
            times.push_back(t);
        }
        times.push_back(plan.totalCycleTimeSec);
    }
    for (const auto& segment : plan.segments) {
        times.push_back(segment.startTimeSec + segment.durationSec);
    }
    std::sort(times.begin(), times.end());
    std::vector<double> uniqueTimes;
    for (double t : times) {
        if (uniqueTimes.empty() || t - uniqueTimes.back() > 1e-6) {
            uniqueTimes.push_back(t);
        }
    }
    plan.frames.reserve(uniqueTimes.size());
    for (double t : uniqueTimes) {
        plan.frames.push_back(sampleTrajectory(plan, kinematics_, t));
    }
    return plan;
}

BakedFrame sampleTrajectory(
    const TrajectoryPlan& plan,
    const OpwKinematics& kinematics,
    double timeSec) {
    const double t = std::clamp(timeSec, 0.0, std::max(0.0, plan.totalCycleTimeSec));
    std::array<double, 6> joints = plan.startJointsRad;
    bool arc = false;
    if (const auto* segment = segmentAt(plan, t)) {
        const double local = std::clamp(t - segment->startTimeSec, 0.0, segment->durationSec);
        double s = 1.0;
        if (segment->durationSec > kTimeEps && segment->profile.distance > kTimeEps) {
            s = segment->profile.position(local) / segment->profile.distance;
        }
        joints = jointsAt(*segment, std::clamp(s, 0.0, 1.0));
        const bool arrived = local >= segment->durationSec - 1e-8;
        arc = arrived ? segment->arcAfter : segment->arcDuring;
    }

    const auto fk = kinematics.forwardKinematics(joints, plan.utoolOffset);
    BakedFrame frame;
    frame.timeSec = t;
    frame.jointsRad = joints;
    frame.linkWorldTransforms = forwardRobotLinkWorldFrames(joints, plan.robotBaseInWorld);
    frame.tcpWorldPos = (plan.robotBaseInWorld * fk.tcpPoseInBase).pos;
    frame.arcActive = arc;
    return frame;
}

void PlaybackCursor::bind(const TrajectoryPlan& plan, const OpwKinematics& kinematics) {
    plan_ = &plan;
    kinematics_ = &kinematics;
    timeSec_ = 0.0;
    state_ = PlaybackState::Stopped;
}

void PlaybackCursor::unbind() noexcept {
    plan_ = nullptr;
    kinematics_ = nullptr;
    timeSec_ = 0.0;
    state_ = PlaybackState::Stopped;
}

void PlaybackCursor::play() {
    if (!isBound()) {
        return;
    }
    state_ = PlaybackState::Playing;
}

void PlaybackCursor::playFromStartIfFinished() {
    if (timeSec_ >= durationSec() - 1e-8) {
        timeSec_ = 0.0;
    }
    play();
}

void PlaybackCursor::pause() {
    if (state_ == PlaybackState::Playing) {
        state_ = PlaybackState::Paused;
    }
}

void PlaybackCursor::stop() {
    timeSec_ = 0.0;
    state_ = PlaybackState::Stopped;
}

void PlaybackCursor::setSpeedMultiplier(double multiplier) {
    speed_ = std::clamp(multiplier, 0.1, 8.0);
}

void PlaybackCursor::seek(double time) {
    timeSec_ = std::clamp(time, 0.0, durationSec());
}

bool PlaybackCursor::seekTarget(const std::string& targetId) {
    if (!isBound()) {
        return false;
    }
    for (const auto& segment : plan_->segments) {
        if (segment.targetId == targetId) {
            seek(segment.startTimeSec + segment.durationSec);
            return true;
        }
    }
    return false;
}

void PlaybackCursor::stepTime(double simDtSec) {
    pause();
    if (state_ == PlaybackState::Stopped) {
        state_ = PlaybackState::Paused;
    }
    seek(timeSec_ + simDtSec);
}

void PlaybackCursor::stepTarget(int direction) {
    pause();
    if (!isBound()) {
        return;
    }
    if (state_ == PlaybackState::Stopped) {
        state_ = PlaybackState::Paused;
    }
    std::vector<double> marks{0.0};
    for (const auto& segment : plan_->segments) {
        marks.push_back(segment.startTimeSec + segment.durationSec);
    }
    std::sort(marks.begin(), marks.end());
    marks.erase(std::unique(marks.begin(), marks.end(), [](double a, double b) {
        return std::abs(a - b) < 1e-6;
    }), marks.end());
    if (direction >= 0) {
        for (double mark : marks) {
            if (mark > timeSec_ + 1e-6) {
                seek(mark);
                return;
            }
        }
        seek(durationSec());
        return;
    }
    for (auto it = marks.rbegin(); it != marks.rend(); ++it) {
        if (*it < timeSec_ - 1e-6) {
            seek(*it);
            return;
        }
    }
    seek(0.0);
}

void PlaybackCursor::advanceWall(double wallDtSec) {
    if (state_ != PlaybackState::Playing || !isBound()) {
        return;
    }
    timeSec_ = std::min(durationSec(), timeSec_ + std::max(0.0, wallDtSec) * speed_);
    if (timeSec_ >= durationSec() - 1e-8) {
        timeSec_ = durationSec();
        state_ = PlaybackState::Paused;
    }
}

BakedFrame PlaybackCursor::sample() const {
    if (!isBound()) {
        return {};
    }
    return sampleTrajectory(*plan_, *kinematics_, timeSec_);
}

double PlaybackCursor::durationSec() const noexcept {
    return plan_ != nullptr ? plan_->totalCycleTimeSec : 0.0;
}

} // namespace olp::algo
