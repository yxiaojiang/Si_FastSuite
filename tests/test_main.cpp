#include "olp/algo/CollisionAndSim.h"
#include "olp/algo/OpwKinematics.h"
#include "olp/algo/SeamAndToolpathAlgo.h"
#include "olp/app/CommandManager.h"
#include "olp/app/WorkcellLayoutService.h"
#include "olp/cad/OcctCadKernel.h"
#include "olp/device/RobotModel.h"
#include "olp/io/FanucLsPostProcessor.h"
#include "olp/ui/MainWindow.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace {

int g_passed = 0;
int g_failed = 0;

void check(bool cond, const std::string& testName, const std::string& detail = "") {
    if (cond) {
        ++g_passed;
        std::cout << "  [PASS] " << testName << "\n";
    } else {
        ++g_failed;
        std::cerr << "  [FAIL] " << testName << (detail.empty() ? "" : (" -> " + detail)) << "\n";
    }
}

void testWprAndGimbalLock() {
    std::cout << "\n--- Test 1: FANUC WPR Conversion & Gimbal Lock (Section 6.4) ---\n";
    const olp::core::WprPose inPose{450.25, -120.50, 310.00, 180.0, -45.0, 30.0};
    const auto T = olp::core::Transform4d::fromWPR(inPose);
    const auto outPose = T.toWPR();
    const auto T2 = olp::core::Transform4d::fromWPR(outPose);

    const double posErr = (T.pos - T2.pos).norm();
    const double rotErrDeg = olp::core::rad2deg(T.rot.angularDistanceRad(T2.rot));
    check(posErr < 1e-9 && rotErrDeg < 1e-7,
          "Standard WPR round-trip precision (< 1e-7 deg)",
          "rotErrDeg=" + std::to_string(rotErrDeg));

    // Gimbal lock at P = +90 deg and P = -90 deg
    const olp::core::WprPose gimbalPose{100.0, 200.0, 300.0, 25.0, 90.0, -40.0};
    const auto Tg1 = olp::core::Transform4d::fromWPR(gimbalPose);
    const auto Tg2 = olp::core::Transform4d::fromWPR(Tg1.toWPR());
    const double gimbalRotErrDeg = olp::core::rad2deg(Tg1.rot.angularDistanceRad(Tg2.rot));
    check(gimbalRotErrDeg < 1e-6,
          "Gimbal lock (P = +90 deg) rotation matrix round-trip",
          "gimbalRotErrDeg=" + std::to_string(gimbalRotErrDeg));
}

void testUrdfAndStlLoading() {
    std::cout << "\n--- Test 2: SampleScenes Scene1 URDF & STL Mesh Loading (M02/M05) ---\n";
    const std::string ws = OLP_SOURCE_DIR;
    const std::string urdfPath =
        ws + "/SampleScenes/Scene1_TJoint_And_FANUC_M10iA/Robot_FANUC_M10iA/urdf/fanuc_m10ia_standalone.urdf";

    auto robotOpt = olp::device::RobotModel::loadFromUrdf(urdfPath);
    check(robotOpt.has_value(), "Load fanuc_m10ia_standalone.urdf");
    if (robotOpt) {
        check(std::abs(robotOpt->jointLimits[0].upperRad - 2.967) < 1e-4 &&
              std::abs(robotOpt->opw.c2 - 600.0) < 1e-4,
              "Verify URDF joint limits and OPW parameters (c2=600mm, J1=+-2.967rad)");

        olp::cad::OcctCadKernel cad;
        olp::core::TriMesh baseMesh;
        const bool stlOk = cad.importStl(robotOpt->visualMeshFiles[0], baseMesh);
        check(stlOk && !baseMesh.triangles.empty(),
              "Load binary STL mesh (base_link.stl, " + std::to_string(baseMesh.triangles.size()) + " tris)");
    }
}

void testOpwKinematics100Samples() {
    std::cout << "\n--- Test 3: OPW FK/IK 100-Sample Closed-Loop & J2/J3 Coupling (Section 11.1) ---\n";
    const auto model = olp::device::RobotModel::createFanucM10iA();
    olp::algo::OpwKinematics kin(model);
    const auto utool = olp::core::Transform4d::fromWPR({0.0, 0.0, 160.0, 0.0, 35.0, 0.0});

    double maxPosErrMm = 0.0;
    double maxRotErrDeg = 0.0;
    double maxJointRecoveryErrDeg = 0.0;
    int successCount = 0;

    for (int i = 0; i < 100; ++i) {
        const double t = static_cast<double>(i) / 100.0;
        const std::array<double, 6> testJointsRad = {
            -1.2 + 2.4 * t,
            -0.4 + 1.0 * std::sin(i * 0.37),
            -0.6 + 1.0 * std::cos(i * 0.29),
            2.5 * std::sin(i * 0.53),
            -1.2 - 0.3 * std::cos(i * 0.41), // Keep J5 away from 0 to test non-singular posture recovery
            -2.0 + 4.0 * t
        };

        const auto fk = kin.forwardKinematics(testJointsRad, utool);
        const auto ikSols = kin.inverseKinematics(fk.tcpPoseInBase, utool, false);
        if (ikSols.empty()) continue;

        // Verify all IK solutions map back to the exact target TCP pose
        for (const auto& sol : ikSols) {
            const auto fkCheck = kin.forwardKinematics(sol.jointsRad, utool);
            const double pErr = (fkCheck.tcpPoseInBase.pos - fk.tcpPoseInBase.pos).norm();
            const double rErr = olp::core::rad2deg(
                fkCheck.tcpPoseInBase.rot.angularDistanceRad(fk.tcpPoseInBase.rot));
            maxPosErrMm = std::max(maxPosErrMm, pErr);
            maxRotErrDeg = std::max(maxRotErrDeg, rErr);
        }

        // Verify selectBestSolution recovers the exact original joint angles
        const auto recovered = kin.selectBestSolution(ikSols, testJointsRad, fk.config, true);
        if (recovered.has_value()) {
            ++successCount;
            for (size_t k = 0; k < 6; ++k) {
                const double jErrDeg = olp::core::rad2deg(
                    std::abs(olp::core::normalizeAngleRad(recovered->jointsRad[k] - testJointsRad[k])));
                maxJointRecoveryErrDeg = std::max(maxJointRecoveryErrDeg, jErrDeg);
            }
        }
    }

    check(successCount == 100 && maxPosErrMm < 0.01 && maxRotErrDeg < 0.001 && maxJointRecoveryErrDeg < 0.001,
          "100-Sample FK(IK(x)) = x (maxPosErr=" + std::to_string(maxPosErrMm) +
          "mm, maxRotErr=" + std::to_string(maxRotErrDeg) + "deg)",
          "successCount=" + std::to_string(successCount) +
          ", maxJointRecoveryErrDeg=" + std::to_string(maxJointRecoveryErrDeg));
}

void testSchemeAFullWorkcellImport() {
    std::cout << "\n--- Test 4: Section 3.4 Scheme A Full Workcell STEP Import & Robot Replacement (Section 11.7) ---\n";
    olp::app::ProjectDocument doc;
    olp::cad::OcctCadKernel cad;
    const auto robotModel = olp::device::RobotModel::createFanucM10iA();
    const std::string stepPath =
        std::string(OLP_SOURCE_DIR) + "/SampleScenes/Scene2_Full_Workcell_STEP/Full_Welding_Cell_With_Robot.step";

    const auto res = olp::app::WorkcellLayoutService::importFullSceneAndReplaceRobot(
        doc, cad, stepPath, robotModel);

    check(res.success && res.totalSolidsImported == 12 && res.staticRobotSolidsHidden == 5,
          "Import 12 solids from Full_Welding_Cell_With_Robot.step and hide 5 static robot solids");

    // Pedestal top is at (0, 0, 500) mm; verify alignment deviation < 1.0 mm (Section 11.7)
    const olp::core::Vec3 expectedBase{0.0, 0.0, 500.0};
    const double baseDevMm = (res.detectedRobotBaseTransform.pos - expectedBase).norm();
    check(baseDevMm < 1.0,
          "Replaced movable FANUC M-10iA base alignment error < 1 mm (actual=" +
          std::to_string(baseDevMm) + " mm)");
}

void testTJointCrossPartSeamAndCollisionExemption() {
    std::cout << "\n--- Test 5: T-Joint Cross-Part Seam Recognition & Wire-Tip Collision Exemption (Section 11.3 & 11.4) ---\n";
    olp::cad::OcctCadKernel cad;
    std::vector<olp::core::SceneNode> nodes;
    const std::string tjointPath =
        std::string(OLP_SOURCE_DIR) + "/SampleScenes/Scene1_TJoint_And_FANUC_M10iA/Workpiece_TJoint_With_Clamps.step";

    const bool ok = cad.importStep(tjointPath, nodes);
    check(ok && nodes.size() == 4, "Import 4 solids from Workpiece_TJoint_With_Clamps.step");

    int webHandle = -1;
    std::string webNodeId;
    std::vector<int> wpHandles;
    for (const auto& n : nodes) {
        if (n.collisionRole == olp::core::CollisionRole::Workpiece) {
            wpHandles.push_back(n.cadHandleId);
            if (n.name.find("Vertical_Web_Plate") != std::string::npos) {
                webHandle = n.cadHandleId;
                webNodeId = n.id;
            }
        }
    }

    // Edge index 3 is the 400mm long front T-joint seam (-200,-6,12) -> (200,-6,12)
    auto seamOpt = olp::algo::SeamAndToolpathAlgo::buildSeamFromPickedEdge(
        cad, webHandle, 3, wpHandles, 50.0);
    check(seamOpt.has_value() && seamOpt->face1PartId != seamOpt->face2PartId,
          "Cross-part T-joint flank recognition (Vertical_Web_Plate edge + Base_Plate mating face)");

    if (seamOpt) {
        // Verify angle bisector between +Z (0,0,1) and -Y (0,-1,0) is 45 deg
        const auto& sf = seamOpt->sampledFrames.front();
        const olp::core::Vec3 expectedBisector = olp::core::Vec3{0.0, -1.0, 1.0}.normalized();
        const double angleDiffDeg = olp::core::rad2deg(
            std::acos(std::clamp(std::abs(sf.bisector.dot(expectedBisector)), 0.0, 1.0)));
        check(angleDiffDeg < 5.0,
              "Auto-computed T-joint torch bisector angle deviation < 5 deg (actual=" +
              std::to_string(angleDiffDeg) + " deg)");

        // Test wire-tip exemption vs clamp collision (M10)
        const auto robotModel = olp::device::RobotModel::createFanucM10iA();
        olp::algo::OpwKinematics kin(robotModel);
        olp::core::UnitsSettings settings;
        olp::algo::CollisionAndSimEngine colEngine(kin, settings);

        double minClearance = 0.0;
        std::string hitPart;
        const auto torchPoseAtSeam = olp::algo::SeamAndToolpathAlgo::applyWeldingAngles(
            sf.frame, 0.0, 10.0, 0.0);
        const bool falseAlarm = colEngine.checkTorchPoseCollision(
            torchPoseAtSeam, nodes, webNodeId, true, minClearance, hitPart);
        check(!falseAlarm, "Wire-tip collision exemption prevents false alarm at weld root");

        // Place torch body directly inside Fixture_Clamp_Left (-180..-120, -130..-40, 12..52)
        olp::core::Transform4d collidingPose = torchPoseAtSeam;
        collidingPose.pos = {-150.0, -80.0, 30.0};
        collidingPose.rot = olp::core::Mat3::identity();
        const bool clampDetected = colEngine.checkTorchPoseCollision(
            collidingPose, nodes, webNodeId, true, minClearance, hitPart);
        check(clampDetected && hitPart.find("Fixture_Clamp_Left") != std::string::npos,
              "Detect torch collision against Fixture_Clamp_Left (" + hitPart + ")");
    }
}

void testGizmoAndUndoRedo100Steps() {
    std::cout << "\n--- Test 6: TcpGizmo Preview/Commit/Esc & 100-Step Undo/Redo Stack (Section 11.5 & 11.8) ---\n";
    olp::ui::MainWindow win;
    auto& doc = win.document();
    auto& cmdMgr = win.commandManager();
    auto& gizmo = win.viewport().tcpGizmo();

    olp::core::ControllerUnit cu;
    olp::core::Program prog;
    olp::core::Operation op;
    op.id = "op_test";
    olp::core::Target t1;
    t1.id = "target_1";
    t1.name = "WELD_START";
    t1.poseInUFrame = olp::core::Transform4d::fromTranslation(100.0, 200.0, 300.0);
    op.targets.push_back(t1);
    prog.operations.push_back(op);
    cu.programs.push_back(prog);
    doc.controllers.push_back(cu);

    // 1. Begin drag and move 20 frames -> undoStack must remain 0 during preview
    int previewCount = 0;
    gizmo.setPreviewCallback([&](const olp::ui::GizmoPreviewState&) { ++previewCount; });
    gizmo.setSnapSteps(true, 1.0, 1.0);
    gizmo.beginDrag({"target_1"});
    for (int i = 1; i <= 20; ++i) {
        gizmo.updateDragDelta({i * 1.4, 0.0, 0.0}, {0.0, 0.0, 0.0});
    }
    check(previewCount == 20 && cmdMgr.undoCount() == 0 &&
          std::abs(doc.findTarget("target_1")->poseInUFrame.pos.x - 100.0) < 1e-6,
          "TcpGizmo 20 drag frames emit previews without polluting Undo stack");

    // 2. Release mouse -> commits exactly 1 command with 1mm snapped translation (20 * 1.4 = 28.0 mm)
    gizmo.commitDrag();
    check(cmdMgr.undoCount() == 1 &&
          std::abs(doc.findTarget("target_1")->poseInUFrame.pos.x - 128.0) < 1e-6 &&
          doc.findTarget("target_1")->userModified,
          "TcpGizmo mouse release commits 1 command (snapped X = 128.0 mm, userModified = true)");

    // 3. Drag and press Esc -> cancels without adding undo command
    gizmo.beginDrag({"target_1"});
    gizmo.updateDragDelta({50.0, 50.0, 50.0}, {10.0, 0.0, 0.0});
    gizmo.cancelDrag();
    check(cmdMgr.undoCount() == 1 &&
          std::abs(doc.findTarget("target_1")->poseInUFrame.pos.x - 128.0) < 1e-6,
          "TcpGizmo Esc key cancels active drag cleanly");

    // 4. Execute 100 commands, Undo 100 steps, Redo 100 steps
    cmdMgr.clear();
    const double baseZ = doc.findTarget("target_1")->poseInUFrame.pos.z;
    for (int step = 1; step <= 100; ++step) {
        auto pose = doc.findTarget("target_1")->poseInUFrame;
        pose.pos.z = baseZ + step;
        cmdMgr.executeCommand(std::make_unique<olp::app::ModifyTargetPoseCommand>(
            doc, "target_1", pose, false));
    }
    check(cmdMgr.undoCount() == 100 &&
          std::abs(doc.findTarget("target_1")->poseInUFrame.pos.z - (baseZ + 100.0)) < 1e-6,
          "Push 100 commands to Undo stack");

    for (int step = 0; step < 100; ++step) {
        cmdMgr.undo();
    }
    check(cmdMgr.undoCount() == 0 && cmdMgr.redoCount() == 100 &&
          std::abs(doc.findTarget("target_1")->poseInUFrame.pos.z - baseZ) < 1e-6,
          "Undo 100 consecutive steps restores exact initial state");

    for (int step = 0; step < 100; ++step) {
        cmdMgr.redo();
    }
    check(cmdMgr.undoCount() == 100 && cmdMgr.redoCount() == 0 &&
          std::abs(doc.findTarget("target_1")->poseInUFrame.pos.z - (baseZ + 100.0)) < 1e-6,
          "Redo 100 consecutive steps restores exact final state");
}

void testFanucLsGeneration() {
    std::cout << "\n--- Test 7: FANUC .ls Post-Processor Golden Structure (M12 & Section 11.2) ---\n";
    olp::core::Program prog;
    prog.name = "WELD_001";
    prog.uframeNumber = 1;
    prog.utoolNumber = 1;

    olp::core::Operation op;
    op.id = "op_1";
    op.name = "TJoint_Fillet";

    olp::core::Target tStart;
    tStart.name = "WELD_START";
    tStart.role = olp::core::TargetRole::WELD_START;
    tStart.motion = olp::core::MotionType::L;
    tStart.speedValue = 200.0;
    tStart.termination = olp::core::TerminationType::FINE;
    tStart.poseInUFrame = olp::core::Transform4d::fromWPR({450.25, -120.50, 310.00, 180.0, -45.0, 0.0});
    tStart.events.push_back({olp::core::ProcessEventType::ArcStart, 1, 0.0, ""});

    olp::core::Target tEnd;
    tEnd.name = "WELD_END";
    tEnd.role = olp::core::TargetRole::WELD_END;
    tEnd.motion = olp::core::MotionType::L;
    tEnd.useWeldSpeed = true;
    tEnd.termination = olp::core::TerminationType::FINE;
    tEnd.poseInUFrame = olp::core::Transform4d::fromWPR({450.25, 120.50, 310.00, 180.0, -45.0, 0.0});
    tEnd.events.push_back({olp::core::ProcessEventType::ArcEnd, 1, 0.0, ""});

    op.targets = {tStart, tEnd};
    prog.operations.push_back(op);

    const std::string lsText = olp::io::FanucLsPostProcessor::generateLsString(prog);
    const bool hasSections =
        lsText.find("/PROG  WELD_001") != std::string::npos &&
        lsText.find("/ATTR") != std::string::npos &&
        lsText.find("/APPL") != std::string::npos &&
        lsText.find("ARC Welding Equipment : 1,*,*,*,*;") != std::string::npos &&
        lsText.find("/MN") != std::string::npos &&
        lsText.find("Arc Start[1] ;") != std::string::npos &&
        lsText.find("WELD_SPEED FINE") != std::string::npos &&
        lsText.find("Arc End[1] ;") != std::string::npos &&
        lsText.find("/POS") != std::string::npos &&
        lsText.find("CONFIG : 'N U T, 0, 0, 0'") != std::string::npos &&
        lsText.find("/END") != std::string::npos;

    check(hasSections, "Verify generated .ls contains /PROG, /ATTR, /APPL, /MN, Arc Start/End, /POS, /END");
}

} // namespace

int main() {
    std::cout << "====================================================================\n";
    std::cout << "  Running SiFastSuite OLP Automated Verification Suite\n";
    std::cout << "====================================================================\n";

    testWprAndGimbalLock();
    testUrdfAndStlLoading();
    testOpwKinematics100Samples();
    testSchemeAFullWorkcellImport();
    testTJointCrossPartSeamAndCollisionExemption();
    testGizmoAndUndoRedo100Steps();
    testFanucLsGeneration();

    std::cout << "\n====================================================================\n";
    std::cout << "  Summary: " << g_passed << " passed, " << g_failed << " failed.\n";
    std::cout << "====================================================================\n";
    return (g_failed == 0) ? 0 : 1;
}
