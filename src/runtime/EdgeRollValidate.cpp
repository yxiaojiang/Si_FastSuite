#include "olp/runtime/EdgeRollValidate.h"

#include "olp/algo/OpwKinematics.h"
#include "olp/io/FanucLsPostProcessor.h"
#include "olp/io/Utf8Path.h"

#include <fstream>
#include <sstream>
#include <utility>

namespace olp::runtime {
namespace {

core::Transform4d resolveUtool(
    const std::optional<HandEyeCalibration>& calib,
    bool& fromCalib,
    std::string& note) {
    fromCalib = false;
    if (calib.has_value() && calib->T_flange_tcp.has_value()) {
        fromCalib = true;
        note += "UTOOL from HandEyeCalibration.T_flange_tcp. ";
        return *calib->T_flange_tcp;
    }
    // Demo placeholder: 150 mm along tool +Z (world -Z when path attitude applies).
    core::Transform4d T = core::Transform4d::fromTranslation(0.0, 0.0, 150.0);
    note += "UTOOL placeholder translation (0,0,150) mm; not field-calibrated. ";
    return T;
}

void fillHonestFclFields(
    EdgeRollValidationReport& report,
    bool transientBoardMeshEmpty) {
#if defined(OLP_HAS_FCL)
    report.fclStatus = FclReportStatus::SkippedNoScene;
    if (transientBoardMeshEmpty) {
        report.boardCollision = BoardCollisionStatus::NotCheckedEmptyMesh;
        report.notes +=
            "FCL is built but Vision transient board mesh is empty; "
            "boardCollision=NotCheckedEmptyMesh (NOT Cleared). ";
    } else {
        // Still no scene wiring in VE-D6 thin wrapper.
        report.boardCollision = BoardCollisionStatus::NotCheckedEmptyMesh;
        report.notes +=
            "FCL built but VE-D6 does not run board FCL; treat as not checked. ";
    }
#else
    (void)transientBoardMeshEmpty;
    report.fclStatus = FclReportStatus::NotBuilt;
    report.boardCollision = BoardCollisionStatus::NotCheckedNoFcl;
    report.notes +=
        "OLP_HAS_FCL undefined: fclStatus=NotBuilt; boardCollision=NotCheckedNoFcl. ";
#endif
    // Never bind welding torch spheres as the roller model on this path.
    report.usedTorchSpheresAsRoller = false;
    report.toolModel = report.roller.boundToFcl ? ToolModelStatus::RollerSpecInFcl
                                                : ToolModelStatus::RollerSpecUnbound;
    if (!report.roller.boundToFcl) {
        report.notes +=
            "RollerToolSpec declared but not bound to FCL "
            "(toolModel=RollerSpecUnbound); welding kTorchSpheres NOT used. ";
    }
    report.claimsBoardCollisionVerified = false;
    report.claimsSiteSafe = false;
    report.risk001Open = true;
    report.notes +=
        "Mock/headless success is NOT site acceptance; RISK-001 remains OPEN. ";
}

} // namespace

const char* toString(IkReportStatus s) noexcept {
    switch (s) {
    case IkReportStatus::Passed: return "Passed";
    case IkReportStatus::Failed: return "Failed";
    }
    return "Unknown";
}

const char* toString(FclReportStatus s) noexcept {
    switch (s) {
    case FclReportStatus::NotBuilt: return "NotBuilt";
    case FclReportStatus::SkippedNoScene: return "SkippedNoScene";
    case FclReportStatus::SkippedEmptyBoardMesh: return "SkippedEmptyBoardMesh";
    case FclReportStatus::RanPartial: return "RanPartial";
    case FclReportStatus::RanFull: return "RanFull";
    }
    return "Unknown";
}

const char* toString(BoardCollisionStatus s) noexcept {
    switch (s) {
    case BoardCollisionStatus::NotCheckedEmptyMesh: return "NotCheckedEmptyMesh";
    case BoardCollisionStatus::NotCheckedNoFcl: return "NotCheckedNoFcl";
    case BoardCollisionStatus::Cleared: return "Cleared";
    case BoardCollisionStatus::Hit: return "Hit";
    }
    return "Unknown";
}

const char* toString(ToolModelStatus s) noexcept {
    switch (s) {
    case ToolModelStatus::UToolOnly: return "UToolOnly";
    case ToolModelStatus::RollerSpecUnbound: return "RollerSpecUnbound";
    case ToolModelStatus::RollerSpecInFcl: return "RollerSpecInFcl";
    }
    return "Unknown";
}

const char* toString(StepReportStatus s) noexcept {
    switch (s) {
    case StepReportStatus::Passed: return "Passed";
    case StepReportStatus::Failed: return "Failed";
    case StepReportStatus::Skipped: return "Skipped";
    }
    return "Unknown";
}

DeviceResult<EdgeRollValidationReport> validateExportAndMockEdgeRoll(
    const EdgeRollPath& path,
    const device::RobotModel& robotModel,
    IRobot& mockRobot,
    const std::optional<HandEyeCalibration>& calib,
    const EdgeRollValidateOptions& options,
    bool transientBoardMeshEmpty) {
    EdgeRollValidationReport report;
    report.roller = options.roller;
    report.roller.boundToFcl = false; // VE-D6 hard rule: do not pretend FCL roller binding
    report.programName = options.programName;

    if (path.waypoints.empty()) {
        return DeviceResult<EdgeRollValidationReport>::failure(
            DeviceError::InvalidArgument,
            "validateExportAndMockEdgeRoll: empty EdgeRollPath");
    }

    fillHonestFclFields(report, transientBoardMeshEmpty);

    std::string utoolNote;
    report.utoolUsed = resolveUtool(calib, report.utoolFromCalibration, utoolNote);
    report.notes += utoolNote;

    // 1) Targets
    auto targets = fillTargetsFromEdgeRollPath(path);
    report.targetCount = static_cast<int>(targets.size());
    if (targets.empty()) {
        return DeviceResult<EdgeRollValidationReport>::failure(
            DeviceError::InvalidArgument,
            "validateExportAndMockEdgeRoll: no targets from path");
    }

    // 2) IK (OPW) -- fill Target.cache for .ls gate
    algo::OpwKinematics kin(robotModel);
    std::array<double, 6> prev = robotModel.homeJointsRad;
    report.ikFailCount = 0;
    report.ikPassCount = 0;
    for (auto& t : targets) {
        const auto sols = kin.inverseKinematics(t.poseInUFrame, report.utoolUsed, false);
        const auto best = kin.selectBestSolution(sols, prev, std::nullopt, false);
        if (!best.has_value() || sols.empty()) {
            ++report.ikFailCount;
            t.cache.valid = false;
            t.cache.status = core::TargetValidationState::Unreachable;
            if (report.ikFirstMessage.empty()) {
                report.ikFirstMessage =
                    "IK failed for target '" + t.id + "' / '" + t.name + "'";
            }
            continue;
        }
        if (!best->withinLimits) {
            ++report.ikFailCount;
            t.cache.valid = false;
            t.cache.status = core::TargetValidationState::JointLimitExceeded;
            t.cache.jointsRad = best->jointsRad;
            t.cache.solvedConfig = best->config;
            if (report.ikFirstMessage.empty()) {
                report.ikFirstMessage =
                    "IK joint-limit failure for target '" + t.id + "'";
            }
            continue;
        }
        t.cache.valid = true;
        t.cache.status = core::TargetValidationState::Valid;
        t.cache.jointsRad = best->jointsRad;
        t.cache.solvedConfig = best->config;
        prev = best->jointsRad;
        ++report.ikPassCount;
    }
    report.ik = (report.ikFailCount == 0 && report.ikPassCount == report.targetCount)
                    ? IkReportStatus::Passed
                    : IkReportStatus::Failed;
    if (report.ik == IkReportStatus::Failed && report.ikFirstMessage.empty()) {
        report.ikFirstMessage = "One or more IK targets failed";
    }

    // 3) Build Program + Fanuc .ls (requires valid IK caches)
    core::Program program;
    program.id = "vision_edge_roll";
    program.name = options.programName;
    program.comment = "Vision Edge demo";
    program.uframeNumber = 1;
    program.utoolNumber = 1;
    program.startState.homeJointsRad = robotModel.homeJointsRad;

    core::Operation op;
    op.id = "op_edge_roll";
    op.name = "EdgeRoll";
    op.enabled = true;
    op.targets = std::move(targets);
    program.operations.push_back(std::move(op));

    io::LsPostConfig lsCfg;
    lsCfg.includeArcWeldingAppl = false; // coating demo, not arc weld
    lsCfg.appendReturnHome = true;
    lsCfg.emitHomeAsJointTarget = false;
    lsCfg.blockOnCollision = true;
    lsCfg.blockOnConfigJump = true;
    lsCfg.blockOnUnreachable = true;

    std::string lsError;
    const std::string lsBody =
        io::FanucLsPostProcessor::generateLsString(program, lsCfg, &lsError);
    if (lsError.empty() && !lsBody.empty()) {
        report.lsExport = StepReportStatus::Passed;
        report.lsText = lsBody;
        if (!options.exportLsPathUtf8.empty()) {
            std::string writeErr;
            if (!io::FanucLsPostProcessor::exportToFile(
                    program, options.exportLsPathUtf8, lsCfg, &writeErr)) {
                report.lsExport = StepReportStatus::Failed;
                report.notes += "LS file write failed: " + writeErr + " ";
            }
        }
    } else {
        report.lsExport = StepReportStatus::Failed;
        report.notes += "LS export failed: " +
                        (lsError.empty() ? std::string("empty body") : lsError) + " ";
    }

    // 4) Mock robot
    if (!options.runMock) {
        report.mockUpload = StepReportStatus::Skipped;
        report.mockExecute = StepReportStatus::Skipped;
    } else {
        const bool gateBlocks =
            options.gateMockOnIkAndLsPass &&
            (report.ik != IkReportStatus::Passed ||
             report.lsExport != StepReportStatus::Passed);
        if (gateBlocks) {
            report.mockUpload = StepReportStatus::Skipped;
            report.mockExecute = StepReportStatus::Skipped;
            report.notes +=
                "Mock upload/execute gated off: ik and lsExport must both Pass "
                "(VE-D7 / VE-D6-001 one-click hard gate). ";
        } else {
            if (!mockRobot.isConnected()) {
                const auto st = mockRobot.connect();
                if (!st.ok()) {
                    report.mockUpload = StepReportStatus::Failed;
                    report.mockExecute = StepReportStatus::Failed;
                    report.notes += "Mock connect failed: " + st.message + " ";
                }
            }

            if (report.lsExport == StepReportStatus::Passed && mockRobot.isConnected()) {
                RobotProgram prog;
                prog.name = options.programName;
                prog.content = report.lsText;
                const auto up = mockRobot.uploadProgram(prog);
                if (up.ok()) {
                    const auto run = mockRobot.runProgram(options.programName);
                    report.mockUpload =
                        (up.ok() && run.ok()) ? StepReportStatus::Passed
                                              : StepReportStatus::Failed;
                    if (!run.ok()) {
                        report.notes += "Mock runProgram failed: " + run.message + " ";
                    }
                } else {
                    report.mockUpload = StepReportStatus::Failed;
                    report.notes += "Mock uploadProgram failed: " + up.message + " ";
                }
            } else if (options.runMock) {
                report.mockUpload = StepReportStatus::Skipped;
                report.notes += "Mock upload skipped because LS export failed. ";
            }

            if (mockRobot.isConnected()) {
                const auto carts = edgeRollPathToCartesian(path);
                const auto ex = mockRobot.executeTrajectory(carts);
                report.mockExecute =
                    ex.ok() ? StepReportStatus::Passed : StepReportStatus::Failed;
                if (!ex.ok()) {
                    report.notes +=
                        "Mock executeTrajectory failed: " + ex.message + " ";
                }
            }
        }
    }

    if (options.requireIkPass && report.ik != IkReportStatus::Passed) {
        return DeviceResult<EdgeRollValidationReport>::failure(
            DeviceError::InternalError,
            "IK required but failed: " + report.ikFirstMessage);
    }

    return DeviceResult<EdgeRollValidationReport>::success(std::move(report));
}

} // namespace olp::runtime
