#include "test_support.h"

#include "olp/io/Utf8Path.h"
#include "olp/runtime/ContourDataset.h"
#include "olp/runtime/MockCamera.h"
#include "olp/runtime/MockIO.h"
#include "olp/runtime/MockRobot.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

std::string visionEdgeDatasetDir() {
#if defined(OLP_SOURCE_DIR)
    return olp::runtime::defaultVisionEdgeDatasetDir(OLP_SOURCE_DIR);
#else
    return olp::runtime::defaultVisionEdgeDatasetDir(
        olp::io::resolveWorkspaceOrAppDir());
#endif
}

bool nearlyEqual(double a, double b, double eps = 1e-6) {
    return std::fabs(a - b) <= eps;
}

} // namespace

void testVisionRuntimeDeviceMocks() {
    std::cout << "\n--- VE-D1: Device interfaces + Mock camera/robot/IO ---\n";

    const std::string datasetDir = visionEdgeDatasetDir();
    auto loaded = olp::runtime::ContourDataset::loadFromDirectory(datasetDir);
    check(loaded.ok(),
          "ContourDataset loads tests/data/vision_edge",
          loaded.status.message);
    if (!loaded.ok()) {
        return;
    }

    const auto& dataset = loaded.value;
    check(dataset.hasPlacement("placement_a"),
          "Dataset contains placement_a",
          "");
    check(dataset.hasPlacement("placement_b"),
          "Dataset contains placement_b",
          "");
    check(dataset.placementIds().size() >= 2,
          "Dataset has at least two placements",
          "count=" + std::to_string(dataset.placementIds().size()));

    auto frameA = dataset.frame("placement_a");
    check(frameA.ok() && frameA.value.polygonMm.size() == 4,
          "placement_a has 4 contour points",
          frameA.status.message);
    if (frameA.ok() && !frameA.value.polygonMm.empty()) {
        check(nearlyEqual(frameA.value.polygonMm[0].x, 50.0) &&
                  nearlyEqual(frameA.value.polygonMm[0].y, 40.0),
              "placement_a first vertex matches synthetic origin",
              "");
        check(frameA.value.fakePointCount > 0,
              "placement_a exposes fake point-cloud metadata",
              "fakePointCount=" + std::to_string(frameA.value.fakePointCount));
    }

    auto frameB = dataset.frame("placement_b");
    check(frameB.ok() && frameB.value.polygonMm.size() == 4,
          "placement_b has 4 contour points",
          frameB.status.message);
    if (frameA.ok() && frameB.ok()) {
        const auto& a0 = frameA.value.polygonMm[0];
        const auto& b0 = frameB.value.polygonMm[0];
        const double dx = b0.x - a0.x;
        const double dy = b0.y - a0.y;
        check(std::hypot(dx, dy) > 1.0,
              "placement_b differs from placement_a (pose change)",
              "delta=" + std::to_string(std::hypot(dx, dy)));
    }

    // --- Camera lifecycle + A/B replay ---
    olp::runtime::MockCamera camera(dataset);
    check(!camera.isConnected(), "MockCamera starts disconnected", "");

    auto capDisconnected = camera.captureContour();
    check(!capDisconnected.ok() &&
              capDisconnected.status.code == olp::runtime::DeviceError::NotConnected,
          "MockCamera capture fails when disconnected",
          capDisconnected.status.message);

    auto camConnect = camera.connect();
    check(camConnect.ok() && camera.isConnected(),
          "MockCamera connect succeeds",
          camConnect.message);

    auto selA = camera.selectPlacement("placement_a");
    check(selA.ok(), "selectPlacement(placement_a)", selA.message);
    auto capA = camera.captureContour();
    check(capA.ok() && capA.value.placementId == "placement_a",
          "captureContour returns placement_a",
          capA.status.message);

    auto selB = camera.selectPlacement("placement_b");
    check(selB.ok(), "selectPlacement(placement_b)", selB.message);
    auto capB = camera.captureContour();
    check(capB.ok() && capB.value.placementId == "placement_b",
          "captureContour returns placement_b",
          capB.status.message);

    auto selMissing = camera.selectPlacement("placement_z");
    check(!selMissing.ok() &&
              selMissing.code == olp::runtime::DeviceError::NotFound,
          "selectPlacement unknown id fails",
          selMissing.message);

    auto camDisconnect = camera.disconnect();
    check(camDisconnect.ok() && !camera.isConnected(),
          "MockCamera disconnect",
          camDisconnect.message);

    // --- Robot record program / trajectory ---
    olp::runtime::MockRobot robot;
    check(!robot.isConnected(), "MockRobot starts disconnected", "");

    auto upDisconnected = robot.uploadProgram({"EDGE_DEMO", ""});
    check(!upDisconnected.ok() &&
              upDisconnected.code == olp::runtime::DeviceError::NotConnected,
          "MockRobot upload fails when disconnected",
          upDisconnected.message);

    check(robot.connect().ok() && robot.isConnected(),
          "MockRobot connect succeeds",
          "");

    auto emptyName = robot.uploadProgram({"", "PROG"});
    check(!emptyName.ok() &&
              emptyName.code == olp::runtime::DeviceError::InvalidArgument,
          "MockRobot rejects empty program name",
          emptyName.message);

    olp::runtime::RobotProgram prog;
    prog.name = "EDGE_DEMO";
    prog.content = "! MOCK LS STUB\n/PROG EDGE_DEMO\n/END\n";
    auto uploaded = robot.uploadProgram(prog);
    check(uploaded.ok(), "MockRobot uploadProgram succeeds", uploaded.message);
    check(robot.uploadedPrograms().size() == 1 &&
              robot.uploadedPrograms().front().name == "EDGE_DEMO",
          "MockRobot records uploaded program",
          "");

    auto runMissing = robot.runProgram("NOT_UPLOADED");
    check(!runMissing.ok() &&
              runMissing.code == olp::runtime::DeviceError::NotFound,
          "MockRobot run unknown program fails",
          runMissing.message);

    auto ran = robot.runProgram("EDGE_DEMO");
    check(ran.ok(), "MockRobot runProgram succeeds", ran.message);
    check(robot.runHistory().size() == 1 &&
              robot.runHistory().front() == "EDGE_DEMO",
          "MockRobot records run history",
          "");

    auto emptyTraj = robot.executeTrajectory({});
    check(!emptyTraj.ok() &&
              emptyTraj.code == olp::runtime::DeviceError::InvalidArgument,
          "MockRobot rejects empty trajectory",
          emptyTraj.message);

    std::vector<olp::runtime::CartesianWaypointMm> traj(3);
    traj[0].positionMm = {100.0, 100.0, 50.0};
    traj[1].positionMm = {150.0, 100.0, 50.0};
    traj[2].positionMm = {150.0, 150.0, 50.0};
    auto trajOk = robot.executeTrajectory(traj);
    check(trajOk.ok(), "MockRobot executeTrajectory succeeds", trajOk.message);
    check(robot.executedTrajectories().size() == 1 &&
              robot.executedTrajectories().front().size() == 3,
          "MockRobot records trajectory waypoints",
          "");

    auto state = robot.queryState();
    check(state.ok() && state.value.connected &&
              state.value.lastProgramName == "EDGE_DEMO",
          "MockRobot queryState reports last program",
          state.status.message);

    check(robot.disconnect().ok() && !robot.isConnected(),
          "MockRobot disconnect",
          "");

    // --- IO lifecycle + failure paths ---
    olp::runtime::MockIO io;
    check(!io.isConnected(), "MockIO starts disconnected", "");
    auto diDisc = io.readDi(0);
    check(!diDisc.ok() &&
              diDisc.status.code == olp::runtime::DeviceError::NotConnected,
          "MockIO readDi fails when disconnected",
          diDisc.status.message);

    check(io.connect().ok() && io.isConnected(), "MockIO connect", "");
    io.setDi(1, true);
    auto di1 = io.readDi(1);
    check(di1.ok() && di1.value == true, "MockIO readDi seeded channel", "");
    auto di0 = io.readDi(0);
    check(di0.ok() && di0.value == false, "MockIO unread DI defaults false", "");

    auto badDi = io.readDi(-1);
    check(!badDi.ok() &&
              badDi.status.code == olp::runtime::DeviceError::InvalidArgument,
          "MockIO rejects negative DI channel",
          badDi.status.message);

    check(io.writeDo(2, true).ok() && io.doValue(2),
          "MockIO writeDo stores value",
          "");
    auto badDo = io.writeDo(-3, false);
    check(!badDo.ok() &&
              badDo.code == olp::runtime::DeviceError::InvalidArgument,
          "MockIO rejects negative DO channel",
          badDo.message);

    check(io.disconnect().ok() && !io.isConnected(), "MockIO disconnect", "");

    // Dataset failure paths
    auto missingDir = olp::runtime::ContourDataset::loadFromDirectory(
        datasetDir + "/__missing_subdir__");
    check(!missingDir.ok() &&
              missingDir.status.code == olp::runtime::DeviceError::NotFound,
          "ContourDataset fails for missing directory",
          missingDir.status.message);

    auto unknownFrame = dataset.frame("no_such_placement");
    check(!unknownFrame.ok() &&
              unknownFrame.status.code == olp::runtime::DeviceError::NotFound,
          "ContourDataset::frame unknown id fails",
          unknownFrame.status.message);

    // VE-D1-001: bad polygonMm element types must return DeviceResult failure,
    // not throw nlohmann::json::type_error out of the public API.
#if defined(OLP_HAS_NLOHMANN_JSON)
    {
        namespace fs = std::filesystem;
        const fs::path badDir =
            fs::temp_directory_path() / "ve_d1_bad_json_runtime_test";
        std::error_code ec;
        fs::remove_all(badDir, ec);
        fs::create_directories(badDir, ec);
        const fs::path badFile = badDir / "placement_bad.json";
        {
            std::ofstream out(badFile);
            out << R"({"placementId":"placement_bad","polygonMm":[["x","y"],[1,2,3],[4,5,6]]})"
                << "\n";
        }
        bool threw = false;
        std::string throwWhat;
        olp::runtime::DeviceResult<olp::runtime::ContourDataset> badLoaded;
        try {
            badLoaded = olp::runtime::ContourDataset::loadFromDirectory(
                olp::io::pathToUtf8(badDir));
        } catch (const std::exception& ex) {
            threw = true;
            throwWhat = ex.what();
        }
        check(!threw,
              "VE-D1-001 bad JSON polygonMm types do not throw",
              throwWhat);
        check(!badLoaded.ok() &&
                  badLoaded.status.code ==
                      olp::runtime::DeviceError::InvalidArgument,
              "VE-D1-001 bad JSON polygonMm maps to InvalidArgument",
              badLoaded.status.message);
        check(badLoaded.status.message.find("type") != std::string::npos ||
                  badLoaded.status.message.find("JSON") != std::string::npos,
              "VE-D1-001 failure message is diagnostic",
              badLoaded.status.message);
        fs::remove_all(badDir, ec);
    }
#endif
}
