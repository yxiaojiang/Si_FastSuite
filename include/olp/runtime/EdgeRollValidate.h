#pragma once

#include "olp/device/RobotModel.h"
#include "olp/runtime/EdgeRollPath.h"
#include "olp/runtime/HandEyeCalibration.h"
#include "olp/runtime/IRobot.h"

#include <optional>
#include <string>
#include <vector>

namespace olp::runtime {

// Declared roller geometry for Vision_Edge. NEVER alias welding kTorchSpheres.
// VE-D6 does not bind this into FCL; future stages may.
struct RollerToolSpec {
    double radiusMm{10.0};
    double axialHalfHeightMm{12.5};
    bool boundToFcl{false}; // must stay false in VE-D6 unless FCL roller is wired
};

enum class IkReportStatus { Passed, Failed };
enum class FclReportStatus {
    NotBuilt,
    SkippedNoScene,
    SkippedEmptyBoardMesh,
    RanPartial,
    RanFull
};
enum class BoardCollisionStatus {
    NotCheckedEmptyMesh,
    NotCheckedNoFcl,
    Cleared,
    Hit
};
enum class ToolModelStatus {
    UToolOnly,
    RollerSpecUnbound,
    RollerSpecInFcl
};
enum class StepReportStatus { Passed, Failed, Skipped };

struct EdgeRollValidationReport {
    IkReportStatus ik{IkReportStatus::Failed};
    int ikFailCount{0};
    std::string ikFirstMessage{};

    FclReportStatus fclStatus{FclReportStatus::NotBuilt};
    BoardCollisionStatus boardCollision{BoardCollisionStatus::NotCheckedNoFcl};
    ToolModelStatus toolModel{ToolModelStatus::RollerSpecUnbound};

    StepReportStatus lsExport{StepReportStatus::Failed};
    StepReportStatus mockUpload{StepReportStatus::Skipped};
    StepReportStatus mockExecute{StepReportStatus::Failed};

    bool risk001Open{true}; // always true until real ROBOGUIDE/site pairing
    bool usedTorchSpheresAsRoller{false}; // must remain false
    bool claimsBoardCollisionVerified{false}; // must remain false for empty mesh
    bool claimsSiteSafe{false}; // must remain false for Mock/headless

    std::string programName{};
    std::string lsText{};
    std::string notes{};
    RollerToolSpec roller{};
    core::Transform4d utoolUsed{core::Transform4d::identity()};
    bool utoolFromCalibration{false};
    int targetCount{0};
    int ikPassCount{0};
};

struct EdgeRollValidateOptions {
    std::string programName{"EDGE_ROLL"}; // FANUC rules: A-Z start, <=16
    std::string exportLsPathUtf8{};       // optional write path
    bool runMock{true};
    bool requireIkPass{false}; // if true, DeviceResult fails when ik Failed
    // VE-D7 one-click gate (closes VE-D6-001): when true, upload/run/executeTrajectory
    // run only if ik==Passed AND lsExport==Passed. Otherwise both Mock steps Skipped.
    bool gateMockOnIkAndLsPass{false};
    RollerToolSpec roller{};
};

// Thin orchestration: Targets -> OPW IK -> honest FCL status -> Fanuc .ls -> Mock.
// Does not rewrite CollisionAndSim / UI / ProjectModel.
DeviceResult<EdgeRollValidationReport> validateExportAndMockEdgeRoll(
    const EdgeRollPath& path,
    const device::RobotModel& robotModel,
    IRobot& mockRobot,
    const std::optional<HandEyeCalibration>& calib = std::nullopt,
    const EdgeRollValidateOptions& options = {},
    bool transientBoardMeshEmpty = true);

const char* toString(IkReportStatus s) noexcept;
const char* toString(FclReportStatus s) noexcept;
const char* toString(BoardCollisionStatus s) noexcept;
const char* toString(ToolModelStatus s) noexcept;
const char* toString(StepReportStatus s) noexcept;

} // namespace olp::runtime
