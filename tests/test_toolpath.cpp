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

namespace {

olp::core::SeamSampleFrame straightSample(double arcMm) {
    olp::core::SeamSampleFrame frame;
    frame.arcLengthMm = arcMm;
    frame.position = {arcMm, 0.0, 0.0};
    frame.tangentX = {1.0, 0.0, 0.0};
    frame.frame = olp::core::Transform4d::fromTranslation(arcMm, 0.0, 0.0);
    return frame;
}

} // namespace

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
    tStart.cache.valid = true;
    tStart.cache.solvedConfig.flip = true;
    tStart.cache.solvedConfig.turns = {1, 0, -1};

    olp::core::Target tEnd;
    tEnd.name = "WELD_END";
    tEnd.role = olp::core::TargetRole::WELD_END;
    tEnd.motion = olp::core::MotionType::L;
    tEnd.useWeldSpeed = true;
    tEnd.termination = olp::core::TerminationType::FINE;
    tEnd.poseInUFrame = olp::core::Transform4d::fromWPR({450.25, 120.50, 310.00, 180.0, -45.0, 0.0});
    tEnd.events.push_back({olp::core::ProcessEventType::ArcEnd, 1, 0.0, ""});
    tEnd.cache.valid = true;
    tEnd.cache.solvedConfig = tStart.cache.solvedConfig;

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
        lsText.find("CONFIG : 'F U T, 1, 0, -1'") != std::string::npos &&
        lsText.find("HOME joints deg") != std::string::npos &&
        lsText.find("/END") != std::string::npos;

    check(hasSections, "Verify generated .ls contains /PROG, /ATTR, /APPL, /MN, Arc Start/End, /POS, /END");

    olp::core::Program blocked = prog;
    blocked.operations[0].targets[0].cache.valid = false;
    std::string exportError;
    const std::string refused = olp::io::FanucLsPostProcessor::generateLsString(blocked, {}, &exportError);
    check(refused.empty() && !exportError.empty(),
          "Post-processor refuses a target whose IK cache is not valid");

    olp::core::Program circular = prog;
    circular.operations[0].targets[0].motion = olp::core::MotionType::C;
    circular.operations[0].targets[0].cache.valid = true;
    std::string circularError;
    const std::string circularText = olp::io::FanucLsPostProcessor::generateLsString(circular, {}, &circularError);
    check(circularText.empty() && circularError.find("Circular") != std::string::npos,
          "Post-processor refuses to write a circular move as a linear move");
}


void testPathEditsAndCommandGates() {
    std::cout << "\n--- Test 8: Seam events, reverse speeds, and command no-ops ---\n";
    olp::core::SeamDefinition one;
    one.sampledFrames.push_back(straightSample(0.0));
    olp::core::Operation single;
    single.id = "single";
    olp::algo::SeamAndToolpathAlgo::generateOperationTargets(one, olp::core::Transform4d::identity(), single);
    bool hasArcStart = false;
    bool hasArcEnd = false;
    for (const auto& target : single.targets) {
        for (const auto& ev : target.events) {
            hasArcStart = hasArcStart || ev.type == olp::core::ProcessEventType::ArcStart;
            hasArcEnd = hasArcEnd || ev.type == olp::core::ProcessEventType::ArcEnd;
        }
    }
    check(hasArcStart && hasArcEnd, "A one-point seam emits both Arc Start and Arc End");

    olp::core::SeamDefinition seam;
    seam.sampledFrames.push_back(straightSample(0.0));
    seam.sampledFrames.push_back(straightSample(100.0));
    olp::core::Operation clean;
    clean.id = "edit";
    olp::algo::SeamAndToolpathAlgo::generateOperationTargets(seam, olp::core::Transform4d::identity(), clean);
    olp::core::Operation edited = clean;
    for (auto& target : edited.targets) {
        if (target.role == olp::core::TargetRole::APPROACH) {
            target.userModified = true;
            target.userDeltaOffset = olp::core::Transform4d::fromTranslation(80.0, 0.0, 0.0);
        }
    }
    olp::algo::SeamAndToolpathAlgo::generateOperationTargets(seam, olp::core::Transform4d::identity(), edited);
    const olp::core::Target* cleanStart = nullptr;
    const olp::core::Target* editedStart = nullptr;
    const olp::core::Target* cleanApproach = nullptr;
    const olp::core::Target* editedApproach = nullptr;
    for (const auto& target : clean.targets) {
        if (target.role == olp::core::TargetRole::WELD_START) cleanStart = &target;
        if (target.role == olp::core::TargetRole::APPROACH) cleanApproach = &target;
    }
    for (const auto& target : edited.targets) {
        if (target.role == olp::core::TargetRole::WELD_START) editedStart = &target;
        if (target.role == olp::core::TargetRole::APPROACH) editedApproach = &target;
    }
    const bool approachKept = cleanStart && editedStart && cleanApproach && editedApproach &&
        (editedStart->poseInUFrame.pos - cleanStart->poseInUFrame.pos).norm() < 1e-6 &&
        (editedApproach->poseInUFrame.pos - cleanApproach->poseInUFrame.pos).norm() > 1.0;
    check(approachKept, "An approach edit is not copied onto WELD_START");

    olp::app::ProjectDocument doc;
    olp::core::ControllerUnit cu;
    olp::core::Program prog;
    olp::core::Operation op;
    op.id = "rev";
    auto makeTarget = [](olp::core::TargetRole role, const char* name, olp::core::MotionType motion,
                         double speed, bool weldSpeed) {
        olp::core::Target target;
        target.role = role;
        target.name = name;
        target.motion = motion;
        target.speedValue = speed;
        target.useWeldSpeed = weldSpeed;
        return target;
    };
    op.targets.push_back(makeTarget(olp::core::TargetRole::APPROACH, "APPROACH", olp::core::MotionType::J, 50.0, false));
    op.targets.push_back(makeTarget(olp::core::TargetRole::WELD_START, "WELD_START", olp::core::MotionType::L, 200.0, false));
    op.targets.push_back(makeTarget(olp::core::TargetRole::WELD_END, "WELD_END", olp::core::MotionType::L, 10.0, true));
    op.targets.push_back(makeTarget(olp::core::TargetRole::RETRACT, "RETRACT", olp::core::MotionType::L, 200.0, false));
    prog.operations.push_back(op);
    cu.programs.push_back(prog);
    doc.controllers.push_back(cu);

    olp::app::CommandManager commands;
    commands.executeCommand(std::make_unique<olp::app::ReverseOperationPathCommand>(doc, "rev"));
    const olp::core::Target* reversedEnd = nullptr;
    const olp::core::Target* reversedApproach = nullptr;
    for (const auto& target : doc.controllers[0].programs[0].operations[0].targets) {
        if (target.role == olp::core::TargetRole::WELD_END) reversedEnd = &target;
        if (target.role == olp::core::TargetRole::APPROACH) reversedApproach = &target;
    }
    check(reversedEnd && reversedEnd->useWeldSpeed && reversedApproach &&
          reversedApproach->motion == olp::core::MotionType::J &&
          std::abs(reversedApproach->speedValue - 50.0) < 1e-6,
          "Reversing a path keeps weld speed on the weld and joint motion on the approach");

    commands.executeCommand(std::make_unique<olp::app::ModifyTargetPoseCommand>(
        doc, "missing-target", olp::core::Transform4d::identity()));
    check(commands.undoCount() == 1, "A command that does not change a target is not stacked");

    olp::ui::MainWindow win;
    olp::ui::TcpGizmo gizmo(win.document(), win.commandManager());
    olp::core::Target dragTarget;
    dragTarget.id = "drag";
    dragTarget.poseInUFrame = olp::core::Transform4d::fromTranslation(10.0, 0.0, 0.0);
    win.document().controllers.push_back({});
    win.document().controllers[0].programs.push_back({});
    win.document().controllers[0].programs[0].operations.push_back({});
    win.document().controllers[0].programs[0].operations[0].targets.push_back(dragTarget);
    gizmo.setSnapSteps(false, 1.0, 1.0);
    gizmo.beginDrag({"drag"});
    gizmo.updateDragDelta({0.0, 0.0, 0.0}, {0.0, 0.0, 0.0});
    check(!gizmo.commitDrag() && win.commandManager().undoCount() == 0,
          "A zero-delta gizmo drag does not commit");

    gizmo.setUFrameInWorld(olp::core::Transform4d::fromWPR({0, 0, 0, 0, 0, 90}));
    olp::core::Vec3 worldPreview{};
    olp::core::Vec3 uframePreview{};
    gizmo.setReferenceFrame(olp::ui::GizmoRefFrame::World);
    gizmo.setPreviewCallback([&](const olp::ui::GizmoPreviewState& state) {
        worldPreview = state.previewPoseInUFrame.pos;
    });
    gizmo.beginDrag({"drag"});
    gizmo.updateDragDelta({20.0, 0.0, 0.0}, {0.0, 0.0, 0.0});
    gizmo.setPreviewCallback(nullptr);
    gizmo.cancelDrag();
    gizmo.setReferenceFrame(olp::ui::GizmoRefFrame::UFrame);
    gizmo.setPreviewCallback([&](const olp::ui::GizmoPreviewState& state) {
        uframePreview = state.previewPoseInUFrame.pos;
    });
    gizmo.beginDrag({"drag"});
    gizmo.updateDragDelta({20.0, 0.0, 0.0}, {0.0, 0.0, 0.0});
    gizmo.setPreviewCallback(nullptr);
    gizmo.cancelDrag();
    check((worldPreview - uframePreview).norm() > 1.0,
          "World and UFrame drags differ when the UFRAME is rotated",
          "world=(" + std::to_string(worldPreview.x) + "," + std::to_string(worldPreview.y) +
          ") uframe=(" + std::to_string(uframePreview.x) + "," + std::to_string(uframePreview.y) + ")");
}


void testTangentContinuousEdgeChaining() {
    std::cout << "\n--- Test 14: Tangent-Continuous Edge Chaining & Multi-Segment Seam Generation (M07) ---\n";
#ifdef OLP_HAS_OCCT
    olp::cad::OcctCadKernel cad;

    // 1. SquareTube: Multi-segment straight + curved fillet corner edge chaining
    const std::string squareTubePath =
        std::string(OLP_SOURCE_DIR) + "/SampleScenes/Scene2_Full_Workcell_STEP/Workpiece_SquareTube_80x80x5.step";
    std::vector<olp::core::SceneNode> tubeNodes;
    bool okTube = cad.importStep(squareTubePath, tubeNodes);
    check(okTube && !tubeNodes.empty(), "Import Workpiece_SquareTube_80x80x5.step");

    int tubeCadHandle = tubeNodes.front().cadHandleId;
    const auto tubeEdges = cad.getEdges(tubeCadHandle);
    check(tubeEdges.size() > 0, "SquareTube has CAD edges", "count=" + std::to_string(tubeEdges.size()));

    // Search for an edge with tangent-continuous fillet corners (straight + arc transition)
    int seedEdgeIdx = -1;
    olp::cad::EdgeChainResult tubeChain;
    for (size_t i = 0; i < tubeEdges.size(); ++i) {
        auto chain = cad.findTangentContinuousEdgeChain(tubeCadHandle, static_cast<int>(i), 15.0);
        if (chain.edges.size() > 1) {
            seedEdgeIdx = static_cast<int>(i);
            tubeChain = chain;
            break;
        }
    }
    check(seedEdgeIdx >= 0 && tubeChain.edges.size() > 1,
          "SquareTube finds tangent-continuous edge chain (> 1 edge)",
          "edges=" + std::to_string(tubeChain.edges.size()) + ", totalLen=" + std::to_string(tubeChain.totalLengthMm));

    // Verify chain length equals sum of chained edges
    double sumEdgeLen = 0.0;
    for (const auto& ce : tubeChain.edges) {
        sumEdgeLen += ce.lengthMm;
    }
    check(std::abs(tubeChain.totalLengthMm - sumEdgeLen) < 1e-4,
          "Chain total length equals sum of chained edge lengths");

    // 2. Pipe Flange: Circular closed loop detection
    const std::string flangePath =
        std::string(OLP_SOURCE_DIR) + "/SampleScenes/Scene1_TJoint_And_FANUC_M10iA/Workpiece_PipeFlange_DN15.step";
    std::vector<olp::core::SceneNode> flangeNodes;
    bool okFlange = cad.importStep(flangePath, flangeNodes);
    check(okFlange && !flangeNodes.empty(), "Import Workpiece_PipeFlange_DN15.step");

    int flangeCadHandle = flangeNodes.front().cadHandleId;
    const auto flangeEdges = cad.getEdges(flangeCadHandle);
    check(flangeEdges.size() > 0, "Pipe Flange has CAD edges", "count=" + std::to_string(flangeEdges.size()));

    bool foundClosed = false;
    bool foundDynamicRadialBisector = false;
    olp::cad::EdgeChainResult flangeChain;
    std::vector<int> flangeWpHandles = {flangeCadHandle};
    for (size_t i = 0; i < flangeEdges.size(); ++i) {
        auto chain = cad.findTangentContinuousEdgeChain(flangeCadHandle, static_cast<int>(i), 30.0);
        if (chain.isClosed) {
            if (!foundClosed) {
                foundClosed = true;
                flangeChain = chain;
            }
            auto candSeamOpt = olp::algo::SeamAndToolpathAlgo::buildSeamFromEdgeChain(
                cad, flangeCadHandle, chain, flangeWpHandles, 10.0, 0.0, 0.0);
            if (candSeamOpt.has_value()) {
                double minBisX = 1e9, maxBisX = -1e9;
                for (const auto& sf : candSeamOpt->sampledFrames) {
                    minBisX = std::min(minBisX, sf.bisector.x);
                    maxBisX = std::max(maxBisX, sf.bisector.x);
                }
                if (maxBisX - minBisX > 0.5) {
                    foundDynamicRadialBisector = true;
                    flangeChain = chain;
                    break;
                }
            }
        }
    }
    check(foundClosed, "Pipe Flange detects closed loop edge chain (isClosed = true)");

    auto flangeSeamOpt = olp::algo::SeamAndToolpathAlgo::buildSeamFromEdgeChain(
        cad, flangeCadHandle, flangeChain, flangeWpHandles, 10.0, 0.0, 0.0);
    check(flangeSeamOpt.has_value(), "buildSeamFromEdgeChain succeeds for Pipe Flange closed loop");
    if (flangeSeamOpt.has_value()) {
        const auto& fseam = *flangeSeamOpt;
        check(fseam.isClosed, "Pipe Flange SeamDefinition preserves isClosed = true");
        check(fseam.sampledFrames.size() >= 4, "Pipe Flange circular seam has multiple sampled frames");
        double minX = 1e9, maxX = -1e9;
        for (const auto& sf : fseam.sampledFrames) {
            minX = std::min(minX, sf.position.x);
            maxX = std::max(maxX, sf.position.x);
        }
        check(maxX - minX > 5.0, "Pipe Flange circular seam positions span true 3D diameter (not collapsed)");
        check(foundDynamicRadialBisector,
              "Pipe Flange cylindrical/conical flank normal & bisector rotate dynamically around circumference");
    }

    // 3. Composite seam definition build
    std::vector<int> wpHandles = {tubeCadHandle};
    auto seamOpt = olp::algo::SeamAndToolpathAlgo::buildSeamFromEdgeChain(
        cad, tubeCadHandle, tubeChain, wpHandles, 20.0, 0.0, 0.0);
    check(seamOpt.has_value(), "buildSeamFromEdgeChain succeeds for SquareTube chain");
    if (seamOpt.has_value()) {
        const auto& seam = *seamOpt;
        check(seam.edgeChain.size() == tubeChain.edges.size(),
              "Composite seam contains all edges in chain",
              "count=" + std::to_string(seam.edgeChain.size()));
        check(seam.sampledFrames.size() >= 3,
              "Composite seam has multiple sampled frames across multi-edge profile",
              "frames=" + std::to_string(seam.sampledFrames.size()));

        bool monotonic = true;
        bool validOrthonormal = true;
        for (size_t i = 0; i < seam.sampledFrames.size(); ++i) {
            if (i > 0 && seam.sampledFrames[i].arcLengthMm <= seam.sampledFrames[i - 1].arcLengthMm) {
                monotonic = false;
            }
            const auto& rot = seam.sampledFrames[i].frame.rot;
            const auto cX = rot.colX();
            const auto cY = rot.colY();
            const auto cZ = rot.colZ();
            const double det = cX.dot(cY.cross(cZ));
            if (std::abs(det - 1.0) > 1e-3) {
                validOrthonormal = false;
            }
        }
        check(monotonic, "Sampled frames have strictly increasing cumulative arc lengths");
        check(validOrthonormal, "Sampled frames all have orthonormal right-handed rotation matrices (det ~ 1)");
    }

    // 4. CreateSeamFromPickedEdgeCommand with chaining + Undo/Redo
    olp::app::ProjectDocument doc;
    olp::app::CommandManager cmdMgr;
    for (const auto& n : tubeNodes) {
        doc.sceneNodes.push_back(n);
    }
    check(doc.seams.empty(), "Document initially has no seams");

    auto cmd = std::make_unique<olp::app::CreateSeamFromPickedEdgeCommand>(
        doc, cad, tubeCadHandle, seedEdgeIdx, 20.0, true, 15.0);
    cmdMgr.executeCommand(std::move(cmd));

    check(doc.seams.size() == 1, "CreateSeamFromPickedEdgeCommand with chaining adds 1 seam");
    check(doc.isDirty, "Document marked dirty after seam creation");
    check(doc.seams.front().edgeChain.size() == tubeChain.edges.size(),
          "Created seam edgeChain matches detected edge chain size");

    cmdMgr.undo();
    check(doc.seams.empty(), "Undo removes created chained seam");
    check(!doc.isDirty, "Undo restores clean dirty state");

    cmdMgr.redo();
    check(doc.seams.size() == 1, "Redo restores created chained seam");
    check(doc.isDirty, "Redo marks document dirty again");

    // Verify duplicate pick on same chain is rejected as no-op to preserve Undo symmetry
    auto dupCmd = std::make_unique<olp::app::CreateSeamFromPickedEdgeCommand>(
        doc, cad, tubeCadHandle, seedEdgeIdx, 20.0, true, 15.0);
    cmdMgr.executeCommand(std::move(dupCmd));
    check(doc.seams.size() == 1, "Duplicate pick on existing seam chain is ignored (no duplicate seam)");
    cmdMgr.undo();
    check(doc.seams.empty(), "Single Undo cleanly removes the seam after duplicate pick no-op");
#endif
}


void testSeamToOperationAndSlerpStepping() {
    std::cout << "\n--- Test 15: M08 Seam-to-Operation Generation, SLERP Orientation Smoothing & Target Stepping ---\n";

    // 1. Verify single straight line thins to 4 points vs closed ring / composite chain retains all points
    olp::core::SeamDefinition seamLine;
    seamLine.id = "seam_line";
    seamLine.name = "StraightLineSeam";
    seamLine.isClosed = false;
    olp::core::TopologicalSignature sigLine;
    sigLine.topologyIndex = 1;
    seamLine.edgeChain = {sigLine};
    for (int i = 0; i < 5; ++i) {
        seamLine.sampledFrames.push_back(straightSample(i * 25.0)); // 0, 25, 50, 75, 100 mm
    }

    olp::core::Operation opLine;
    opLine.id = "op_line";
    opLine.genParams.endpointsOnlyForLine = true;
    opLine.genParams.approachDistMm = 40.0;
    opLine.genParams.retractDistMm = 35.0;
    olp::algo::SeamAndToolpathAlgo::generateOperationTargets(seamLine, olp::core::Transform4d::identity(), opLine);

    check(opLine.targets.size() == 4,
          "Single straight line with endpointsOnlyForLine=true generates exactly 4 points",
          "count=" + std::to_string(opLine.targets.size()));
    if (opLine.targets.size() == 4) {
        check(opLine.targets[0].role == olp::core::TargetRole::APPROACH &&
              opLine.targets[1].role == olp::core::TargetRole::WELD_START &&
              opLine.targets[2].role == olp::core::TargetRole::WELD_END &&
              opLine.targets[3].role == olp::core::TargetRole::RETRACT,
              "Target roles for line: APPROACH, WELD_START, WELD_END, RETRACT");
        const double appDist = (opLine.targets[0].poseInUFrame.pos -
            (opLine.targets[1].poseInUFrame.pos - opLine.targets[1].poseInUFrame.rot.colZ() * 40.0)).norm();
        const double retDist = (opLine.targets[3].poseInUFrame.pos -
            (opLine.targets[2].poseInUFrame.pos - opLine.targets[2].poseInUFrame.rot.colZ() * 35.0)).norm();
        check(appDist < 1e-4 && retDist < 1e-4,
              "APPROACH and RETRACT retreat along -Z_tcp by approachDistMm and retractDistMm");
    }

    // Closed loop seam
    olp::core::SeamDefinition seamClosed;
    seamClosed.id = "seam_closed";
    seamClosed.name = "ClosedRingSeam";
    seamClosed.isClosed = true;
    for (int i = 0; i < 8; ++i) {
        const double angle = i * (2.0 * olp::core::kPi / 8.0);
        olp::core::SeamSampleFrame sf;
        sf.arcLengthMm = i * 20.0;
        sf.position = {50.0 * std::cos(angle), 50.0 * std::sin(angle), 0.0};
        sf.tangentX = {-std::sin(angle), std::cos(angle), 0.0};
        sf.frame = olp::core::Transform4d::fromTranslation(sf.position.x, sf.position.y, sf.position.z);
        seamClosed.sampledFrames.push_back(sf);
    }

    olp::core::Operation opClosed;
    opClosed.id = "op_closed";
    opClosed.genParams.endpointsOnlyForLine = true;
    olp::algo::SeamAndToolpathAlgo::generateOperationTargets(seamClosed, olp::core::Transform4d::identity(), opClosed);

    check(opClosed.targets.size() == 10,
          "Closed ring seam preserves all 8 sampled points (1 APPROACH + 8 weld + 1 RETRACT = 10 targets)",
          "count=" + std::to_string(opClosed.targets.size()));
    bool hasVia = false;
    for (const auto& t : opClosed.targets) {
        if (t.role == olp::core::TargetRole::WELD_VIA) hasVia = true;
    }
    check(hasVia, "Closed ring seam contains WELD_VIA intermediate targets sequence");

    // Multi-edge chain seam
    olp::core::SeamDefinition seamChain;
    seamChain.id = "seam_chain";
    seamChain.name = "MultiEdgeChainSeam";
    seamChain.isClosed = false;
    olp::core::TopologicalSignature sig1, sig2;
    sig1.topologyIndex = 1; sig2.topologyIndex = 2;
    seamChain.edgeChain = {sig1, sig2};
    for (int i = 0; i < 5; ++i) {
        seamChain.sampledFrames.push_back(straightSample(i * 20.0));
    }
    olp::core::Operation opChain;
    opChain.id = "op_chain";
    opChain.genParams.endpointsOnlyForLine = true;
    olp::algo::SeamAndToolpathAlgo::generateOperationTargets(seamChain, olp::core::Transform4d::identity(), opChain);
    check(opChain.targets.size() == 7,
          "Multi-edge chain seam preserves all sampled points (not thinned by endpointsOnlyForLine)",
          "count=" + std::to_string(opChain.targets.size()));

    // 2. GenerateOperationFromSeamCommand Undo/Redo and dirty symmetry
    olp::app::ProjectDocument doc;
    olp::app::CommandManager cmdMgr;
    doc.seams.push_back(seamLine);
    doc.isDirty = false;

    check(!doc.isDirty && !doc.seams.front().programmed && doc.controllers.empty(),
          "Initial state: clean dirty state, programmed=false, controllers empty");

    auto genCmd = std::make_unique<olp::app::GenerateOperationFromSeamCommand>(doc, seamLine.id);
    const std::string createdOpId = genCmd->operationId();
    cmdMgr.executeCommand(std::move(genCmd));

    check(doc.isDirty, "GenerateOperationFromSeamCommand marks doc.isDirty = true");
    check(doc.seams.front().programmed, "seam.programmed marked true after generation");
    check(!doc.controllers.empty() && !doc.controllers.front().programs.empty(),
          "Controller and program created automatically");
    const auto* createdOp = doc.findOperation(createdOpId);
    check(createdOp != nullptr && createdOp->targets.size() == 4,
          "Created operation has 4 targets for straight line seam");

    // Undo
    cmdMgr.undo();
    check(!doc.isDirty, "Undo restores doc.isDirty = false");
    check(!doc.seams.front().programmed, "Undo restores seam.programmed = false");
    check(doc.findOperation(createdOpId) == nullptr, "Undo removes created operation");

    // Redo
    cmdMgr.redo();
    check(doc.isDirty, "Redo marks doc.isDirty = true");
    check(doc.seams.front().programmed, "Redo restores seam.programmed = true");
    check(doc.findOperation(createdOpId) != nullptr, "Redo restores created operation");

    // 3. SLERP Orientation Interpolation & SmoothTargetOrientationsCommand
    olp::core::Operation slerpOp;
    slerpOp.id = "slerp_op";
    slerpOp.name = "SlerpOp";
    slerpOp.seamId = seamLine.id;

    for (int i = 0; i < 5; ++i) {
        olp::core::Target t;
        t.id = "slerp_T_" + std::to_string(i);
        t.arcLengthRatio = i * 0.25;
        t.role = (i == 0) ? olp::core::TargetRole::WELD_START :
                 ((i == 4) ? olp::core::TargetRole::WELD_END : olp::core::TargetRole::WELD_VIA);
        t.poseInUFrame.rot = olp::core::Mat3::identity();
        t.poseInUFrame.pos = {i * 25.0, 0.0, 0.0};
        slerpOp.targets.push_back(t);
    }
    doc.controllers.front().programs.front().operations.push_back(slerpOp);
    doc.isDirty = false;

    // Tilt end target by 40 degrees around X axis
    const double tiltAngleDeg = 40.0;
    auto* opInDoc = doc.findOperation("slerp_op");
    check(opInDoc != nullptr, "Found slerp_op in document");
    if (opInDoc != nullptr) {
        opInDoc->targets.back().poseInUFrame.rot = olp::core::Mat3::rotX(olp::core::deg2rad(tiltAngleDeg));
        opInDoc->targets.back().userDeltaOffset.rot = olp::core::Mat3::rotX(olp::core::deg2rad(tiltAngleDeg));
        opInDoc->targets.back().userModified = true;

        auto smoothCmd = std::make_unique<olp::app::SmoothTargetOrientationsCommand>(
            doc, "slerp_op", opInDoc->targets.front().id, opInDoc->targets.back().id);
        cmdMgr.executeCommand(std::move(smoothCmd));

        check(doc.isDirty, "SmoothTargetOrientationsCommand marks document dirty");

        const auto& tMid = opInDoc->targets[2]; // ratio 0.5 -> expected 20 deg
        const auto& tQ1 = opInDoc->targets[1];  // ratio 0.25 -> expected 10 deg
        const auto& tQ3 = opInDoc->targets[3];  // ratio 0.75 -> expected 30 deg

        check(tMid.userModified && tQ1.userModified && tQ3.userModified,
              "Intermediate targets marked userModified = true after SLERP");

        const auto expectedMidRot = olp::core::Mat3::rotX(olp::core::deg2rad(20.0));
        const double midErrDeg = olp::core::rad2deg(tMid.poseInUFrame.rot.angularDistanceRad(expectedMidRot));
        check(midErrDeg < 0.1,
              "Midpoint orientation error < 0.1 deg (actual: " + std::to_string(midErrDeg) + " deg)");

        const auto expectedQ1Rot = olp::core::Mat3::rotX(olp::core::deg2rad(10.0));
        const double q1ErrDeg = olp::core::rad2deg(tQ1.poseInUFrame.rot.angularDistanceRad(expectedQ1Rot));
        const auto expectedQ3Rot = olp::core::Mat3::rotX(olp::core::deg2rad(30.0));
        const double q3ErrDeg = olp::core::rad2deg(tQ3.poseInUFrame.rot.angularDistanceRad(expectedQ3Rot));
        check(q1ErrDeg < 0.1 && q3ErrDeg < 0.1,
              "Intermediate targets scale monotonically with arcLengthRatio (10 deg and 30 deg)");

        // Undo SLERP
        cmdMgr.undo();
        check(!opInDoc->targets[2].userModified,
              "Undo restores intermediate targets to un-modified state");
        check(doc.isDirty == false, "Undo restores clean dirty state");

        // Redo SLERP
        cmdMgr.redo();
        check(opInDoc->targets[2].userModified,
              "Redo re-applies SLERP interpolation to intermediate targets");
        check(doc.isDirty == true, "Redo marks document dirty again");
    }

    // M08-003 regression: Zero-change SLERP does not pollute Undo stack or userModified
    olp::core::Operation zeroOp;
    zeroOp.id = "zero_slerp_op";
    zeroOp.seamId = seamLine.id;
    for (int i = 0; i < 4; ++i) {
        olp::core::Target t;
        t.id = "zero_T_" + std::to_string(i);
        t.arcLengthRatio = i / 3.0;
        t.role = (i == 0) ? olp::core::TargetRole::WELD_START :
                 ((i == 3) ? olp::core::TargetRole::WELD_END : olp::core::TargetRole::WELD_VIA);
        t.poseInUFrame.rot = olp::core::Mat3::identity();
        zeroOp.targets.push_back(t);
    }
    doc.controllers.front().programs.front().operations.push_back(zeroOp);
    const size_t undoCountBeforeZeroSlerp = cmdMgr.undoCount();
    auto noChangeCmd = std::make_unique<olp::app::SmoothTargetOrientationsCommand>(
        doc, "zero_slerp_op");
    cmdMgr.executeCommand(std::move(noChangeCmd));
    check(cmdMgr.undoCount() == undoCountBeforeZeroSlerp,
          "Zero-change SLERP command is rejected by didMutate() gate and not stacked");
    const auto* zeroOpCheck = doc.findOperation("zero_slerp_op");
    check(zeroOpCheck != nullptr && !zeroOpCheck->targets[1].userModified && !zeroOpCheck->targets[2].userModified,
          "Intermediate targets remain !userModified after zero-change SLERP");

    // M08-004 regression: RegenerateOperationTargetsCommand undo restores seam.programmed and doc.isDirty
    olp::core::SeamDefinition seamRegen = seamLine;
    seamRegen.id = "seam_regen";
    seamRegen.programmed = false;
    doc.seams.push_back(seamRegen);
    doc.isDirty = false;

    olp::core::Operation opRegen;
    opRegen.id = "op_regen";
    opRegen.seamId = "seam_regen";
    doc.controllers.front().programs.front().operations.push_back(opRegen);

    const size_t undoCountBeforeRegen = cmdMgr.undoCount();
    auto regenCmd = std::make_unique<olp::app::RegenerateOperationTargetsCommand>(doc, "op_regen");
    cmdMgr.executeCommand(std::move(regenCmd));

    check(cmdMgr.undoCount() == undoCountBeforeRegen + 1, "RegenerateOperationTargetsCommand executed and stacked");
    check(doc.isDirty, "RegenerateOperationTargetsCommand marks doc.isDirty = true");
    check(doc.findSeam("seam_regen")->programmed, "RegenerateOperationTargetsCommand marks seam.programmed = true");

    cmdMgr.undo();
    check(!doc.isDirty, "Undo RegenerateOperationTargetsCommand restores doc.isDirty = false");
    check(!doc.findSeam("seam_regen")->programmed, "Undo RegenerateOperationTargetsCommand restores seam.programmed = false");

    cmdMgr.redo();
    check(doc.isDirty, "Redo RegenerateOperationTargetsCommand re-marks doc.isDirty = true");
    check(doc.findSeam("seam_regen")->programmed, "Redo RegenerateOperationTargetsCommand restores seam.programmed = true");

    // 4. Target stepping navigation & OcctViewport linkage & IK preview
    olp::ui::MainWindow win;
    olp::core::ControllerUnit winCu;
    winCu.id = "cu_win";
    olp::core::Program winProg;
    winProg.id = "prog_win";
    olp::core::Operation winOp;
    winOp.id = "op_step";
    for (int i = 0; i < 3; ++i) {
        olp::core::Target t;
        t.id = "step_T_" + std::to_string(i + 1);
        t.name = "P" + std::to_string(i + 1);
        if (i == 1) {
            t.cache.valid = true;
            t.cache.jointsRad = {0.15, -0.25, 0.35, -0.45, 0.55, -0.65};
        }
        winOp.targets.push_back(t);
    }
    winProg.operations.push_back(winOp);

    // M08-005 regression: second operation to verify cross-operation selectTarget resolution
    olp::core::Operation winOp2;
    winOp2.id = "op_step2";
    for (int i = 0; i < 2; ++i) {
        olp::core::Target t;
        t.id = "op2_T_" + std::to_string(i + 1);
        t.name = "OP2_P" + std::to_string(i + 1);
        winOp2.targets.push_back(t);
    }
    winProg.operations.push_back(winOp2);
    winCu.programs.push_back(winProg);
    win.document().controllers.push_back(winCu);

    olp::core::SceneNode robotNode;
    robotNode.id = "robot_node";
    robotNode.type = olp::core::SceneNodeType::Robot;
    win.document().sceneNodes.push_back(robotNode);

    olp::ui::OcctViewport viewport(win.document(), win.commandManager(), &win.cadKernel());
    viewport.attachMainWindow(&win);

    win.selectOperation("op_step");

    // Step to first target
    bool step1 = win.stepTarget(true);
    check(step1 && win.selectedTargetId() == "step_T_1",
          "stepTarget(true) from none selects first target (step_T_1)");
    check(viewport.activeTargetId() == "step_T_1",
          "Viewport activeTargetId links to win.selectedTargetId() (step_T_1)");

    // Step to second target (with IK valid cache)
    bool step2 = win.stepTarget(true);
    check(step2 && win.selectedTargetId() == "step_T_2",
          "stepTarget(true) advances to second target (step_T_2)");
    check(viewport.activeTargetId() == "step_T_2",
          "Viewport activeTargetId advances to step_T_2");

    const auto* rNode = win.document().findSceneNode("robot_node");
    check(rNode != nullptr && std::abs(rNode->currentJointsRad[0] - 0.15) < 1e-4 &&
          std::abs(rNode->currentJointsRad[4] - 0.55) < 1e-4,
          "Selecting target with valid IK cache updates robot sceneNode joint angles");

    // Step back to first target
    bool stepBack = win.stepTarget(false);
    check(stepBack && win.selectedTargetId() == "step_T_1",
          "stepTarget(false) navigates back to previous target (step_T_1)");
    check(viewport.activeTargetId() == "step_T_1",
          "Viewport activeTargetId synchronizes back to step_T_1");

    // M08-005 regression check: cross-operation target selection with default operationId
    win.selectTarget("op2_T_2");
    check(win.selectedTargetId() == "op2_T_2", "selectTarget successfully updates selectedTargetId to op2_T_2");
    check(win.selectedOperationId() == "op_step2",
          "selectTarget with default operationId correctly resolves owning operationId to op_step2");
    check(viewport.activeTargetId() == "op2_T_2",
          "Viewport activeTargetId synchronizes to op2_T_2 across operations");
}


