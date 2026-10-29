#include "test_support.h"
#include "olp/algo/CollisionAndSim.h"
#include "olp/algo/OpwKinematics.h"
#include "olp/algo/SeamAndToolpathAlgo.h"
#include "olp/app/CommandManager.h"
#include "olp/app/WorkcellLayoutService.h"
#include "olp/cad/OcctCadKernel.h"
#include "olp/device/RobotModel.h"
#include "olp/io/FanucLsPostProcessor.h"
#include "olp/ui/MainWindow.h"
#include "olp/ui/OcctViewport.h"
#include "olp/ui/TcpGizmo.h"

#ifdef OLP_HAS_QT6
#include "olp/ui/ShellWindow.h"
#include <QAction>
#include <QApplication>
#include <QListWidget>
#include <QStringList>
#endif

#ifdef OLP_HAS_OCCT
#include <TopoDS.hxx>
#endif

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

void testGizmoAndUndoRedo100Steps() {
    std::cout << "\n--- Test 6: TcpGizmo Preview/Commit/Esc & 100-Step Undo/Redo Stack (Section 11.5 & 11.8) ---\n";
    olp::ui::MainWindow win;
    auto& doc = win.document();
    auto& cmdMgr = win.commandManager();
    olp::ui::TcpGizmo gizmo(doc, cmdMgr);

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


void testMainWindowZones() {
    std::cout << "\n--- Test 11: Main window zones ---\n";
#ifndef OLP_HAS_QT6
    check(false, "Qt6 is required to build the main window");
#else
    qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef OLP_QT_PLUGINS
    qputenv("QT_PLUGIN_PATH", OLP_QT_PLUGINS);
#endif
    // QApplication is created in main()
    olp::ui::MainWindow window;
    olp::ui::ShellWindow shell(window);
    shell.show();
    const QStringList titles = shell.dockTitles();
    const QStringList required = {
        QString::fromUtf8("场景"),
        QString::fromUtf8("程序"),
        QString::fromUtf8("路径点"),
        QString::fromUtf8("属性"),
        QString::fromUtf8("关节点动"),
        QString::fromUtf8("问题")
    };
    QString missing;
    for (const auto& title : required) {
        if (!titles.contains(title)) {
            missing += title + " ";
        }
    }
    check(missing.isEmpty(), "Main window has the six functional docks", missing.toStdString());
    check(shell.zoneChecked(QString::fromUtf8("场景")) &&
              shell.zoneChecked(QString::fromUtf8("属性")) &&
              shell.zoneChecked(QString::fromUtf8("关节点动")) &&
              !shell.zoneChecked(QString::fromUtf8("程序")) &&
              !shell.zoneChecked(QString::fromUtf8("路径点")) &&
              !shell.zoneChecked(QString::fromUtf8("问题")),
          "Layout workbench keeps scene, properties, and jog");
    shell.showOlpWorkbench();
    check(shell.zoneChecked(QString::fromUtf8("场景")) &&
              shell.zoneChecked(QString::fromUtf8("程序")) &&
              shell.zoneChecked(QString::fromUtf8("路径点")) &&
              shell.zoneChecked(QString::fromUtf8("关节点动")) &&
              shell.zoneChecked(QString::fromUtf8("问题")) &&
              shell.zoneChecked(QString::fromUtf8("属性")),
          "Programming workbench adds the program, points, and issues");
    shell.showLayoutWorkbench();
    check(shell.zoneChecked(QString::fromUtf8("场景")) &&
              shell.zoneChecked(QString::fromUtf8("属性")) &&
              shell.zoneChecked(QString::fromUtf8("关节点动")) &&
              !shell.zoneChecked(QString::fromUtf8("程序")),
          "Switching back to the layout workbench hides the program dock");

    check(shell.cursor().shape() == Qt::ArrowCursor, "ShellWindow has default ArrowCursor configured");

    auto findAction = [&](const QString& text) -> QAction* {
        for (auto* a : shell.findChildren<QAction*>()) {
            if (a->text() == text) return a;
        }
        return nullptr;
    };

    auto* actExportLs = findAction(QString::fromUtf8("导出 FANUC .ls 程序..."));
    auto* actExportPdf = findAction(QString::fromUtf8("导出 2D PDF 工艺报告..."));
    auto* actUndo = findAction(QString::fromUtf8("撤销"));
    auto* actNew = findAction(QString::fromUtf8("新建"));
    auto* actOpen = findAction(QString::fromUtf8("打开..."));
    auto* actLoad = findAction(QString::fromUtf8("加载示例工位"));

    check(actExportLs != nullptr && !actExportLs->isEnabled(), "Empty project: FANUC .ls export is grayed out (disabled)");
    check(actExportPdf != nullptr && !actExportPdf->isEnabled(), "Empty project: 2D PDF report export is grayed out (disabled)");
    check(actUndo != nullptr && !actUndo->isEnabled(), "Empty project: Undo is grayed out (disabled)");
    check(actNew != nullptr && !actNew->isEnabled(), "Unimplemented New action is grayed out (disabled)");
    check(actOpen != nullptr && actOpen->isEnabled(), "Open action is functional (enabled)");
    check(actLoad != nullptr && actLoad->isEnabled(), "Load sample action is functional (enabled)");
#endif
}


void testBRepMeshRenderingAndEdgePicking() {
    std::cout << "\n--- Test 12: B-Rep Mesh Rendering and Workpiece Edge Picking ---\n";
    olp::cad::OcctCadKernel cad;
    const std::string fullCellStep =
        std::string(OLP_SOURCE_DIR) + "/SampleScenes/Scene2_Full_Workcell_STEP/Full_Welding_Cell_With_Robot.step";

    std::vector<olp::core::SceneNode> parts;
    bool ok = cad.importStep(fullCellStep, parts);
    check(ok, "Import STEP for B-Rep mesh rendering and edge picking");

    int boxWallHandle = -1;
    const olp::core::SceneNode* boxWallNode = nullptr;
    for (const auto& p : parts) {
        if (p.name.find("Workpiece_BoxWall_Front") != std::string::npos) {
            boxWallHandle = p.cadHandleId;
            boxWallNode = &p;
            break;
        }
    }
    check(boxWallHandle > 0 && boxWallNode != nullptr, "Located Workpiece_BoxWall_Front in imported parts");

#ifdef OLP_HAS_OCCT
    // 1. Verify real TopoDS_Shape is available from OcctCadKernel
    const TopoDS_Shape shape = cad.getShape(boxWallHandle);
    check(!shape.IsNull(), "OcctCadKernel::getShape returns valid TopoDS_Shape for workpiece");

    // 2. Verify tessellate generates real B-Rep mesh (fine render mesh and coarse collision mesh)
    olp::core::TriMesh renderMesh, collisionMesh;
    const bool tessOk = cad.tessellate(boxWallHandle, 0.2, 2.0, renderMesh, collisionMesh);
    check(tessOk, "OcctCadKernel::tessellate succeeds for B-Rep solid");
    check(renderMesh.vertices.size() >= 8 && renderMesh.triangles.size() >= 12,
          "Tessellated render mesh contains actual 3D triangulation (triangles=" +
          std::to_string(renderMesh.triangles.size()) + ")");
    check(collisionMesh.triangles.size() > 0,
          "Tessellated collision mesh contains coarse triangulation (triangles=" +
          std::to_string(collisionMesh.triangles.size()) + ")");

    // 3. Verify Edge shape queries and findEdgeIndex round-trip
    const auto edges = cad.getEdges(boxWallHandle);
    check(edges.size() >= 4, "Workpiece has expected CAD edge count (" + std::to_string(edges.size()) + ")");
    constexpr int kEdgeIdx = 3;
    const TopoDS_Edge edgeShape = cad.getEdgeShape(boxWallHandle, kEdgeIdx);
    check(!edgeShape.IsNull(), "OcctCadKernel::getEdgeShape returns valid TopoDS_Edge");
    const int resolvedIdx = cad.findEdgeIndex(boxWallHandle, edgeShape);
    check(resolvedIdx == kEdgeIdx, "findEdgeIndex round-trip matches edge index (" +
          std::to_string(resolvedIdx) + " == " + std::to_string(kEdgeIdx) + ")");
    gp_Trsf dummyTrsf;
    dummyTrsf.SetTranslation(gp_Vec(100.0, 200.0, 300.0));
    const TopoDS_Edge transformedEdge = TopoDS::Edge(edgeShape.Moved(TopLoc_Location(dummyTrsf)));
    const int resolvedTransformedIdx = cad.findEdgeIndex(boxWallHandle, transformedEdge);
    check(resolvedTransformedIdx == kEdgeIdx, "findEdgeIndex matches transformed edge via topological partner");

    // 4. Verify OcctViewport edge selection and callback
    olp::app::ProjectDocument doc;
    doc.sceneNodes = parts;
    olp::app::CommandManager cmdMgr(50);
    olp::ui::OcctViewport viewport(doc, cmdMgr, &cad);

    bool callbackFired = false;
    olp::ui::PickedEdge callbackEdge{};
    viewport.setEdgePickedCallback([&](const olp::ui::PickedEdge& pe) {
        callbackFired = true;
        callbackEdge = pe;
    });

    viewport.setSelectionMode(olp::ui::SelectionSubShapeMode::Edge);
    check(viewport.selectionMode() == olp::ui::SelectionSubShapeMode::Edge,
          "OcctViewport selection mode set to Edge");

    viewport.setDisplayMode(olp::ui::ViewportDisplayMode::ShadedWithEdges);
    check(viewport.displayMode() == olp::ui::ViewportDisplayMode::ShadedWithEdges,
          "OcctViewport display mode set to ShadedWithEdges");

    // Sync scene and select edge
    viewport.syncScene();
    const bool selectOk = viewport.selectEdge(boxWallHandle, kEdgeIdx);
    check(selectOk && callbackFired, "OcctViewport::selectEdge triggers edge-picked callback");
    check(viewport.lastPickedEdge().has_value() && viewport.lastPickedEdge()->edgeIndex == kEdgeIdx,
          "OcctViewport records picked edge index in lastPickedEdge");
    check(callbackEdge.lengthMm > 1.0,
          "Picked edge records valid length (" + std::to_string(callbackEdge.lengthMm) + " mm)");

    // 5. Verify CreateSeamFromPickedEdgeCommand Undo/Redo
    doc.isDirty = false;
    const size_t initialSeamCount = doc.seams.size();
    auto createSeamCmd = std::make_unique<olp::app::CreateSeamFromPickedEdgeCommand>(
        doc, cad, boxWallHandle, kEdgeIdx, 50.0);
    cmdMgr.executeCommand(std::move(createSeamCmd));

    check(doc.seams.size() == initialSeamCount + 1, "CreateSeamFromPickedEdgeCommand adds seam to document");
    check(doc.isDirty, "CreateSeamFromPickedEdgeCommand marks document dirty");
    check(cmdMgr.canUndo(), "Command pushed to Undo stack");

    // Sync scene to verify viewport renders new seam
    viewport.syncScene();

    cmdMgr.undo();
    check(doc.seams.size() == initialSeamCount, "Undo CreateSeamFromPickedEdgeCommand restores seam list");
    check(!doc.isDirty, "Undo restores clean document dirty state");

    cmdMgr.redo();
    check(doc.seams.size() == initialSeamCount + 1, "Redo CreateSeamFromPickedEdgeCommand restores created seam");
    check(doc.isDirty, "Redo marks document dirty again");
#endif
}


void testTcpGizmoMountAndInteraction() {
    std::cout << "\n--- Test 13: TcpGizmo Mount and Interactive Drag in OcctViewport ---\n";
    olp::app::ProjectDocument doc;
    olp::app::CommandManager cmdMgr(50);
    olp::cad::OcctCadKernel cad;

    // 1. Setup scene with a frame node (UFRAME)
    olp::core::SceneNode uframeNode;
    uframeNode.id = "UFRAME_1";
    uframeNode.name = "UFRAME_1";
    uframeNode.type = olp::core::SceneNodeType::Frame;
    uframeNode.localTransform = olp::core::Transform4d::fromTranslation(100.0, 200.0, 300.0);
    doc.sceneNodes.push_back(uframeNode);

    // 2. Setup controller, program, operation, and 2 targets
    olp::core::ControllerUnit cu;
    olp::core::Program prog;
    olp::core::Operation op;
    op.id = "op_gizmo";

    olp::core::Target t1;
    t1.id = "target_gizmo_1";
    t1.name = "T1";
    t1.poseInUFrame = olp::core::Transform4d::fromTranslation(10.0, 20.0, 30.0);

    olp::core::Target t2;
    t2.id = "target_gizmo_2";
    t2.name = "T2";
    t2.poseInUFrame = olp::core::Transform4d::fromTranslation(50.0, 60.0, 70.0);

    op.targets.push_back(t1);
    op.targets.push_back(t2);
    prog.operations.push_back(op);
    cu.programs.push_back(prog);
    doc.controllers.push_back(cu);

    olp::ui::OcctViewport viewport(doc, cmdMgr, &cad);

    // 3. Test mounting gizmo to target_gizmo_1
    const bool mountOk = viewport.setTargetGizmo("target_gizmo_1");
    check(mountOk, "Mount TcpGizmo to target_gizmo_1");
    check(viewport.activeTargetId() == "target_gizmo_1", "activeTargetId matches selected target");

    // 4. Test reference frame switching
    viewport.setGizmoReferenceFrame(olp::ui::GizmoRefFrame::World);
    check(viewport.gizmoReferenceFrame() == olp::ui::GizmoRefFrame::World, "Gizmo reference frame switched to World");
    viewport.setGizmoReferenceFrame(olp::ui::GizmoRefFrame::Tool);
    check(viewport.gizmoReferenceFrame() == olp::ui::GizmoRefFrame::Tool, "Gizmo reference frame switched to Tool");

    // 4b. Test Seam reference frame and in-place rotation
    olp::core::SeamDefinition seam;
    seam.id = "seam_gizmo";
    olp::core::SeamSampleFrame sf0;
    sf0.position = {110.0, 220.0, 330.0}; // world pos: (100,200,300) + (10,20,30)
    sf0.frame = olp::core::Transform4d::fromTranslation(110.0, 220.0, 330.0);
    sf0.frame.rot = olp::core::Mat3::rotZ(olp::core::deg2rad(45.0));
    seam.sampledFrames.push_back(sf0);
    doc.seams.push_back(seam);
    doc.controllers[0].programs[0].operations[0].seamId = "seam_gizmo";

    viewport.setTargetGizmo("target_gizmo_1");
    viewport.setGizmoReferenceFrame(olp::ui::GizmoRefFrame::Seam);
    check(viewport.gizmoReferenceFrame() == olp::ui::GizmoRefFrame::Seam, "Gizmo reference frame switched to Seam");

    // 5. Test snap steps configuration
    viewport.setGizmoSnapSteps(true, 5.0, 2.0);

    // 6. Test interactive drag with zero document mutation
    bool previewFired = false;
    olp::ui::GizmoPreviewState lastPreview{};
    viewport.tcpGizmo().setPreviewCallback([&](const olp::ui::GizmoPreviewState& st) {
        previewFired = true;
        lastPreview = st;
    });

    // In-place rotation in Seam frame: rotating torch angles must NOT cause position to jump or orbit
    const auto initialPoseBeforeRot = doc.findTarget("target_gizmo_1")->poseInUFrame;
    viewport.tcpGizmo().beginDrag({"target_gizmo_1"});
    viewport.tcpGizmo().updateDragDelta(olp::core::Vec3(0.0, 0.0, 0.0), olp::core::Vec3(0.0, 10.0, 0.0));
    check(std::abs(lastPreview.previewPoseInUFrame.pos.x - initialPoseBeforeRot.pos.x) < 1e-6 &&
          std::abs(lastPreview.previewPoseInUFrame.pos.y - initialPoseBeforeRot.pos.y) < 1e-6 &&
          std::abs(lastPreview.previewPoseInUFrame.pos.z - initialPoseBeforeRot.pos.z) < 1e-6,
          "Rotation in Seam frame is in-place (zero orbital translation jump)");
    check(lastPreview.previewPoseInUFrame.rot.angularDistanceRad(initialPoseBeforeRot.rot) > 0.1,
          "Rotation in Seam frame modifies torch orientation");
    viewport.tcpGizmo().cancelDrag();

    // Switch back to Tool frame for linear drag test
    viewport.setGizmoReferenceFrame(olp::ui::GizmoRefFrame::Tool);

    const bool dragStarted = viewport.tcpGizmo().beginDrag({"target_gizmo_1"});
    check(dragStarted && viewport.tcpGizmo().isDragging(), "TcpGizmo drag started");

    viewport.tcpGizmo().updateDragDelta(olp::core::Vec3(15.0, 0.0, 0.0), olp::core::Vec3(0.0, 0.0, 0.0));
    check(previewFired, "Gizmo drag triggers preview callback");
    check(lastPreview.primaryTargetId == "target_gizmo_1", "Preview state matches primary target");

    // Rule 3 verification: zero document mutation during drag
    const auto* docTarget = doc.findTarget("target_gizmo_1");
    check(docTarget != nullptr && std::abs(docTarget->poseInUFrame.pos.x - 10.0) < 1e-6,
          "Zero document mutation during drag (target poseInUFrame remains initial)");
    check(cmdMgr.undoCount() == 0, "No commands pushed during drag");

    // Rule 4 verification: mouse release commits exactly 1 command
    const bool committed = viewport.tcpGizmo().commitDrag();
    check(committed, "Commit drag succeeds");
    check(!viewport.tcpGizmo().isDragging(), "TcpGizmo isDragging is false after commit");
    check(cmdMgr.undoCount() == 1, "Exactly 1 ModifyTargetPoseCommand committed to Undo stack");
    check(std::abs(doc.findTarget("target_gizmo_1")->poseInUFrame.pos.x - 25.0) < 1e-6,
          "Target poseInUFrame updated to new position (X = 25.0 mm)");

    // Test Undo / Redo of gizmo drag
    cmdMgr.undo();
    check(std::abs(doc.findTarget("target_gizmo_1")->poseInUFrame.pos.x - 10.0) < 1e-6,
          "Undo restores target initial pose (X = 10.0 mm)");
    cmdMgr.redo();
    check(std::abs(doc.findTarget("target_gizmo_1")->poseInUFrame.pos.x - 25.0) < 1e-6,
          "Redo re-applies target modified pose (X = 25.0 mm)");

    // 7. Test Esc cancellation restores pose with zero commands
    viewport.tcpGizmo().beginDrag({"target_gizmo_1"});
    viewport.tcpGizmo().updateDragDelta(olp::core::Vec3(100.0, 0.0, 0.0), olp::core::Vec3(0.0, 0.0, 0.0));
    viewport.tcpGizmo().cancelDrag();
    check(!viewport.tcpGizmo().isDragging(), "TcpGizmo isDragging is false after cancel");
    check(cmdMgr.undoCount() == 1, "Undo stack untouched after cancelDrag");
    check(std::abs(doc.findTarget("target_gizmo_1")->poseInUFrame.pos.x - 25.0) < 1e-6,
          "Target pose unchanged after cancelDrag");

    // 8. Test multi-selection drag (Rule 5: exactly 1 MacroCommand)
    viewport.setMultiTargetProvider([&]() {
        return std::vector<std::string>{"target_gizmo_1", "target_gizmo_2"};
    });
    viewport.tcpGizmo().beginDrag({"target_gizmo_1", "target_gizmo_2"});
    viewport.tcpGizmo().updateDragDelta(olp::core::Vec3(0.0, 10.0, 0.0), olp::core::Vec3(0.0, 0.0, 0.0));
    viewport.tcpGizmo().commitDrag();
    check(cmdMgr.undoCount() == 2, "Multi-target drag commits exactly 1 MacroCommand");
    check(std::abs(doc.findTarget("target_gizmo_1")->poseInUFrame.pos.y - 30.0) < 1e-6 &&
          std::abs(doc.findTarget("target_gizmo_2")->poseInUFrame.pos.y - 70.0) < 1e-6,
          "Both targets updated by drag delta (T1.Y = 30.0, T2.Y = 70.0)");

    // Undo multi-target drag rolls back both targets together in 1 step
    cmdMgr.undo();
    check(std::abs(doc.findTarget("target_gizmo_1")->poseInUFrame.pos.y - 20.0) < 1e-6 &&
          std::abs(doc.findTarget("target_gizmo_2")->poseInUFrame.pos.y - 60.0) < 1e-6,
          "Undo multi-target MacroCommand rolls back both targets in 1 step");

    // 9. Test clearing gizmo
    viewport.clearTargetGizmo();
    check(viewport.activeTargetId().empty(), "clearTargetGizmo clears activeTargetId");
}


