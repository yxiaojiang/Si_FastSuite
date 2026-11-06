#pragma once

#include "olp/device/RobotModel.h"
#include "olp/runtime/EdgeRollPath.h"
#include "olp/runtime/EdgeRollValidate.h"
#include "olp/runtime/HandEyeCalibration.h"
#include "olp/runtime/ICamera.h"
#include "olp/runtime/IRobot.h"

#include <string>

namespace olp::runtime {

// Minimal one-click demo state machine (VE-D7). Software Mock only.
enum class VisionEdgeDemoState {
    Idle,
    Capture,
    Perceive,
    Fit,
    Plan,
    Validate,
    Execute,
    Done,
    Fault
};

const char* toString(VisionEdgeDemoState s) noexcept;

struct VisionEdgeDemoDeps {
    ICamera& camera;
    IRobot& robot;
    const HandEyeCalibration& calib;
    const device::RobotModel& robotModel;
    EdgeRollParams pathParams{};
    RollerToolSpec roller{};
};

struct VisionEdgeDemoRunReport {
    std::string placementId{};
    VisionEdgeDemoState stateReached{VisionEdgeDemoState::Idle};
    std::string faultReason{};
    EdgeRollValidationReport validation{};
    bool hasValidation{false};
    bool executedMock{false};     // true only if Mock upload/execute actually ran
    bool mockDemoPassed{false};   // Done with executedMock; NEVER site-safe
    // claimsSiteSafe is intentionally absent / always treated false
};

struct VisionEdgeDemoABReport {
    VisionEdgeDemoRunReport a{};
    VisionEdgeDemoRunReport b{};
    bool bothDone{false};
};

// Single placement: Idle -> ... -> Done|Fault. Same logic for any placementId.
DeviceResult<VisionEdgeDemoRunReport> runVisionEdgeDemoOnce(
    const std::string& placementId,
    VisionEdgeDemoDeps& deps);

// Same deps/params/calib/model/state machine; only placement A then B.
DeviceResult<VisionEdgeDemoABReport> runVisionEdgeDemoAB(VisionEdgeDemoDeps& deps);

} // namespace olp::runtime
