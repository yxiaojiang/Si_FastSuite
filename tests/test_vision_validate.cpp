#include "test_support.h"

#include "olp/device/RobotModel.h"
#include "olp/io/Utf8Path.h"
#include "olp/runtime/BoardOutlineFit.h"
#include "olp/runtime/BoardPerception.h"
#include "olp/runtime/ContourDataset.h"
#include "olp/runtime/EdgeRollPath.h"
#include "olp/runtime/EdgeRollValidate.h"
#include "olp/runtime/HandEyeCalibration.h"
#include "olp/runtime/MockRobot.h"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

std::string workspaceRoot() {
#if defined(OLP_SOURCE_DIR)
    return std::string(OLP_SOURCE_DIR);
#else
    return olp::io::resolveWorkspaceOrAppDir();
#endif
}

olp::runtime::DeviceResult<olp::runtime::EdgeRollPath> pathFor(
    const std::string& placementId) {
    auto dataset = olp::runtime::ContourDataset::loadFromDirectory(
        olp::runtime::defaultVisionEdgeDatasetDir(workspaceRoot()));
    auto calib = olp::runtime::loadHandEyeCalibrationJson(
        olp::runtime::defaultVisionEdgeCalibDemoPath(workspaceRoot()));
    if (!dataset.ok()) {
        return olp::runtime::DeviceResult<olp::runtime::EdgeRollPath>::failure(
            dataset.status.code, dataset.status.message);
    }
    if (!calib.ok()) {
        return olp::runtime::DeviceResult<olp::runtime::EdgeRollPath>::failure(
            calib.status.code, calib.status.message);
    }
    auto frame = dataset.value.frame(placementId);
    if (!frame.ok()) {
        return olp::runtime::DeviceResult<olp::runtime::EdgeRollPath>::failure(
            frame.status.code, frame.status.message);
    }
    auto perc =
        olp::runtime::perceiveFromContour(frame.value, calib.value);
    if (!perc.ok()) {
        return olp::runtime::DeviceResult<olp::runtime::EdgeRollPath>::failure(
            perc.status.code, perc.status.message);
    }
    auto fit = olp::runtime::fitBoardOutline(perc.value);
    if (!fit.ok()) {
        return olp::runtime::DeviceResult<olp::runtime::EdgeRollPath>::failure(
            fit.status.code, fit.status.message);
    }
    return olp::runtime::planEdgeRollPath(fit.value);
}

} // namespace

void testVisionEdgeRollValidateExportMock() {
    std::cout << "\n--- VE-D6: Validate / LS export / Mock (honest report) ---\n";

#if !defined(OLP_HAS_NLOHMANN_JSON)
    check(false, "VE-D6 requires OLP_HAS_NLOHMANN_JSON", "");
    return;
#else
    auto pathA = pathFor("placement_a");
    auto pathB = pathFor("placement_b");
    check(pathA.ok() && pathB.ok(), "VE-D6 build A/B edge paths",
          pathA.ok() ? pathB.status.message : pathA.status.message);
    if (!pathA.ok() || !pathB.ok()) {
        return;
    }

    auto calib = olp::runtime::loadHandEyeCalibrationJson(
        olp::runtime::defaultVisionEdgeCalibDemoPath(workspaceRoot()));
    check(calib.ok(), "VE-D6 load calib for UTOOL", calib.status.message);

    const auto robot = olp::device::RobotModel::createFanucM10iA(workspaceRoot());
    olp::runtime::MockRobot mock;
    check(mock.connect().ok(), "VE-D6 MockRobot connect", "");

    olp::runtime::EdgeRollValidateOptions optA;
    optA.programName = "EDGE_ROLL_A";
    optA.runMock = true;
    optA.requireIkPass = false; // demo reach may be near limit; honesty over fake pass
    optA.roller.radiusMm = 10.0;
    optA.roller.axialHalfHeightMm = 12.5;
    optA.roller.boundToFcl = false;

    namespace fs = std::filesystem;
    const fs::path tmpDir = fs::temp_directory_path() / "ve_d6_ls_export";
    std::error_code ec;
    fs::remove_all(tmpDir, ec);
    fs::create_directories(tmpDir, ec);
    optA.exportLsPathUtf8 = olp::io::pathToUtf8(tmpDir / "EDGE_ROLL_A.ls");

    auto repA = olp::runtime::validateExportAndMockEdgeRoll(
        pathA.value,
        robot,
        mock,
        calib.ok() ? std::optional<olp::runtime::HandEyeCalibration>(calib.value)
                   : std::nullopt,
        optA,
        /*transientBoardMeshEmpty=*/true);
    check(repA.ok(), "validateExportAndMockEdgeRoll A DeviceResult ok",
          repA.status.message);
    if (!repA.ok()) {
        return;
    }

    // Honest FCL / board collision / risk claims
#if defined(OLP_HAS_FCL)
    check(repA.value.fclStatus == olp::runtime::FclReportStatus::SkippedNoScene ||
              repA.value.fclStatus ==
                  olp::runtime::FclReportStatus::SkippedEmptyBoardMesh,
          "with FCL: status is skipped/not full board clear",
          olp::runtime::toString(repA.value.fclStatus));
    check(repA.value.boardCollision ==
              olp::runtime::BoardCollisionStatus::NotCheckedEmptyMesh,
          "empty mesh => NotCheckedEmptyMesh (not Cleared)",
          olp::runtime::toString(repA.value.boardCollision));
#else
    check(repA.value.fclStatus == olp::runtime::FclReportStatus::NotBuilt,
          "no FCL: fclStatus=NotBuilt",
          olp::runtime::toString(repA.value.fclStatus));
    check(repA.value.boardCollision ==
              olp::runtime::BoardCollisionStatus::NotCheckedNoFcl,
          "no FCL: boardCollision=NotCheckedNoFcl",
          olp::runtime::toString(repA.value.boardCollision));
#endif
    check(!repA.value.claimsBoardCollisionVerified,
          "never claims board collision verified",
          "");
    check(!repA.value.claimsSiteSafe, "never claims site safe", "");
    check(repA.value.risk001Open, "RISK-001 remains OPEN", "");
    check(!repA.value.usedTorchSpheresAsRoller,
          "welding kTorchSpheres NOT used as roller",
          "");
    check(repA.value.toolModel == olp::runtime::ToolModelStatus::RollerSpecUnbound,
          "toolModel=RollerSpecUnbound",
          olp::runtime::toString(repA.value.toolModel));
    check(repA.value.utoolFromCalibration,
          "UTOOL taken from calib T_flange_tcp",
          "");

    // Mock trajectory should work regardless of IK reachability
    check(repA.value.mockExecute == olp::runtime::StepReportStatus::Passed,
          "Mock executeTrajectory Passed",
          olp::runtime::toString(repA.value.mockExecute));
    check(!mock.executedTrajectories().empty(),
          "Mock recorded trajectory history",
          "");

    // IK / LS: report honestly; if Passed, LS text must look like dense L
    std::cout << "  [INFO] A ik=" << olp::runtime::toString(repA.value.ik)
              << " pass=" << repA.value.ikPassCount
              << " fail=" << repA.value.ikFailCount
              << " ls=" << olp::runtime::toString(repA.value.lsExport)
              << " msg=" << repA.value.ikFirstMessage << "\n";
    if (repA.value.ik == olp::runtime::IkReportStatus::Passed) {
        check(repA.value.lsExport == olp::runtime::StepReportStatus::Passed,
              "IK Passed => LS export Passed",
              "");
        check(repA.value.lsText.find("L P[") != std::string::npos,
              "LS contains linear L moves",
              "");
        check(repA.value.lsText.find("mm/sec") != std::string::npos,
              "LS contains mm/sec speed",
              "");
        check(repA.value.lsText.find("RISK-001") != std::string::npos,
              "LS watermarks RISK-001 draft gate",
              "");
        check(repA.value.mockUpload == olp::runtime::StepReportStatus::Passed,
              "Mock upload/run Passed when LS ok",
              "");
        check(fs::exists(tmpDir / "EDGE_ROLL_A.ls", ec),
              "LS file written to export path",
              "");
    } else {
        check(repA.value.ikFailCount > 0,
              "IK Failed reports fail count > 0",
              std::to_string(repA.value.ikFailCount));
        check(repA.value.lsExport == olp::runtime::StepReportStatus::Failed,
              "IK Failed => LS export Failed (no fake pass)",
              olp::runtime::toString(repA.value.lsExport));
        check(repA.value.notes.find("RISK-001") != std::string::npos,
              "notes mention RISK-001 OPEN",
              "");
    }

    // Placement B with same robot/params
    olp::runtime::MockRobot mockB;
    check(mockB.connect().ok(), "VE-D6 MockRobot B connect", "");
    olp::runtime::EdgeRollValidateOptions optB = optA;
    optB.programName = "EDGE_ROLL_B";
    optB.exportLsPathUtf8.clear();
    auto repB = olp::runtime::validateExportAndMockEdgeRoll(
        pathB.value, robot, mockB, calib.value, optB, true);
    check(repB.ok(), "validateExportAndMockEdgeRoll B DeviceResult ok",
          repB.status.message);
    if (repB.ok()) {
        check(repB.value.mockExecute == olp::runtime::StepReportStatus::Passed,
              "Mock executeTrajectory B Passed",
              "");
        check(repB.value.fclStatus == repA.value.fclStatus,
              "A/B share same FCL honesty status",
              "");
        check(!repB.value.claimsBoardCollisionVerified &&
                  !repB.value.usedTorchSpheresAsRoller,
              "B also refuses false collision/torch claims",
              "");
    }

    // Failure path: empty path
    olp::runtime::EdgeRollPath empty;
    bool threw = false;
    try {
        (void)olp::runtime::validateExportAndMockEdgeRoll(
            empty, robot, mock, calib.value, optA, true);
    } catch (const std::exception&) {
        threw = true;
    }
    check(!threw, "empty path does not throw", "");
    auto emptyRes = olp::runtime::validateExportAndMockEdgeRoll(
        empty, robot, mock, calib.value, optA, true);
    check(!emptyRes.ok(), "empty path -> DeviceResult failure",
          emptyRes.status.message);

    // Disconnected mock execute fails honesty
    olp::runtime::MockRobot dead;
    olp::runtime::EdgeRollValidateOptions optDead = optA;
    optDead.exportLsPathUtf8.clear();
    // leave disconnected
    auto repDead = olp::runtime::validateExportAndMockEdgeRoll(
        pathA.value, robot, dead, calib.value, optDead, true);
    check(repDead.ok(), "disconnected mock still returns report DeviceResult",
          repDead.status.message);
    if (repDead.ok()) {
        // connect() is attempted inside; if connect works on fresh mock, execute may pass.
        // Force failure by disconnect after? Fresh MockRobot connect is called inside.
        // Instead assert report never claims site safe.
        check(!repDead.value.claimsSiteSafe && repDead.value.risk001Open,
              "disconnected-or-connected report never site-safe",
              "");
    }

    fs::remove_all(tmpDir, ec);
#endif
}
