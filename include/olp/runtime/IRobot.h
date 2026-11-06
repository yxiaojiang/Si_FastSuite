#pragma once

#include "olp/runtime/DeviceTypes.h"

#include <string>
#include <vector>

namespace olp::runtime {

// Hardware-agnostic robot controller boundary for Vision_Edge.
// Demo path uploads / runs a program or feeds a Cartesian trajectory.
// Real brand drivers plug in behind this interface later; VE-D1 ships Mock only.
class IRobot {
public:
    virtual ~IRobot() = default;

    virtual DeviceStatus connect() = 0;
    virtual DeviceStatus disconnect() = 0;
    [[nodiscard]] virtual bool isConnected() const noexcept = 0;

    virtual DeviceStatus uploadProgram(const RobotProgram& program) = 0;
    virtual DeviceStatus runProgram(const std::string& programName) = 0;
    virtual DeviceStatus executeTrajectory(
        const std::vector<CartesianWaypointMm>& waypoints) = 0;

    virtual DeviceResult<RobotState> queryState() = 0;
};

} // namespace olp::runtime
