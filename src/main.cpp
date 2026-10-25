#include "olp/algo/CollisionAndSim.h"
#include "olp/algo/OpwKinematics.h"
#include "olp/algo/SeamAndToolpathAlgo.h"
#include "olp/app/WorkcellLayoutService.h"
#include "olp/io/FanucLsPostProcessor.h"
#include "olp/io/HtmlAnimationExporter.h"
#include "olp/ui/MainWindow.h"

#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    std::cout << "====================================================================\n";
    std::cout << "  SiFastSuite OLP v0.1.0 - Industrial Robot Welding OLP System\n";
    std::cout << "  Architecture: C++20 | 5-Layer Decoupled | FANUC OPW & .ls Engine\n";
    std::cout << "====================================================================\n";

    olp::ui::MainWindow mainWin;
    auto& doc = mainWin.document();
    auto& cad = mainWin.cadKernel();
    const auto& robotModel = mainWin.activeRobotModel();

    const std::string wsRoot = OLP_SOURCE_DIR;
    const std::string fullCellStep =
        wsRoot + "/SampleScenes/Scene2_Full_Workcell_STEP/Full_Welding_Cell_With_Robot.step";

    std::cout << "[1/5] Importing Full Workcell STEP & Executing 3.4 Scheme A Robot Replacement...\n";
    const auto repRes = olp::app::WorkcellLayoutService::importFullSceneAndReplaceRobot(
        doc, cad, fullCellStep, robotModel);

    if (!repRes.success) {
        std::cerr << "Failed to import workcell STEP: " << fullCellStep << "\n";
        return 1;
    }

    std::cout << "      -> Imported " << repRes.totalSolidsImported << " B-Rep solids.\n";
    std::cout << "      -> Hid " << repRes.staticRobotSolidsHidden
              << " static robot solids (FANUC_M10iA_Static_*).\n";
    std::cout << "      -> Placed movable '" << robotModel.displayName << "' at pedestal base ("
              << repRes.detectedRobotBaseTransform.pos.x << ", "
              << repRes.detectedRobotBaseTransform.pos.y << ", "
              << repRes.detectedRobotBaseTransform.pos.z << ") mm.\n";

    // Switch to OLP Workbench
    mainWin.switchWorkbench(olp::ui::ActiveWorkbench::OlpWorkbench);

    // Define UFRAME[1] on the welding fixture table top (Z = 600 mm)
    const auto& uframeNode = olp::app::WorkcellLayoutService::defineUFrameByThreePoints(
        doc, 1, "UFRAME_1_Table",
        {500.0, -300.0, 600.0},
        {900.0, -300.0, 600.0},
        {500.0,  100.0, 600.0});

    // Find Workpiece_BoxWall_Front and Workpiece_Base_Plate for T-joint / Box inner fillet weld
    int frontWallHandle = -1;
    std::string frontWallNodeId;
    std::vector<int> workpieceHandles;
    for (const auto& node : doc.sceneNodes) {
        if (node.visible && node.type == olp::core::SceneNodeType::Part &&
            node.collisionRole == olp::core::CollisionRole::Workpiece) {
            workpieceHandles.push_back(node.cadHandleId);
            if (node.name.find("Workpiece_BoxWall_Front") != std::string::npos) {
                frontWallHandle = node.cadHandleId;
                frontWallNodeId = node.id;
            }
        }
    }

    std::cout << "[2/5] Picking T-Joint / Box Corner Seam Edge & Computing Bisector Frames...\n";
    auto seamOpt = olp::algo::SeamAndToolpathAlgo::buildSeamFromPickedEdge(
        cad, frontWallHandle, 3, workpieceHandles, 50.0);
    if (!seamOpt.has_value()) {
        std::cerr << "Failed to build seam from picked edge.\n";
        return 1;
    }
    doc.seams.push_back(*seamOpt);
    std::cout << "      -> Built Seam '" << seamOpt->name << "' across parts ("
              << seamOpt->face1PartId << " & " << seamOpt->face2PartId << ") with "
              << seamOpt->sampledFrames.size() << " sampled frames.\n";

    // Create ControllerUnit, Program, and Operation
    std::cout << "[3/5] Generating Welding Toolpath & Process Events (Arc Start/End)...\n";
    olp::core::ControllerUnit cu;
    cu.id = "cu_1";
    cu.robotNodeId = repRes.activeRobotNodeId;

    olp::core::Program prog;
    prog.id = "prog_1";
    prog.name = "WELD_001";
    prog.uframeNumber = 1;
    prog.utoolNumber = 1;

    olp::core::Operation op;
    op.id = "op_1";
    op.name = "Fillet_BoxWall_Front";
    op.seamId = seamOpt->id;
    op.targetWorkpieceId = frontWallNodeId;
    op.genParams.travelAngleDeg = 10.0;
    op.genParams.approachDistMm = 45.0;
    op.genParams.retractDistMm = 45.0;

    olp::algo::SeamAndToolpathAlgo::generateOperationTargets(
        *seamOpt, uframeNode.localTransform, op);
    prog.operations.push_back(op);
    cu.programs.push_back(prog);
    doc.controllers.push_back(cu);

    // Run OPW Kinematics, Collision Detection (with wire-tip exemption) & Trajectory Simulation
    std::cout << "[4/5] Running OPW Inverse Kinematics, Collision Check & Frame Baking...\n";
    olp::algo::OpwKinematics kin(robotModel);
    olp::algo::CollisionAndSimEngine simEngine(kin, doc.settings);
    const olp::core::Transform4d utoolOffset = olp::core::Transform4d::fromTranslation(0.0, 0.0, 150.0);

    auto simRes = simEngine.validateAndSimulateProgram(
        doc.controllers[0].programs[0],
        repRes.detectedRobotBaseTransform,
        uframeNode.localTransform,
        utoolOffset,
        doc.sceneNodes,
        0.05);

    std::cout << "      -> Baked " << simRes.frames.size() << " animation frames | Cycle Time: "
              << simRes.totalCycleTimeSec << " s (Weld: " << simRes.arcWeldTimeSec
              << " s, Air: " << simRes.airCutTimeSec << " s).\n";

    // Export FANUC .ls and Single-File Offline HTML5 Animation Package
    std::cout << "[5/5] Exporting FANUC .ls Program & Single-File HTML 3D Preview...\n";
    const std::string lsOut = wsRoot + "/build/WELD_001.ls";
    const std::string htmlOut = wsRoot + "/build/WELD_001_Preview.html";
    olp::io::FanucLsPostProcessor::exportToFile(doc.controllers[0].programs[0], lsOut);
    olp::io::HtmlAnimationExporter::exportSingleFileHtml(
        htmlOut, doc.controllers[0].programs[0], doc.sceneNodes, simRes);

    std::cout << "      -> Exported FANUC .ls: " << lsOut << "\n";
    std::cout << "      -> Exported HTML 3D Animation: " << htmlOut << "\n";
    std::cout << "====================================================================\n";
    return 0;
}
