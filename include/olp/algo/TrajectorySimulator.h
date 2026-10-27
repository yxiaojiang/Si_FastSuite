#pragma once

#include "olp/algo/CollisionAndSim.h"
#include "olp/device/RobotModel.h"

#include <optional>

namespace olp::algo {

// Estimated path acceleration used when a linear move is not already limited by a joint.
// Cycle time is an estimate: geometry follows the interpolator, not a FANUC RRS kernel.
inline constexpr double kEstimatedCartesianAccelMmPerSec2 = 2000.0;
inline constexpr double kLinearSampleStepMm = 5.0;
inline constexpr double kLinearSampleStepDeg = 2.0;
inline constexpr double kConfigJumpLimitDeg = 15.0;

// Scalar trapezoid on a path coordinate. Distance is whatever unit the caller uses (rad, mm, or 1).
struct TrapezoidProfile {
    double distance{0.0};
    double vPeak{0.0};
    double aMax{0.0};
    double tAcc{0.0};
    double tCruise{0.0};
    double duration{0.0};

    double position(double t) const noexcept;
    double velocity(double t) const noexcept;
};

TrapezoidProfile makeTrapezoid(double distance, double vMax, double aMax) noexcept;

struct OperationTiming {
    std::string operationId{};
    std::string operationName{};
    double weldTimeSec{0.0};
    double airTimeSec{0.0};
    double totalTimeSec{0.0};
};

struct TrajectoryPlan {
    struct Sample {
        double s{0.0};
        std::array<double, 6> jointsRad{};
    };

    struct Segment {
        std::string operationId{};
        std::string targetId{};
        core::MotionType motion{core::MotionType::J};
        double startTimeSec{0.0};
        double durationSec{0.0};
        bool arcDuring{false};
        bool arcAfter{false};
        TrapezoidProfile profile{};
        std::vector<Sample> samples{};
    };

    double totalCycleTimeSec{0.0};
    double arcWeldTimeSec{0.0};
    double airCutTimeSec{0.0};
    bool hasConfigJumpInLinear{false};
    std::array<double, 6> startJointsRad{};
    core::Transform4d robotBaseInWorld{};
    core::Transform4d utoolOffset{};
    std::vector<OperationTiming> operations{};
    std::vector<Segment> segments{};
    std::vector<ValidationIssue> issues{};
    std::vector<BakedFrame> frames{};
};

// M11: J/L interpolation, trapezoidal timing, cycle estimate, and frame bake.
// Collision sampling stays in CollisionAndSimEngine so M10 sweep checks keep their step size.
class TrajectorySimulator {
public:
    TrajectorySimulator(const OpwKinematics& kinematics, const device::RobotModel& model);

    TrajectoryPlan plan(
        const core::Program& program,
        const core::Transform4d& robotBaseInWorld,
        const core::Transform4d& uframeInWorld,
        const core::Transform4d& utoolOffset,
        double framesPerSec = 30.0) const;

private:
    const OpwKinematics& kinematics_;
    std::array<device::JointLimit, 6> limits_{};
};

enum class PlaybackState {
    Stopped,
    Playing,
    Paused
};

// Plays a plan from Program::StartState. Seeking a target lands on its arrival time.
class PlaybackCursor {
public:
    void bind(const TrajectoryPlan& plan, const OpwKinematics& kinematics);
    void unbind() noexcept;

    void play();
    void playFromStartIfFinished();
    void pause();
    void stop();
    void setSpeedMultiplier(double multiplier);
    void seek(double timeSec);
    bool seekTarget(const std::string& targetId);
    void stepTime(double simDtSec);
    void stepTarget(int direction);
    void advanceWall(double wallDtSec);

    BakedFrame sample() const;
    double timeSec() const noexcept { return timeSec_; }
    double durationSec() const noexcept;
    double speedMultiplier() const noexcept { return speed_; }
    PlaybackState state() const noexcept { return state_; }
    bool isBound() const noexcept { return plan_ != nullptr && kinematics_ != nullptr; }

private:
    const TrajectoryPlan* plan_{nullptr};
    const OpwKinematics* kinematics_{nullptr};
    double timeSec_{0.0};
    double speed_{1.0};
    PlaybackState state_{PlaybackState::Stopped};
};

BakedFrame sampleTrajectory(
    const TrajectoryPlan& plan,
    const OpwKinematics& kinematics,
    double timeSec);

} // namespace olp::algo
