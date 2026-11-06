#include "olp/runtime/MockRobot.h"

namespace olp::runtime {

DeviceStatus MockRobot::connect() {
    connected_ = true;
    programRunning_ = false;
    return DeviceStatus::success("MockRobot connected");
}

DeviceStatus MockRobot::disconnect() {
    connected_ = false;
    programRunning_ = false;
    return DeviceStatus::success("MockRobot disconnected");
}

bool MockRobot::isConnected() const noexcept {
    return connected_;
}

DeviceStatus MockRobot::uploadProgram(const RobotProgram& program) {
    if (!connected_) {
        return DeviceStatus::failure(
            DeviceError::NotConnected,
            "MockRobot is not connected");
    }
    if (program.name.empty()) {
        return DeviceStatus::failure(
            DeviceError::InvalidArgument,
            "Program name must not be empty");
    }
    uploadedPrograms_.push_back(program);
    lastProgramName_ = program.name;
    return DeviceStatus::success("Uploaded program " + program.name);
}

DeviceStatus MockRobot::runProgram(const std::string& programName) {
    if (!connected_) {
        return DeviceStatus::failure(
            DeviceError::NotConnected,
            "MockRobot is not connected");
    }
    if (programName.empty()) {
        return DeviceStatus::failure(
            DeviceError::InvalidArgument,
            "Program name must not be empty");
    }

    bool known = false;
    for (const auto& prog : uploadedPrograms_) {
        if (prog.name == programName) {
            known = true;
            break;
        }
    }
    if (!known) {
        return DeviceStatus::failure(
            DeviceError::NotFound,
            "Program not uploaded: " + programName);
    }

    runHistory_.push_back(programName);
    lastProgramName_ = programName;
    programRunning_ = false; // Instant success; no async controller.
    return DeviceStatus::success("Ran program " + programName);
}

DeviceStatus MockRobot::executeTrajectory(
    const std::vector<CartesianWaypointMm>& waypoints) {
    if (!connected_) {
        return DeviceStatus::failure(
            DeviceError::NotConnected,
            "MockRobot is not connected");
    }
    if (waypoints.empty()) {
        return DeviceStatus::failure(
            DeviceError::InvalidArgument,
            "Trajectory must contain at least one waypoint");
    }
    executedTrajectories_.push_back(waypoints);
    programRunning_ = false;
    return DeviceStatus::success(
        "Executed trajectory with " + std::to_string(waypoints.size()) +
        " waypoints");
}

DeviceResult<RobotState> MockRobot::queryState() {
    RobotState state;
    state.connected = connected_;
    state.programRunning = programRunning_;
    state.lastProgramName = lastProgramName_;
    state.statusText = connected_ ? "ready" : "disconnected";
    if (!connected_) {
        return DeviceResult<RobotState>::failure(
            DeviceError::NotConnected,
            "MockRobot is not connected");
    }
    return DeviceResult<RobotState>::success(std::move(state));
}

} // namespace olp::runtime
