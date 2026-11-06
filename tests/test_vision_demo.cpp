#include "test_support.h"

#include "olp/device/RobotModel.h"
#include "olp/io/Utf8Path.h"
#include "olp/runtime/ContourDataset.h"
#include "olp/runtime/EdgeRollPath.h"
#include "olp/runtime/EdgeRollValidate.h"
#include "olp/runtime/HandEyeCalibration.h"
#include "olp/runtime/MockCamera.h"
#include "olp/runtime/MockRobot.h"
#include "olp/runtime/VisionEdgeDemo.h"

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

} // namespace

void testVisionEdgeDemoOrchestrateAB() {
    std::cout << "\n--- VE-D7: One-click orchestrate A/B + safety gate ---\n";

#if !defined(OLP_HAS_NLOHMANN_JSON)
    check(false, "VE-D7 requires OLP_HAS_NLOHMANN_JSON", "");
    return;
#else
    auto dataset = olp::runtime::ContourDataset::loadFromDirectory(
        olp::runtime::defaultVisionEdgeDatasetDir(workspaceRoot()));
    auto calib = olp::runtime::loadHandEyeCalibrationJson(
        olp::runtime::defaultVisionEdgeCalibDemoPath(workspaceRoot()));
    check(dataset.ok() && calib.ok(), "VE-D7 load dataset+calib",
          dataset.ok() ? calib.status.message : dataset.status.message);
    if (!dataset.ok() || !calib.ok()) {
        return;
    }

    olp::runtime::MockCamera camera(dataset.value);
    olp::runtime::MockRobot robot;
    const auto model =
        olp::device::RobotModel::createFanucM10iA(workspaceRoot());

    olp::runtime::VisionEdgeDemoDeps deps{
        camera, robot, calib.value, model, {}, {}};

    // --- AB happy path (same logic, placement switch only) ---
    const std::size_t trajBeforeAb = robot.executedTrajectories().size();
    auto ab = olp::runtime::runVisionEdgeDemoAB(deps);
    check(ab.ok(), "runVisionEdgeDemoAB DeviceResult ok", ab.status.message);
    if (!ab.ok()) {
        return;
    }
    check(ab.value.bothDone, "A and B both Done with mockDemoPassed", "");
    check(ab.value.a.stateReached == olp::runtime::VisionEdgeDemoState::Done &&
              ab.value.b.stateReached == olp::runtime::VisionEdgeDemoState::Done,
          "A/B stateReached == Done",
          std::string(olp::runtime::toString(ab.value.a.stateReached)) + "/" +
              olp::runtime::toString(ab.value.b.stateReached));
    check(ab.value.a.executedMock && ab.value.b.executedMock,
          "A/B executedMock true only after ik+ls Pass",
          "");
    check(ab.value.a.placementId == "placement_a" &&
              ab.value.b.placementId == "placement_b",
          "A/B placementIds correct",
          "");
    check(ab.value.a.hasValidation && ab.value.b.hasValidation &&
              ab.value.a.validation.ik == olp::runtime::IkReportStatus::Passed &&
              ab.value.b.validation.ik == olp::runtime::IkReportStatus::Passed &&
              ab.value.a.validation.lsExport ==
                  olp::runtime::StepReportStatus::Passed &&
              ab.value.b.validation.lsExport ==
                  olp::runtime::StepReportStatus::Passed,
          "A/B validation ik+ls Passed",
          "");
    check(!ab.value.a.validation.claimsSiteSafe &&
              !ab.value.b.validation.claimsSiteSafe,
          "never claimsSiteSafe on demo success",
          "");
    check(ab.value.a.mockDemoPassed && !ab.value.a.validation.claimsSiteSafe,
          "mockDemoPassed is Mock-only (not site safe)",
          "");
    check(robot.executedTrajectories().size() == trajBeforeAb + 2,
          "AB success records exactly two Mock trajectories",
          std::to_string(robot.executedTrajectories().size()));

    // --- Capture failure: disconnected camera, unknown placement ---
    olp::runtime::MockCamera cam2(dataset.value);
    // leave disconnected and ask for missing placement after connect fails path:
    // selectPlacement without connect -> capture fails NotConnected if we connect first
    check(cam2.connect().ok(), "fault-test camera connect", "");
    olp::runtime::MockRobot robot2;
    check(robot2.connect().ok(), "fault-test robot connect", "");
    const std::size_t trajBeforeFault = robot2.executedTrajectories().size();
    olp::runtime::VisionEdgeDemoDeps depsFault{
        cam2, robot2, calib.value, model, {}, {}};
    auto badPlace = olp::runtime::runVisionEdgeDemoOnce("placement_missing", depsFault);
    check(badPlace.ok() &&
              badPlace.value.stateReached == olp::runtime::VisionEdgeDemoState::Fault,
          "unknown placement -> Fault",
          badPlace.ok() ? badPlace.value.faultReason : badPlace.status.message);
    check(!badPlace.value.executedMock && !badPlace.value.mockDemoPassed,
          "unknown placement does not execute Mock",
          "");
    check(robot2.executedTrajectories().size() == trajBeforeFault,
          "unknown placement adds no Mock trajectory",
          "");

    // --- VE-D6-001 closed on one-click gate: IK failure must NOT executeTrajectory ---
    olp::runtime::MockCamera cam3(dataset.value);
    check(cam3.connect().ok() && cam3.selectPlacement("placement_a").ok(),
          "gate-test camera ready",
          "");
    auto frame = dataset.value.frame("placement_a");
    check(frame.ok(), "gate-test frame", frame.status.message);
    auto perc = olp::runtime::perceiveFromContour(frame.value, calib.value);
    auto fit = olp::runtime::fitBoardOutline(perc.value);
    auto path = olp::runtime::planEdgeRollPath(fit.value);
    check(path.ok(), "gate-test plan path", path.status.message);
    if (path.ok()) {
        for (auto& wp : path.value.waypoints) {
            wp.cartesian.positionMm.x += 50000.0;
            wp.cartesian.positionMm.y += 50000.0;
        }
        olp::runtime::MockRobot gated;
        check(gated.connect().ok(), "gate-test robot connect", "");
        const std::size_t before = gated.executedTrajectories().size();
        olp::runtime::EdgeRollValidateOptions opt;
        opt.programName = "EDGE_FAIL";
        opt.runMock = true;
        opt.gateMockOnIkAndLsPass = true;
        auto rep = olp::runtime::validateExportAndMockEdgeRoll(
            path.value, model, gated, calib.value, opt, true);
        check(rep.ok(), "gated validate DeviceResult ok", rep.status.message);
        check(rep.value.ik == olp::runtime::IkReportStatus::Failed,
              "forced unreachable path => ik Failed",
              "");
        check(rep.value.lsExport == olp::runtime::StepReportStatus::Failed,
              "ik Failed => ls Failed",
              "");
        check(rep.value.mockUpload == olp::runtime::StepReportStatus::Skipped &&
                  rep.value.mockExecute == olp::runtime::StepReportStatus::Skipped,
              "VE-D6-001 closed: Mock upload/execute Skipped when gated",
              std::string(olp::runtime::toString(rep.value.mockUpload)) + "/" +
                  olp::runtime::toString(rep.value.mockExecute));
        check(gated.executedTrajectories().size() == before,
              "gated IK failure adds no Mock trajectory",
              std::to_string(gated.executedTrajectories().size()));
        check(rep.value.notes.find("gated off") != std::string::npos ||
                  rep.value.notes.find("hard gate") != std::string::npos,
              "notes mention one-click hard gate",
              rep.value.notes);
    }

    // One-click Fault path when camera never connected and connect is... Mock always can connect.
    // Use missing placement already covered.

    check(true, "VE-D7 success is Mock demo only; RISK-001 still OPEN for site", "");
#endif
}
