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


void testWeldingProcessEventsAndTorchSpinTuning() {
    std::cout << "\n--- Test 16: M09 Welding Process Events, Weld Schedules & Torch Spin Tuning ---\n";

    // 1. Weld Schedule Model & Default Generation Rules (APPROACH=FINE, WELD_START=FINE+ArcStart[n], WELD_VIA=CNT50, WELD_END=FINE+ArcEnd[n], RETRACT=FINE)
    olp::app::ProjectDocument doc;
    check(!doc.weldSchedules.empty(), "ProjectDocument initializes with default weld schedules");
    const auto* sched1 = doc.findWeldSchedule(1);
    check(sched1 != nullptr, "findWeldSchedule(1) finds default Schedule 1");
    if (sched1) {
        check(std::abs(sched1->currentAmps - 180.0) < 1e-4, "Schedule 1 preset current is 180A");
        check(std::abs(sched1->voltageVolts - 22.0) < 1e-4, "Schedule 1 preset voltage is 22V");
        check(std::abs(sched1->weldSpeedCmPerMin - 50.0) < 1e-4, "Schedule 1 welding speed is 50 cm/min");
        check(std::abs(sched1->getWeldSpeedMmPerSec() - (500.0 / 60.0)) < 1e-4, "Schedule 1 mm/s helper returns 8.333 mm/s");
    }
    check(doc.findWeldSchedule(99) == nullptr, "findWeldSchedule(99) returns nullptr for non-existent schedule");

    // Build a seam with 5 sampled frames
    olp::core::SeamDefinition seam;
    seam.id = "seam_m09";
    seam.name = "SeamM09";
    for (int i = 0; i < 5; ++i) {
        olp::core::SeamSampleFrame frame;
        frame.arcLengthMm = i * 25.0;
        frame.position = {i * 25.0, 0.0, 0.0};
        frame.tangentX = {1.0, 0.0, 0.0};
        frame.normalFace1 = {0.0, 1.0, 0.0};
        frame.normalFace2 = {0.0, 0.0, 1.0};
        frame.bisector = {0.0, 0.70710678, 0.70710678};
        frame.frame = olp::core::Transform4d::identity();
        frame.frame.pos = frame.position;
        seam.sampledFrames.push_back(frame);
    }
    doc.seams.push_back(seam);

    olp::core::Operation op;
    op.id = "op_m09";
    op.name = "OpM09";
    op.seamId = seam.id;
    op.genParams.endpointsOnlyForLine = false; // preserve all 5 frames
    op.genParams.weldScheduleNumber = 2;       // Arc Start[2] and Arc End[2]
    op.genParams.weldSpeedMmPerSec = 12.0;
    op.genParams.approachSpeedMmPerSec = 150.0;
    op.genParams.retractSpeedMmPerSec = 180.0;

    olp::algo::SeamAndToolpathAlgo::generateOperationTargets(
        seam, olp::core::Transform4d::identity(), op);

    // 1 APPROACH + 5 Weld + 1 RETRACT = 7 targets
    check(op.targets.size() == 7, "Operation targets size is 7 (APPROACH, START, 3x VIA, END, RETRACT)");
    
    // Check APPROACH
    const auto& tApp = op.targets[0];
    check(tApp.role == olp::core::TargetRole::APPROACH, "Target 0 is APPROACH");
    check(tApp.termination == olp::core::TerminationType::FINE, "APPROACH default termination is FINE");
    check(tApp.events.empty(), "APPROACH has no process events");

    // Check WELD_START
    const auto& tStart = op.targets[1];
    check(tStart.role == olp::core::TargetRole::WELD_START, "Target 1 is WELD_START");
    check(tStart.termination == olp::core::TerminationType::FINE, "WELD_START default termination is FINE");
    check(tStart.events.size() == 1, "WELD_START has 1 process event");
    if (tStart.events.size() == 1) {
        check(tStart.events[0].type == olp::core::ProcessEventType::ArcStart, "WELD_START event is ArcStart");
        check(tStart.events[0].scheduleNumber == 2, "WELD_START ArcStart condition number is 2");
    }

    // Check WELD_VIA intermediate points
    for (size_t i = 2; i <= 4; ++i) {
        const auto& tVia = op.targets[i];
        check(tVia.role == olp::core::TargetRole::WELD_VIA, "Intermediate target is WELD_VIA");
        check(tVia.useWeldSpeed, "WELD_VIA uses weld speed");
        check(tVia.termination == olp::core::TerminationType::CNT, "WELD_VIA termination is CNT");
        check(tVia.cntValue == 50, "WELD_VIA default cntValue is 50");
        check(tVia.events.empty(), "WELD_VIA has no process events");
    }

    // Check WELD_END
    const auto& tEnd = op.targets[5];
    check(tEnd.role == olp::core::TargetRole::WELD_END, "Target 5 is WELD_END");
    check(tEnd.termination == olp::core::TerminationType::FINE, "WELD_END default termination is FINE");
    check(tEnd.useWeldSpeed, "WELD_END uses weld speed");
    check(tEnd.events.size() == 1, "WELD_END has 1 process event");
    if (tEnd.events.size() == 1) {
        check(tEnd.events[0].type == olp::core::ProcessEventType::ArcEnd, "WELD_END event is ArcEnd");
        check(tEnd.events[0].scheduleNumber == 2, "WELD_END ArcEnd condition number is 2");
    }

    // Check RETRACT
    const auto& tRet = op.targets[6];
    check(tRet.role == olp::core::TargetRole::RETRACT, "Target 6 is RETRACT");
    check(tRet.termination == olp::core::TerminationType::FINE, "RETRACT default termination is FINE");
    check(tRet.events.empty(), "RETRACT has no process events");

    // Add operation to document controller
    olp::core::ControllerUnit cu;
    cu.id = "ctrl_m09";
    olp::core::Program prog;
    prog.id = "prog_m09";
    prog.operations.push_back(op);
    cu.programs.push_back(prog);
    doc.controllers.push_back(cu);
    doc.isDirty = false;

    // 2. ModifyTargetProcessParamsCommand & Undo/Redo Symmetry & Zero-Change Gate
    olp::app::CommandManager cmdMgr;
    const std::string viaId = op.targets[2].id;

    olp::app::TargetProcessParams newParams;
    newParams.motion = olp::core::MotionType::J;
    newParams.speedValue = 85.0;
    newParams.termination = olp::core::TerminationType::CNT;
    newParams.cntValue = 80;

    auto modCmd = std::make_unique<olp::app::ModifyTargetProcessParamsCommand>(
        doc, viaId, newParams);
    cmdMgr.executeCommand(std::move(modCmd));

    check(doc.isDirty, "ModifyTargetProcessParamsCommand marks doc.isDirty = true");
    auto* tViaInDoc = doc.findTarget(viaId);
    check(tViaInDoc != nullptr, "Target found in doc");
    if (tViaInDoc) {
        check(tViaInDoc->motion == olp::core::MotionType::J, "Target motion modified to J");
        check(std::abs(tViaInDoc->speedValue - 85.0) < 1e-4, "Target speedValue modified to 85.0");
        check(tViaInDoc->termination == olp::core::TerminationType::CNT, "Target termination modified to CNT");
        check(tViaInDoc->cntValue == 80, "Target cntValue modified to 80");
    }

    // Undo ModifyTargetProcessParamsCommand
    cmdMgr.undo();
    check(!doc.isDirty, "Undo ModifyTargetProcessParamsCommand restores doc.isDirty = false");
    if (tViaInDoc) {
        check(tViaInDoc->motion == olp::core::MotionType::L, "Undo restores target motion to L");
        check(std::abs(tViaInDoc->speedValue - 12.0) < 1e-4, "Undo restores target speedValue to 12.0");
        check(tViaInDoc->cntValue == 50, "Undo restores target cntValue to 50");
    }

    // Redo ModifyTargetProcessParamsCommand
    cmdMgr.redo();
    check(doc.isDirty, "Redo ModifyTargetProcessParamsCommand marks doc.isDirty = true");
    if (tViaInDoc) {
        check(tViaInDoc->motion == olp::core::MotionType::J, "Redo restores target motion to J");
        check(tViaInDoc->cntValue == 80, "Redo restores target cntValue to 80");
    }

    // Zero-change gate
    const size_t undoCountBefore = cmdMgr.undoCount();
    auto noChangeCmd = std::make_unique<olp::app::ModifyTargetProcessParamsCommand>(
        doc, viaId, newParams);
    cmdMgr.executeCommand(std::move(noChangeCmd));
    check(cmdMgr.undoCount() == undoCountBefore, "Zero-change ModifyTargetProcessParamsCommand is not stacked");

    // 3. Partial Range SLERP Orientation Interpolation (UI-001)
    // Create an operation with 6 targets, all initially identity rotation
    olp::core::Operation partialOp;
    partialOp.id = "op_partial";
    partialOp.name = "OpPartial";
    for (int i = 0; i < 6; ++i) {
        olp::core::Target t;
        t.id = "partial_T_" + std::to_string(i);
        t.arcLengthRatio = i * 0.2;
        t.role = (i == 0) ? olp::core::TargetRole::APPROACH :
                 ((i == 1) ? olp::core::TargetRole::WELD_START :
                 ((i == 5) ? olp::core::TargetRole::WELD_END : olp::core::TargetRole::WELD_VIA));
        t.poseInUFrame.rot = olp::core::Mat3::identity();
        t.poseInUFrame.pos = {i * 20.0, 0.0, 0.0};
        partialOp.targets.push_back(t);
    }
    doc.controllers.front().programs.front().operations.push_back(partialOp);
    doc.isDirty = false;

    // Tilt T_4 by 40 degrees around X axis
    auto* pOp = doc.findOperation("op_partial");
    check(pOp != nullptr, "Found op_partial in document");
    if (pOp) {
        pOp->targets[4].poseInUFrame.rot = olp::core::Mat3::rotX(olp::core::deg2rad(40.0));
        pOp->targets[4].userDeltaOffset.rot = olp::core::Mat3::rotX(olp::core::deg2rad(40.0));
        pOp->targets[4].userModified = true;

        // Perform partial SLERP only between T_2 and T_4 (startTargetId = "partial_T_2", endTargetId = "partial_T_4")
        auto partialSlerpCmd = std::make_unique<olp::app::SmoothTargetOrientationsCommand>(
            doc, "op_partial", "partial_T_2", "partial_T_4");
        cmdMgr.executeCommand(std::move(partialSlerpCmd));

        check(doc.isDirty, "Partial SLERP marks doc.isDirty = true");
        
        // T_3 (in range, midway between T_2 [0 deg] and T_4 [40 deg]): expected ~20 deg around X
        const auto& t3 = pOp->targets[3];
        check(t3.userModified, "T_3 inside partial SLERP range is marked userModified = true");
        const auto expectedT3Rot = olp::core::Mat3::rotX(olp::core::deg2rad(20.0));
        const double errT3Deg = olp::core::rad2deg(t3.poseInUFrame.rot.angularDistanceRad(expectedT3Rot));
        check(errT3Deg < 0.1, "T_3 orientation interpolated accurately to midpoint ~20 deg (actual err: " + std::to_string(errT3Deg) + " deg)");

        // Points outside the range (T_0, T_1, T_5) MUST remain untouched!
        const auto& t1 = pOp->targets[1]; // WELD_START (outside range)
        check(!t1.userModified, "T_1 (WELD_START) outside partial SLERP range remains !userModified");
        check(t1.poseInUFrame.rot.angularDistanceRad(olp::core::Mat3::identity()) < 1e-6,
              "T_1 orientation outside partial SLERP range remains strictly unchanged (identity)");

        const auto& t5 = pOp->targets[5]; // WELD_END (outside range)
        check(!t5.userModified, "T_5 (WELD_END) outside partial SLERP range remains !userModified");
        check(t5.poseInUFrame.rot.angularDistanceRad(olp::core::Mat3::identity()) < 1e-6,
              "T_5 orientation outside partial SLERP range remains strictly unchanged (identity)");

        // Undo partial SLERP
        cmdMgr.undo();
        check(!doc.isDirty, "Undo partial SLERP restores clean dirty state");
        check(!pOp->targets[3].userModified, "Undo partial SLERP restores T_3 to !userModified");
        check(pOp->targets[3].poseInUFrame.rot.angularDistanceRad(olp::core::Mat3::identity()) < 1e-6,
              "Undo partial SLERP restores T_3 rotation to identity");

        // Redo partial SLERP
        cmdMgr.redo();
        check(doc.isDirty, "Redo partial SLERP marks doc.isDirty = true");
        check(pOp->targets[3].userModified, "Redo partial SLERP re-marks T_3 as userModified");
    }

    // 4. Torch Spin Tuning (RotateTargetAroundTcpZCommand & rotateTargetAroundTcpZ)
    // Setup target with an arbitrary non-trivial initial 3D pose
    olp::core::Target spinTarget;
    spinTarget.id = "spin_target_1";
    spinTarget.name = "SPIN_TARGET";
    spinTarget.poseInUFrame.pos = {120.0, -80.0, 350.0};
    const olp::core::Mat3 initialRot = olp::core::Mat3::rotX(olp::core::deg2rad(30.0)) *
                                       olp::core::Mat3::rotY(olp::core::deg2rad(-20.0));
    spinTarget.poseInUFrame.rot = initialRot;
    doc.controllers.front().programs.front().operations.front().targets.push_back(spinTarget);
    doc.isDirty = false;

    auto* tSpinInDoc = doc.findTarget("spin_target_1");
    check(tSpinInDoc != nullptr, "Found spin_target_1 in doc");
    if (tSpinInDoc) {
        const olp::core::Vec3 origZ = tSpinInDoc->poseInUFrame.rot.colZ();
        const olp::core::Vec3 origPos = tSpinInDoc->poseInUFrame.pos;

        // Rotate by +45 degrees around TCP Z
        const double spinDeg = 45.0;
        auto spinCmd = std::make_unique<olp::app::RotateTargetAroundTcpZCommand>(
            doc, "spin_target_1", olp::core::deg2rad(spinDeg));
        cmdMgr.executeCommand(std::move(spinCmd));

        check(doc.isDirty, "RotateTargetAroundTcpZCommand marks doc.isDirty = true");
        check(tSpinInDoc->userModified, "Target marked userModified after TCP Z rotation");

        // Mathematical invariant 1: Position does not move at all
        const double posShift = (tSpinInDoc->poseInUFrame.pos - origPos).norm();
        check(posShift < 1e-12, "TCP position strictly unchanged during Z spin (shift: " + std::to_string(posShift) + " mm)");

        // Mathematical invariant 2: Local Z axis in space remains identical
        const olp::core::Vec3 newZ = tSpinInDoc->poseInUFrame.rot.colZ();
        const double zDev = (newZ - origZ).norm();
        check(zDev < 1e-12, "Torch Z axis vector in space remains strictly unchanged (dev: " + std::to_string(zDev) + ")");

        // Mathematical invariant 3: Right-handed orthonormal matrix with determinant strictly 1.0
        const double det = tSpinInDoc->poseInUFrame.rot.determinant();
        check(std::abs(det - 1.0) < 1e-10, "Rotated matrix determinant strictly equals 1.0 (det: " + std::to_string(det) + ")");

        const auto xCol = tSpinInDoc->poseInUFrame.rot.colX();
        const auto yCol = tSpinInDoc->poseInUFrame.rot.colY();
        const auto zCol = tSpinInDoc->poseInUFrame.rot.colZ();
        check(std::abs(xCol.dot(yCol)) < 1e-12 &&
              std::abs(xCol.dot(zCol)) < 1e-12 &&
              std::abs(yCol.dot(zCol)) < 1e-12,
              "Rotated matrix columns strictly orthogonal (dot products < 1e-12)");

        // Mathematical invariant 4: Rotation around Z axis is exactly 45 degrees
        const olp::core::Mat3 dR = initialRot.transposed() * tSpinInDoc->poseInUFrame.rot;
        const double actualSpinDeg = olp::core::rad2deg(std::atan2(dR.m[1][0], dR.m[0][0]));
        check(std::abs(actualSpinDeg - 45.0) < 1e-5, "Actual spin angle is 45.0 deg (actual: " + std::to_string(actualSpinDeg) + " deg)");

        // Undo RotateTargetAroundTcpZCommand
        cmdMgr.undo();
        check(!doc.isDirty, "Undo RotateTargetAroundTcpZCommand restores clean dirty state");
        check(!tSpinInDoc->userModified, "Undo restores userModified to false");
        check(tSpinInDoc->poseInUFrame.rot.angularDistanceRad(initialRot) < 1e-10,
              "Undo restores exact initial orientation");

        // Redo RotateTargetAroundTcpZCommand
        cmdMgr.redo();
        check(doc.isDirty, "Redo RotateTargetAroundTcpZCommand marks doc.isDirty = true");
        check(tSpinInDoc->userModified, "Redo restores userModified = true");

        // Zero-rotation gate
        const size_t spinUndoCount = cmdMgr.undoCount();
        auto zeroSpinCmd = std::make_unique<olp::app::RotateTargetAroundTcpZCommand>(
            doc, "spin_target_1", 0.0);
        cmdMgr.executeCommand(std::move(zeroSpinCmd));
        check(cmdMgr.undoCount() == spinUndoCount, "Zero-angle RotateTargetAroundTcpZCommand is not stacked");
    }

    // 5. Toolpath Regeneration Process Parameter Preservation (M09-003 Regression)
    // Setup targets in an operation with mixed manual edits:
    // Target 1 (WELD_START): process modification (ArcStart schedule = 3)
    // Target 2 (WELD_VIA_1): process modification (CNT = 80, speedValue = 85.0)
    // Target 3 (WELD_VIA_2): pose modification ONLY (Z spin 30 deg, userModified = true, processModified = false)
    // Target 4 (WELD_VIA_3): unmodified
    auto* testOp = doc.findOperation("op_m09");
    check(testOp != nullptr, "Found op_m09 in document for M09-003 regression test");
    if (testOp && testOp->targets.size() >= 5) {
        // Edit Target 1: ArcStart schedule = 3
        olp::app::TargetProcessParams pStartParams;
        pStartParams.events = std::vector<olp::core::ProcessEvent>{{olp::core::ProcessEventType::ArcStart, 3, 0.0, ""}};
        cmdMgr.executeCommand(std::make_unique<olp::app::ModifyTargetProcessParamsCommand>(
            doc, testOp->targets[1].id, pStartParams));
        check(testOp->targets[1].processModified, "WELD_START marked processModified after process param edit");

        // Target 2 was already edited above with CNT = 80 and motion = J, speed = 85.0
        check(testOp->targets[2].processModified, "WELD_VIA_1 marked processModified after earlier edit");

        // Edit Target 3: Pose rotation ONLY (30 deg around TCP Z)
        cmdMgr.executeCommand(std::make_unique<olp::app::RotateTargetAroundTcpZCommand>(
            doc, testOp->targets[3].id, olp::core::deg2rad(30.0)));
        check(testOp->targets[3].userModified, "WELD_VIA_2 marked userModified after TCP Z rotation");
        check(!testOp->targets[3].processModified, "WELD_VIA_2 remains !processModified after pure pose edit");

        // Target 4 remains untouched
        check(!testOp->targets[4].userModified, "WELD_VIA_3 is !userModified");
        check(!testOp->targets[4].processModified, "WELD_VIA_3 is !processModified");

        // Regenerate operation targets using RegenerateOperationTargetsCommand
        cmdMgr.executeCommand(std::make_unique<olp::app::RegenerateOperationTargetsCommand>(
            doc, testOp->id));

        // Invariant 1: WELD_START retained manual process change (schedule = 3, processModified = true)
        check(testOp->targets[1].processModified, "Regenerated WELD_START preserves processModified = true");
        check(testOp->targets[1].events.size() == 1, "Regenerated WELD_START retains 1 event");
        if (testOp->targets[1].events.size() == 1) {
            check(testOp->targets[1].events[0].scheduleNumber == 3,
                  "Regenerated WELD_START preserved manual Schedule 3 (not overwritten by default)");
        }

        // Invariant 2: WELD_VIA_1 retained manual process change (CNT = 80, speed = 85.0, motion = J)
        check(testOp->targets[2].processModified, "Regenerated WELD_VIA_1 preserves processModified = true");
        check(testOp->targets[2].cntValue == 80, "Regenerated WELD_VIA_1 preserved manual CNT 80");
        check(testOp->targets[2].motion == olp::core::MotionType::J, "Regenerated WELD_VIA_1 preserved manual motion J");
        check(std::abs(testOp->targets[2].speedValue - 85.0) < 1e-4, "Regenerated WELD_VIA_1 preserved manual speed 85.0");

        // Invariant 3: WELD_VIA_2 (pose edit only) preserved its pose offset, but its process params WERE rewritten per genParams rules!
        check(testOp->targets[3].userModified, "Regenerated WELD_VIA_2 preserved userModified = true");
        check(!testOp->targets[3].processModified, "Regenerated WELD_VIA_2 has processModified = false");
        check(testOp->targets[3].cntValue == 50,
              "Regenerated WELD_VIA_2 (pose-only edit) has CNT reset to generation rule 50 (not locked)");
        check(testOp->targets[3].motion == olp::core::MotionType::L,
              "Regenerated WELD_VIA_2 (pose-only edit) has motion reset to generation rule L");

        // Invariant 4: WELD_VIA_3 unmodified remains clean
        check(!testOp->targets[4].userModified, "Regenerated WELD_VIA_3 remains !userModified");
        check(!testOp->targets[4].processModified, "Regenerated WELD_VIA_3 remains !processModified");
        check(testOp->targets[4].cntValue == 50, "Regenerated WELD_VIA_3 has default rule CNT 50");
    }

#ifdef OLP_HAS_QT6
    // 6. Speed Cell Text Parsing & Round-trip (M09-001 Regression)
    {
        // Test "WELD_SPEED (80 mm/s)"
        const auto p1 = olp::ui::parseSpeedCellText("WELD_SPEED (80 mm/s)");
        check(p1.valid, "parseSpeedCellText('WELD_SPEED (80 mm/s)') is valid");
        check(p1.useWeldSpeed, "parseSpeedCellText('WELD_SPEED (80 mm/s)') has useWeldSpeed = true");
        check(std::abs(p1.speedValue - 80.0) < 1e-4, "parseSpeedCellText('WELD_SPEED (80 mm/s)') speedValue is 80.0");

        // Test "80 mm/s" -> exits WELD_SPEED (useWeldSpeed = false)
        const auto p2 = olp::ui::parseSpeedCellText("80 mm/s");
        check(p2.valid, "parseSpeedCellText('80 mm/s') is valid");
        check(!p2.useWeldSpeed, "parseSpeedCellText('80 mm/s') has useWeldSpeed = false (exits WELD_SPEED)");
        check(std::abs(p2.speedValue - 80.0) < 1e-4, "parseSpeedCellText('80 mm/s') speedValue is 80.0");

        // Test "50%" -> joint motion speed
        const auto p3 = olp::ui::parseSpeedCellText("50%");
        check(p3.valid, "parseSpeedCellText('50%') is valid");
        check(!p3.useWeldSpeed, "parseSpeedCellText('50%') has useWeldSpeed = false");
        check(std::abs(p3.speedValue - 50.0) < 1e-4, "parseSpeedCellText('50%') speedValue is 50.0");

        // Test "50 cm/min" -> converted to mm/s (50 * 10 / 60 = 8.33333 mm/s)
        const auto p4 = olp::ui::parseSpeedCellText("50 cm/min");
        check(p4.valid, "parseSpeedCellText('50 cm/min') is valid");
        check(!p4.useWeldSpeed, "parseSpeedCellText('50 cm/min') has useWeldSpeed = false");
        check(std::abs(p4.speedValue - (50.0 * 10.0 / 60.0)) < 1e-4,
              "parseSpeedCellText('50 cm/min') converts to 8.333 mm/s");

        // Test bare number "250"
        const auto p5 = olp::ui::parseSpeedCellText("250");
        check(p5.valid, "parseSpeedCellText('250') is valid");
        check(!p5.useWeldSpeed, "parseSpeedCellText('250') has useWeldSpeed = false");
        check(std::abs(p5.speedValue - 250.0) < 1e-4, "parseSpeedCellText('250') speedValue is 250.0");

        // Round-trip test with Target 1: WELD_SPEED mode
        olp::core::Target tgtWeld;
        tgtWeld.motion = olp::core::MotionType::L;
        tgtWeld.speedValue = 80.0;
        tgtWeld.useWeldSpeed = true;
        const QString formattedWeld = olp::ui::formatSpeedCellText(tgtWeld);
        check(formattedWeld == "WELD_SPEED (80 mm/s)", "formatSpeedCellText formats WELD_SPEED correctly");
        const auto parsedWeld = olp::ui::parseSpeedCellText(formattedWeld);
        check(parsedWeld.valid && parsedWeld.useWeldSpeed && std::abs(parsedWeld.speedValue - 80.0) < 1e-4,
              "Round-trip format -> parse preserves WELD_SPEED and speedValue");

        // Round-trip test with Target 2: Explicit linear speed mode
        olp::core::Target tgtLinear;
        tgtLinear.motion = olp::core::MotionType::L;
        tgtLinear.speedValue = 150.0;
        tgtLinear.useWeldSpeed = false;
        const QString formattedLinear = olp::ui::formatSpeedCellText(tgtLinear);
        check(formattedLinear == "150 mm/s", "formatSpeedCellText formats linear speed correctly");
        const auto parsedLinear = olp::ui::parseSpeedCellText(formattedLinear);
        check(parsedLinear.valid && !parsedLinear.useWeldSpeed && std::abs(parsedLinear.speedValue - 150.0) < 1e-4,
              "Round-trip format -> parse preserves linear speed without WELD_SPEED");

        // Round-trip test with Target 3: Joint motion percentage mode
        olp::core::Target tgtJoint;
        tgtJoint.motion = olp::core::MotionType::J;
        tgtJoint.speedValue = 60.0;
        tgtJoint.useWeldSpeed = false;
        const QString formattedJoint = olp::ui::formatSpeedCellText(tgtJoint);
        check(formattedJoint == "60%", "formatSpeedCellText formats joint speed percentage correctly");
        const auto parsedJoint = olp::ui::parseSpeedCellText(formattedJoint);
        check(parsedJoint.valid && !parsedJoint.useWeldSpeed && std::abs(parsedJoint.speedValue - 60.0) < 1e-4,
              "Round-trip format -> parse preserves joint speed percentage");
    }
#endif
}


