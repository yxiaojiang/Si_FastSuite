#pragma once

#include "olp/runtime/IRobot.h"

#include <string>
#include <vector>

namespace olp::runtime {

// In-memory robot Mock: records uploaded programs and trajectories,
// returns success when connected. No network / controller SDK.
class MockRobot final : public IRobot {
public:
    MockRobot() = default;

    DeviceStatus connect() override;
    DeviceStatus disconnect() override;
    [[nodiscard]] bool isConnected() const noexcept override;

    DeviceStatus uploadProgram(const RobotProgram& program) override;
    DeviceStatus runProgram(const std::string& programName) override;
    DeviceStatus executeTrajectory(
        const std::vector<CartesianWaypointMm>& waypoints) override;
    DeviceResult<RobotState> queryState() override;

    [[nodiscard]] const std::vector<RobotProgram>& uploadedPrograms() const noexcept {
        return uploadedPrograms_;
    }
    [[nodiscard]] const std::vector<std::string>& runHistory() const noexcept {
        return runHistory_;
    }
    [[nodiscard]] const std::vector<std::vector<CartesianWaypointMm>>&
    executedTrajectories() const noexcept {
        return executedTrajectories_;
    }

private:
    bool connected_{false};
    bool programRunning_{false};
    std::string lastProgramName_{};
    std::vector<RobotProgram> uploadedPrograms_{};
    std::vector<std::string> runHistory_{};
    std::vector<std::vector<CartesianWaypointMm>> executedTrajectories_{};
};

} // namespace olp::runtime
