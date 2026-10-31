#include "test_support.h"

#include "olp/io/ProjectSerializer.h"
#include "olp/io/Utf8Path.h"
#include "olp/core/MathTypes.h"
#include "olp/core/ProjectModel.h"
#include "olp/ui/MainWindow.h"

#ifdef OLP_HAS_QT6
#include "olp/ui/ShellWindow.h"
#include <QApplication>
#include <QCloseEvent>
#endif

#include <iostream>
#include <fstream>
#include <filesystem>
#include <cmath>

namespace {

using namespace olp;

bool isNear(double a, double b, double eps = 1e-9) {
    return std::abs(a - b) <= eps;
}

bool isNear(const core::Vec3& a, const core::Vec3& b, double eps = 1e-9) {
    return isNear(a.x, b.x, eps) && isNear(a.y, b.y, eps) && isNear(a.z, b.z, eps);
}

bool isNear(const core::Transform4d& a, const core::Transform4d& b, double eps = 1e-9) {
    if (!isNear(a.pos, b.pos, eps)) return false;
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            if (!isNear(a.rot.m[r][c], b.rot.m[r][c], eps)) return false;
        }
    }
    return true;
}

} // namespace

void testProjectJsonSerialization() {
    std::cout << "\n--- Test 22: M14 ProjectDocument JSON Serialization & Deserialization ---\n";

    // 1. Math types round-trip fidelity
    {
        core::Transform4d original;
        original.pos = {123.456789, -987.654321, 555.123456};
        original.rot = core::Mat3::rotZ(core::deg2rad(45.0)) * core::Mat3::rotY(core::deg2rad(30.0));

        app::ProjectDocument doc;
        core::SceneNode node;
        node.id = "test_transform_node";
        node.name = "TransformTest";
        node.localTransform = original;
        doc.sceneNodes.push_back(node);

        const std::string jsonStr = io::ProjectSerializer::toJsonString(doc);
        app::ProjectDocument loadedDoc;
        std::string err;
        const bool ok = io::ProjectSerializer::fromJsonString(jsonStr, loadedDoc, &err);

        check(ok, "M14-001: JSON deserialize succeeded for Transform4d", err);
        check(loadedDoc.sceneNodes.size() == 1, "M14-001: SceneNode count restored");
        if (loadedDoc.sceneNodes.size() == 1) {
            const auto& restored = loadedDoc.sceneNodes[0].localTransform;
            check(isNear(restored, original, 1e-9),
                  "M14-001: Transform4d matrix 100% precision restored (< 1e-9)");
        }
    }

    // 2. Gimbal Lock Pose Round-trip (P = +90 deg)
    {
        core::WprPose wpr{100.0, 200.0, 300.0, 30.0, 90.0, 0.0};
        core::Transform4d gimbalT = core::Transform4d::fromWPR(wpr);

        app::ProjectDocument doc;
        core::SceneNode node;
        node.id = "gimbal_node";
        node.localTransform = gimbalT;
        doc.sceneNodes.push_back(node);

        const std::string jsonStr = io::ProjectSerializer::toJsonString(doc);
        app::ProjectDocument loadedDoc;
        std::string err;
        const bool ok = io::ProjectSerializer::fromJsonString(jsonStr, loadedDoc, &err);
        check(ok, "M14-002: Deserialize gimbal lock pose succeeded");
        if (ok && loadedDoc.sceneNodes.size() == 1) {
            check(isNear(loadedDoc.sceneNodes[0].localTransform, gimbalT, 1e-9),
                  "M14-002: Gimbal lock transform 100% precision restored");
        }
    }

    // 3. UnitsSettings and WeldScheduleDefinition Round-trip
    {
        app::ProjectDocument doc;
        doc.weldSchedules.clear();
        doc.settings.lengthUnit = "mm";
        doc.settings.angleUnit = "deg";
        doc.settings.chordToleranceMm = 0.05;
        doc.settings.collisionSafeClearanceMm = 8.5;
        doc.settings.wireTipExemptionRadiusMm = 12.0;

        core::WeldScheduleDefinition sched;
        sched.scheduleNumber = 5;
        sched.currentAmps = 220.0;
        sched.voltageVolts = 24.5;
        sched.wireFeedSpeedMPerMin = 7.2;
        sched.setWeldSpeedCmPerMin(60.0);
        sched.gasFlowRateLPerMin = 18.0;
        sched.description = "Heavy Plate MAG 220A";
        doc.weldSchedules.push_back(sched);

        const std::string jsonStr = io::ProjectSerializer::toJsonString(doc);
        app::ProjectDocument loadedDoc;
        std::string err;
        const bool ok = io::ProjectSerializer::fromJsonString(jsonStr, loadedDoc, &err);

        check(ok, "M14-003: Deserialize units & schedules succeeded");
        check(loadedDoc.settings.lengthUnit == "mm" && loadedDoc.settings.angleUnit == "deg",
              "M14-003: UnitsSettings strings restored");
        check(isNear(loadedDoc.settings.collisionSafeClearanceMm, 8.5) &&
              isNear(loadedDoc.settings.wireTipExemptionRadiusMm, 12.0),
              "M14-003: Clearance and exemption radius restored");
        check(loadedDoc.weldSchedules.size() == 1, "M14-003: WeldSchedule count restored");
        if (loadedDoc.weldSchedules.size() == 1) {
            const auto& ws = loadedDoc.weldSchedules[0];
            check(ws.scheduleNumber == 5 && isNear(ws.currentAmps, 220.0) && isNear(ws.voltageVolts, 24.5),
                  "M14-003: Weld schedule voltage/current restored");
            check(isNear(ws.weldSpeedCmPerMin, 60.0) && isNear(ws.weldSpeedMmPerSec, 10.0),
                  "M14-003: Weld speed conversion consistency restored");
            check(ws.description == "Heavy Plate MAG 220A", "M14-003: Schedule description restored");
        }
    }

    // 4. SeamDefinition with sampledFrames, trimming & topological signature
    {
        app::ProjectDocument doc;
        core::SeamDefinition seam;
        seam.id = "seam_box_front";
        seam.name = "Fillet_Front_Left";
        seam.seamType = core::SeamType::Fillet;
        seam.filletSizeMm = 8.0;
        seam.startTrimMm = 15.0;
        seam.endTrimMm = 10.0;
        seam.isClosed = false;
        seam.reversed = true;
        seam.programmed = true;
        seam.face1PartId = "box_base";
        seam.face1Index = 2;
        seam.face2PartId = "box_wall";
        seam.face2Index = 4;

        core::TopologicalSignature sig;
        sig.cadFilePath = "SampleScenes/Scene1/workpiece.step";
        sig.cadFileHash = "abc123hash";
        sig.partName = "BoxPart";
        sig.topologyIndex = 3;
        sig.startPoint = {0.0, 0.0, 0.0};
        sig.endPoint = {200.0, 0.0, 0.0};
        sig.midPoint = {100.0, 0.0, 0.0};
        sig.midTangent = {1.0, 0.0, 0.0};
        sig.arcLengthMm = 200.0;
        seam.edgeChain.push_back(sig);

        for (int i = 0; i < 5; ++i) {
            core::SeamSampleFrame sf;
            sf.arcLengthMm = i * 40.0;
            sf.position = {i * 40.0, 10.0, 20.0};
            sf.tangentX = {1.0, 0.0, 0.0};
            sf.normalFace1 = {0.0, 1.0, 0.0};
            sf.normalFace2 = {0.0, 0.0, 1.0};
            sf.bisector = {0.0, std::sqrt(0.5), std::sqrt(0.5)};
            sf.frame = core::Transform4d::fromTranslation(sf.position.x, sf.position.y, sf.position.z);
            seam.sampledFrames.push_back(sf);
        }
        doc.seams.push_back(seam);

        const std::string jsonStr = io::ProjectSerializer::toJsonString(doc);
        app::ProjectDocument loadedDoc;
        std::string err;
        const bool ok = io::ProjectSerializer::fromJsonString(jsonStr, loadedDoc, &err);

        check(ok, "M14-004: Deserialize seam definition succeeded");
        check(loadedDoc.seams.size() == 1, "M14-004: Seam count restored");
        if (loadedDoc.seams.size() == 1) {
            const auto& rs = loadedDoc.seams[0];
            check(rs.id == "seam_box_front" && rs.seamType == core::SeamType::Fillet, "M14-004: Seam ID and type restored");
            check(isNear(rs.startTrimMm, 15.0) && isNear(rs.endTrimMm, 10.0), "M14-004: Start/End trim mm restored");
            check(rs.reversed && rs.programmed, "M14-004: Reversed and programmed flags restored");
            check(rs.edgeChain.size() == 1, "M14-004: Edge chain count restored");
            if (rs.edgeChain.size() == 1) {
                check(rs.edgeChain[0].cadFileHash == "abc123hash" && isNear(rs.edgeChain[0].arcLengthMm, 200.0),
                      "M14-004: TopologicalSignature fields restored");
            }
            check(rs.sampledFrames.size() == 5, "M14-004: Sampled frames count 100% exact (5 frames)");
            bool framesMatch = true;
            for (size_t i = 0; i < rs.sampledFrames.size() && i < seam.sampledFrames.size(); ++i) {
                if (!isNear(rs.sampledFrames[i].position, seam.sampledFrames[i].position) ||
                    !isNear(rs.sampledFrames[i].tangentX, seam.sampledFrames[i].tangentX) ||
                    !isNear(rs.sampledFrames[i].normalFace1, seam.sampledFrames[i].normalFace1) ||
                    !isNear(rs.sampledFrames[i].normalFace2, seam.sampledFrames[i].normalFace2) ||
                    !isNear(rs.sampledFrames[i].bisector, seam.sampledFrames[i].bisector) ||
                    !isNear(rs.sampledFrames[i].frame, seam.sampledFrames[i].frame) ||
                    !isNear(rs.sampledFrames[i].arcLengthMm, seam.sampledFrames[i].arcLengthMm)) {
                    framesMatch = false;
                    break;
                }
            }
            check(framesMatch, "M14-004: Sampled frames tangent, normals, bisector, and frame 100% exact restored");
        }
    }

    // 5. Target with userModified = true, userDeltaOffset, ArcStart/End events, and configuration
    {
        app::ProjectDocument doc;
        core::ControllerUnit cu;
        cu.id = "ctrl_fanuc";
        cu.name = "FANUC_R30iB_Plus";
        cu.robotNodeId = "robot_m10ia";
        cu.activeUFrame = 2;
        cu.activeUTool = 1;

        core::Program prog;
        prog.id = "prog_weld";
        prog.name = "WELD_MAIN";
        prog.uframeNumber = 2;
        prog.utoolNumber = 1;
        prog.startState.homeJointsRad = {0.0, 0.1, -0.2, 0.0, -1.57, 0.0};

        core::Operation op;
        op.id = "op_fillet_1";
        op.name = "Op_Fillet";
        op.seamId = "seam_1";
        op.targetWorkpieceId = "part_box";
        op.exemptionPartIds = {"part_box", "part_base"};
        op.genParams.weldSpeedMmPerSec = 12.5;

        // Target 1: Approach
        core::Target tApp;
        tApp.id = "t_approach";
        tApp.name = "APPROACH";
        tApp.role = core::TargetRole::APPROACH;
        tApp.motion = core::MotionType::J;
        tApp.speedValue = 80.0;
        tApp.poseInUFrame = core::Transform4d::fromTranslation(100.0, 200.0, 350.0);
        op.targets.push_back(tApp);

        // Target 2: Weld Start with userModified = true and ArcStart event
        core::Target tStart;
        tStart.id = "t_weld_start";
        tStart.name = "WELD_START";
        tStart.role = core::TargetRole::WELD_START;
        tStart.motion = core::MotionType::L;
        tStart.speedValue = 12.5;
        tStart.useWeldSpeed = true;
        tStart.termination = core::TerminationType::FINE;
        tStart.poseInUFrame = core::Transform4d::fromTranslation(100.0, 200.0, 300.0);
        tStart.userModified = true;
        tStart.processModified = true;
        tStart.arcLengthRatio = 0.0;
        tStart.userDeltaOffset = core::Transform4d::fromTranslation(3.5, -2.1, 1.0);
        tStart.userDeltaOffset.rot = core::Mat3::rotZ(core::deg2rad(5.0));
        tStart.config.flip = true;
        tStart.config.up = false;
        tStart.config.top = true;
        tStart.config.turns = {1, -1, 0};
        tStart.lockConfig = true;

        core::ProcessEvent evStart;
        evStart.type = core::ProcessEventType::ArcStart;
        evStart.scheduleNumber = 2;
        evStart.paramValue = 0.0;
        evStart.comment = "Arc on schedule 2";
        tStart.events.push_back(evStart);

        core::ProcessEvent evWeave;
        evWeave.type = core::ProcessEventType::WeaveStart;
        evWeave.scheduleNumber = 1;
        evWeave.paramValue = 2.5;
        evWeave.comment = "Sine weave 2.5mm";
        tStart.events.push_back(evWeave);
        op.targets.push_back(tStart);

        // Target 3: Weld End with ArcEnd event
        core::Target tEnd;
        tEnd.id = "t_weld_end";
        tEnd.name = "WELD_END";
        tEnd.role = core::TargetRole::WELD_END;
        tEnd.motion = core::MotionType::L;
        tEnd.speedValue = 12.5;
        tEnd.useWeldSpeed = true;
        tEnd.poseInUFrame = core::Transform4d::fromTranslation(300.0, 200.0, 300.0);

        core::ProcessEvent evEnd;
        evEnd.type = core::ProcessEventType::ArcEnd;
        evEnd.scheduleNumber = 2;
        evEnd.paramValue = 0.0;
        tEnd.events.push_back(evEnd);
        op.targets.push_back(tEnd);

        prog.operations.push_back(op);
        cu.programs.push_back(prog);
        doc.controllers.push_back(cu);

        const std::string jsonStr = io::ProjectSerializer::toJsonString(doc);
        app::ProjectDocument loadedDoc;
        std::string err;
        const bool ok = io::ProjectSerializer::fromJsonString(jsonStr, loadedDoc, &err);

        check(ok, "M14-005: Deserialize operation & targets succeeded");
        check(loadedDoc.controllers.size() == 1, "M14-005: ControllerUnit count restored");
        if (loadedDoc.controllers.size() == 1 &&
            !loadedDoc.controllers[0].programs.empty() &&
            !loadedDoc.controllers[0].programs[0].operations.empty()) {
            const auto& lop = loadedDoc.controllers[0].programs[0].operations[0];
            check(lop.targets.size() == 3, "M14-005: Operation targets count restored (3 targets)");
            if (lop.targets.size() == 3) {
                const auto& rtStart = lop.targets[1];
                check(rtStart.userModified == true,
                      "M14-005: userModified = true flag 100% restored");
                check(rtStart.processModified == true,
                      "M14-005: processModified = true flag 100% restored");
                check(isNear(rtStart.userDeltaOffset, tStart.userDeltaOffset, 1e-9),
                      "M14-005: userDeltaOffset Transform4d restored to < 1e-9 precision");
                check(rtStart.lockConfig == true,
                      "M14-005: lockConfig flag restored");
                check(rtStart.config.flip == true && rtStart.config.up == false && rtStart.config.turns == std::array<int, 3>{1, -1, 0},
                      "M14-005: FanucConfig flip/up/turns restored exactly");
                check(rtStart.events.size() == 2,
                      "M14-005: ProcessEvent count restored (2 events)");
                if (rtStart.events.size() == 2) {
                    check(rtStart.events[0].type == core::ProcessEventType::ArcStart &&
                          rtStart.events[0].scheduleNumber == 2 &&
                          rtStart.events[0].comment == "Arc on schedule 2",
                          "M14-005: ArcStart event restored exactly");
                    check(rtStart.events[1].type == core::ProcessEventType::WeaveStart &&
                          isNear(rtStart.events[1].paramValue, 2.5),
                          "M14-005: WeaveStart event restored exactly");
                }
                const auto& rtEnd = lop.targets[2];
                check(rtEnd.events.size() == 1 && rtEnd.events[0].type == core::ProcessEventType::ArcEnd,
                      "M14-005: ArcEnd event restored exactly");
            }
        }
    }

    // 6. Complete Workcell File Persistence Round-Trip (Save to disk -> Load from disk -> Compare)
    {
        const std::string tempDir = std::string(OLP_SOURCE_DIR) + "/temps";
        std::filesystem::create_directories(tempDir);
        const std::string savePath = tempDir + "/m14_workcell_persistence_test.json";

        app::ProjectDocument origDoc;
        origDoc.settings.collisionSafeClearanceMm = 6.0;

        // SceneNodes
        core::SceneNode partNode;
        partNode.id = "part_box_base";
        partNode.name = "Box_Base_Solid";
        partNode.type = core::SceneNodeType::Part;
        partNode.collisionRole = core::CollisionRole::Workpiece;
        partNode.sourceCadFile = "SampleScenes/Scene2_Full_Workcell_STEP/Full_Welding_Cell_With_Robot.step";
        partNode.cadHandleId = 42;
        partNode.renderMesh.vertices = {{0.0, 0.0, 0.0}, {100.0, 0.0, 0.0}, {0.0, 100.0, 0.0}};
        partNode.renderMesh.triangles = {{0, 1, 2}};
        partNode.renderMesh.bboxMin = {0.0, 0.0, 0.0};
        partNode.renderMesh.bboxMax = {100.0, 100.0, 0.0};
        partNode.collisionMesh = partNode.renderMesh;
        partNode.localTransform = core::Transform4d::fromTranslation(500.0, -300.0, 600.0);
        origDoc.sceneNodes.push_back(partNode);

        core::SceneNode robotNode;
        robotNode.id = "robot_m10ia";
        robotNode.name = "FANUC M-10iA";
        robotNode.type = core::SceneNodeType::Robot;
        robotNode.collisionRole = core::CollisionRole::RobotLink;
        robotNode.robotModelId = "fanuc_m10ia";
        robotNode.currentJointsRad = {0.1, -0.2, 0.3, -0.4, 0.5, -0.6};
        robotNode.mountedToolNodeId = "tool_torch";
        robotNode.localTransform = core::Transform4d::fromTranslation(0.0, 0.0, 500.0);
        origDoc.sceneNodes.push_back(robotNode);

        core::SceneNode toolNode;
        toolNode.id = "tool_torch";
        toolNode.name = "Torch_150mm";
        toolNode.type = core::SceneNodeType::Tool;
        toolNode.collisionRole = core::CollisionRole::WeldingTorch;
        toolNode.utoolNumber = 1;
        toolNode.tcpOffset = core::Transform4d::fromTranslation(0.0, 0.0, 150.0);
        toolNode.localTransform = toolNode.tcpOffset;
        toolNode.parentId = "robot_m10ia";
        origDoc.sceneNodes.push_back(toolNode);

        // Seams
        core::SeamDefinition seam;
        seam.id = "seam_001";
        seam.name = "Seam_Weld";
        seam.filletSizeMm = 6.0;
        seam.startTrimMm = 32.0;
        seam.endTrimMm = 5.0;
        for (int i = 0; i < 4; ++i) {
            core::SeamSampleFrame sf;
            sf.arcLengthMm = i * 25.0;
            sf.position = {500.0 + i * 25.0, -300.0, 600.0};
            sf.frame = core::Transform4d::fromTranslation(sf.position.x, sf.position.y, sf.position.z);
            seam.sampledFrames.push_back(sf);
        }
        origDoc.seams.push_back(seam);

        // Weld Schedules
        origDoc.weldSchedules.clear();
        core::WeldScheduleDefinition ws1;
        ws1.scheduleNumber = 1;
        ws1.currentAmps = 180.0;
        ws1.voltageVolts = 22.0;
        origDoc.weldSchedules.push_back(ws1);

        // Controllers & Programs
        core::ControllerUnit cu;
        cu.id = "cu_1";
        cu.robotNodeId = "robot_m10ia";
        core::Program prog;
        prog.id = "prog_1";
        prog.name = "WELD_001";
        core::Operation op;
        op.id = "op_1";
        op.seamId = "seam_001";
        core::Target tgt;
        tgt.id = "tgt_1";
        tgt.name = "T1";
        tgt.userModified = true;
        tgt.userDeltaOffset = core::Transform4d::fromTranslation(1.0, 2.0, 3.0);
        tgt.poseInUFrame = core::Transform4d::fromTranslation(10.0, 20.0, 30.0);
        op.targets.push_back(tgt);
        prog.operations.push_back(op);
        cu.programs.push_back(prog);
        origDoc.controllers.push_back(cu);

        // Save to file
        std::string saveErr;
        const bool saveOk = io::ProjectSerializer::saveToFile(origDoc, savePath, {}, &saveErr);
        check(saveOk, "M14-006: Save ProjectDocument to disk file succeeded", saveErr);
        check(std::filesystem::exists(savePath), "M14-006: Saved project file exists on disk");

        // Load back from file
        app::ProjectDocument loadedDoc;
        std::string loadErr;
        const bool loadOk = io::ProjectSerializer::loadFromFile(savePath, loadedDoc, &loadErr);
        check(loadOk, "M14-006: Load ProjectDocument from disk file succeeded", loadErr);

        check(loadedDoc.sceneNodes.size() == 3, "M14-006: Restored 3 scene nodes");
        check(loadedDoc.seams.size() == 1, "M14-006: Restored 1 seam");
        check(loadedDoc.weldSchedules.size() == 1, "M14-006: Restored 1 weld schedule");
        check(loadedDoc.controllers.size() == 1, "M14-006: Restored 1 controller");

        if (loadedDoc.sceneNodes.size() == 3) {
            check(loadedDoc.sceneNodes[0].cadHandleId == 42 &&
                  loadedDoc.sceneNodes[0].collisionRole == core::CollisionRole::Workpiece,
                  "M14-006: Part node role and cadHandleId preserved");
            check(loadedDoc.sceneNodes[0].renderMesh.vertices.size() == 3 &&
                  loadedDoc.sceneNodes[0].renderMesh.triangles.size() == 1,
                  "M14-011: Part node renderMesh vertices and triangles preserved");
            check(isNear(loadedDoc.sceneNodes[0].renderMesh.bboxMax, core::Vec3{100.0, 100.0, 0.0}),
                  "M14-011: Part node renderMesh bounding box preserved");
            check(loadedDoc.sceneNodes[1].robotModelId == "fanuc_m10ia" &&
                  loadedDoc.sceneNodes[1].mountedToolNodeId == "tool_torch",
                  "M14-006: Robot node linkage preserved");
            check(isNear(loadedDoc.sceneNodes[1].currentJointsRad[2], 0.3),
                  "M14-006: Robot joints preserved");
        }

        if (loadedDoc.controllers.size() == 1 &&
            !loadedDoc.controllers[0].programs.empty() &&
            !loadedDoc.controllers[0].programs[0].operations.empty() &&
            !loadedDoc.controllers[0].programs[0].operations[0].targets.empty()) {
            const auto& rt = loadedDoc.controllers[0].programs[0].operations[0].targets[0];
            check(rt.userModified == true, "M14-006: Persistence preserves userModified = true");
            check(isNear(rt.userDeltaOffset, tgt.userDeltaOffset),
                  "M14-006: Persistence preserves exact userDeltaOffset");
            check(isNear(rt.poseInUFrame, tgt.poseInUFrame),
                  "M14-006: Persistence preserves exact poseInUFrame");
        }

        // M14-009: Atomic overwrite existing file test
        std::error_code ec;
        const std::string overwritePath = tempDir + "/m14_overwrite_test.json";
        {
            std::ofstream initOfs(overwritePath);
            initOfs << "old_corruptible_content_prior_to_save";
            initOfs.close();
        }
        std::string overwriteErr;
        const bool overwriteOk = io::ProjectSerializer::saveToFile(origDoc, overwritePath, {}, &overwriteErr);
        check(overwriteOk, "M14-009: Atomic save overwrites existing file successfully", overwriteErr);
        {
            app::ProjectDocument reloadedOverwrite;
            check(io::ProjectSerializer::loadFromFile(overwritePath, reloadedOverwrite),
                  "M14-009: Overwritten file loads cleanly without corruption");
            check(reloadedOverwrite.sceneNodes.size() == 3,
                  "M14-009: Overwritten file has new content");
        }
        std::filesystem::remove(overwritePath, ec);

        // M14-009: Target replacement failure cleans up temporary file and does not succeed
        const std::string lockedDirPath = tempDir;
        std::string lockedDirErr;
        const bool lockedSaveOk = io::ProjectSerializer::saveToFile(origDoc, lockedDirPath, {}, &lockedDirErr);
        check(!lockedSaveOk, "M14-009: Save fails cleanly when target cannot be replaced");
        check(!std::filesystem::exists(lockedDirPath + ".tmp"),
              "M14-009: Temp file cleaned up on replacement failure");

        // M14-010: UTF-8 Chinese path test (Windows CP936 safety)
        const std::string utf8ChinesePath = tempDir + "/工位测试_工程保存_001/工位工程.json";
        std::string chineseSaveErr;
        const bool chineseSaveOk = io::ProjectSerializer::saveToFile(origDoc, utf8ChinesePath, {}, &chineseSaveErr);
        check(chineseSaveOk, "M14-010: Save to UTF-8 Chinese path succeeded", chineseSaveErr);

        app::ProjectDocument loadedChineseDoc;
        std::string chineseLoadErr;
        const bool chineseLoadOk = io::ProjectSerializer::loadFromFile(utf8ChinesePath, loadedChineseDoc, &chineseLoadErr);
        check(chineseLoadOk, "M14-010: Load from UTF-8 Chinese path succeeded", chineseLoadErr);
        if (chineseLoadOk && !loadedChineseDoc.controllers.empty() &&
            !loadedChineseDoc.controllers[0].programs.empty() &&
            !loadedChineseDoc.controllers[0].programs[0].operations.empty() &&
            !loadedChineseDoc.controllers[0].programs[0].operations[0].targets.empty()) {
            check(loadedChineseDoc.controllers[0].programs[0].operations[0].targets[0].userModified == true,
                  "M14-010: Chinese path round-trip preserves userModified = true");
        }
        std::filesystem::remove_all(tempDir + "/工位测试_工程保存_001", ec);

        // Clean up temp file
        std::filesystem::remove(savePath, ec);
    }

    // 7. Error handling robustness (M14-007 & M14-008)
    {
        app::ProjectDocument doc;
        std::string err;

        // Empty file path
        check(!io::ProjectSerializer::saveToFile(doc, "", {}, &err),
              "M14-007: Empty save path rejected gracefully");
        check(!err.empty(), "M14-007: Empty save path returns error message");

        err.clear();
        check(!io::ProjectSerializer::loadFromFile("", doc, &err),
              "M14-007: Empty load path rejected gracefully");

        err.clear();
        check(!io::ProjectSerializer::loadFromFile("non_existent_project_file.json", doc, &err),
              "M14-007: Non-existent file rejected gracefully");

        err.clear();
        check(!io::ProjectSerializer::fromJsonString("{ corrupted json: [ }", doc, &err),
              "M14-007: Corrupted JSON rejected gracefully");

        err.clear();
        check(!io::ProjectSerializer::fromJsonString(
                  R"({"fileFormat": "WrongFormat", "schemaVersion": 1})", doc, &err),
              "M14-007: Unsupported file format rejected gracefully");

        err.clear();
        check(!io::ProjectSerializer::fromJsonString(
                  R"({"fileFormat": "SiFastSuite_Project", "schemaVersion": 999})", doc, &err),
              "M14-007: Future schema version rejected gracefully");

        // M14-008: Empty object {} must be rejected and leave doc untouched
        app::ProjectDocument canaryDoc;
        canaryDoc.settings.lengthUnit = "canary_unit";
        err.clear();
        check(!io::ProjectSerializer::fromJsonString("{}", canaryDoc, &err),
              "M14-008: Empty JSON object rejected due to missing fileFormat");
        check(!err.empty(), "M14-008: Rejection returns error message");
        check(canaryDoc.settings.lengthUnit == "canary_unit",
              "M14-008: Failed deserialization leaves output document untouched");

        // M14-008: Type mismatch for fileFormat
        err.clear();
        check(!io::ProjectSerializer::fromJsonString(R"({"fileFormat": 12345, "schemaVersion": 1})", canaryDoc, &err),
              "M14-008: Integer fileFormat rejected without throwing exception");

        // M14-008: Type mismatch for schemaVersion
        err.clear();
        check(!io::ProjectSerializer::fromJsonString(R"({"fileFormat": "SiFastSuite_Project", "schemaVersion": "v1"})", canaryDoc, &err),
              "M14-008: String schemaVersion rejected without throwing exception");
    }

    // 8. M14-012: Dirty document check before opening new project & closeEvent interception
#ifdef OLP_HAS_QT6
    {
        const std::string tempDir = (std::filesystem::temp_directory_path() / "olp_m14_012_test").string();
        std::error_code ec;
        std::filesystem::create_directories(tempDir, ec);

        const std::string projAPath = tempDir + "/projA.json";
        const std::string projBPath = tempDir + "/projB.json";

        app::ProjectDocument docA;
        core::SceneNode nodeA;
        nodeA.id = "nodeA";
        nodeA.name = "ProjectA_Node";
        docA.sceneNodes.push_back(nodeA);
        io::ProjectSerializer::saveToFile(docA, projAPath);

        app::ProjectDocument docB;
        core::SceneNode nodeB;
        nodeB.id = "nodeB";
        nodeB.name = "ProjectB_Node";
        docB.sceneNodes.push_back(nodeB);
        io::ProjectSerializer::saveToFile(docB, projBPath);

        olp::ui::MainWindow mainWin;
        olp::ui::ShellWindow shell(mainWin);

        // 打开工程 A
        bool openAOk = mainWin.openProject(projAPath);
        check(openAOk, "M14-012: Project A loaded into MainWindow");
        check(!mainWin.document().isDirty, "M14-012: Initial document is not dirty");

        // 标记脏文档
        mainWin.document().isDirty = true;

        // 8.1 脏文档未保存下，用户点击取消 -> 安全中止，保持原状
        bool hookTriggered = false;
        shell.setConfirmDiscardCallback([&]() {
            hookTriggered = true;
            return false; // 取消
        });

        shell.openProjectFile(QString::fromStdString(projBPath));
        check(hookTriggered, "M14-012: Dirty prompt hook triggered on open attempt");
        check(mainWin.document().sceneNodes.size() == 1 &&
              mainWin.document().sceneNodes[0].id == "nodeA",
              "M14-012: Cancel keeps current document untouched (nodeA intact)");
        check(mainWin.document().isDirty, "M14-012: Cancel keeps document dirty flag");

        // 8.2 脏文档未保存下，用户确认放弃 -> 成功覆盖打开工程 B
        hookTriggered = false;
        shell.setConfirmDiscardCallback([&]() {
            hookTriggered = true;
            return true; // 确认放弃
        });

        shell.openProjectFile(QString::fromStdString(projBPath));
        check(hookTriggered, "M14-012: Discard prompt hook triggered on open attempt");
        check(mainWin.document().sceneNodes.size() == 1 &&
              mainWin.document().sceneNodes[0].id == "nodeB",
              "M14-012: Confirm discard overwrites and opens Project B (nodeB)");
        check(!mainWin.document().isDirty, "M14-012: Fresh opened project clears dirty flag");

        // 8.3 保存或 isDirty == false 打开不触发阻拦
        hookTriggered = false;
        shell.setConfirmDiscardCallback([&]() {
            hookTriggered = true;
            return false; // 即使 hook 设为 false，也不应该被调用
        });

        shell.openProjectFile(QString::fromStdString(projAPath));
        check(!hookTriggered, "M14-012: Clean document (isDirty == false) opens without prompting");
        check(mainWin.document().sceneNodes.size() == 1 &&
              mainWin.document().sceneNodes[0].id == "nodeA",
              "M14-012: Clean document opened successfully");

        // 8.4 closeEvent 脏标记拦截
        mainWin.document().isDirty = true;
        hookTriggered = false;
        shell.setConfirmDiscardCallback([&]() {
            hookTriggered = true;
            return false; // 取消关闭
        });

        QCloseEvent closeEvtCancel;
        QApplication::sendEvent(&shell, &closeEvtCancel);
        check(hookTriggered, "M14-012: closeEvent triggers discard prompt hook when dirty");
        check(!closeEvtCancel.isAccepted(), "M14-012: closeEvent ignored when user cancels");

        hookTriggered = false;
        shell.setConfirmDiscardCallback([&]() {
            hookTriggered = true;
            return true; // 确认关闭
        });

        QCloseEvent closeEvtConfirm;
        QApplication::sendEvent(&shell, &closeEvtConfirm);
        check(hookTriggered, "M14-012: closeEvent triggers discard prompt hook on confirm");
        check(closeEvtConfirm.isAccepted(), "M14-012: closeEvent accepted when user confirms discard");

        std::filesystem::remove_all(tempDir, ec);
    }
#endif

    // 9. M14-013: CAD shape re-binding and stale cadHandleId isolation
    {
        const std::string tempDir = (std::filesystem::temp_directory_path() / "olp_m14_013_test").string();
        std::error_code ec;
        std::filesystem::create_directories(tempDir, ec);

        const std::string stepSource =
            std::string(OLP_SOURCE_DIR) + "/SampleScenes/Scene1_TJoint_And_FANUC_M10iA/Workpiece_TJoint_With_Clamps.step";
        const std::string localStepPath = tempDir + "/TestPart.step";

        // 拷贝测试 STEP 文件到临时目录以测试相对路径和存在性
        std::filesystem::copy_file(stepSource, localStepPath, std::filesystem::copy_options::overwrite_existing, ec);

        olp::ui::MainWindow mainWin;

        // 9.1 先在当前内核装入一个旧实体，占用 cadHandleId (如 handle 1)
        std::vector<core::SceneNode> dummyNodes;
        mainWin.cadKernel().importStep(stepSource, dummyNodes);
        check(!dummyNodes.empty(), "M14-013: Step imported for baseline test");
        const int baselineHandle = dummyNodes.empty() ? 1 : dummyNodes[0].cadHandleId;
        const std::string solidName = dummyNodes.empty() ? "Workpiece_TJoint_With_Clamps" : dummyNodes[0].name;
        check(!mainWin.cadKernel().getEdges(baselineHandle).empty(),
              "M14-013: Baseline kernel has loaded handle with valid edges");

        // 9.2 丢失 STEP 或 sourceCadFile 为空：跨工程打开时隔离旧 handle，干净置 -1 并走网格回退
        {
            const std::string missingStepProjPath = tempDir + "/proj_missing_step.json";
            app::ProjectDocument docMissing;
            core::SceneNode nodeMissing;
            nodeMissing.id = "part_missing";
            nodeMissing.name = "PartMissingCad";
            nodeMissing.type = core::SceneNodeType::Part;
            nodeMissing.sourceCadFile = "non_existent_cad_file.step";
            nodeMissing.cadHandleId = baselineHandle; // 假装上一工程残留了 baseline handle

            // 构造持久化网格
            nodeMissing.renderMesh.vertices = {
                {0.0, 0.0, 0.0}, {100.0, 0.0, 0.0}, {100.0, 100.0, 0.0}, {0.0, 100.0, 0.0}
            };
            nodeMissing.renderMesh.triangles = {{0, 1, 2}, {0, 2, 3}};
            nodeMissing.collisionMesh = nodeMissing.renderMesh;
            docMissing.sceneNodes.push_back(nodeMissing);

            io::ProjectSerializer::saveToFile(docMissing, missingStepProjPath);

            std::string openErr;
            const bool ok = mainWin.openProject(missingStepProjPath, &openErr);
            check(ok, "M14-013: Open project with missing STEP succeeded", openErr);

            // 验证旧 handle 彻底被 clear 清空
            check(mainWin.cadKernel().getEdges(baselineHandle).empty(),
                  "M14-013: Prior kernel handles cleared on project open (baseline handle empty)");

            // 验证节点 cadHandleId 干净置为 -1
            check(mainWin.document().sceneNodes.size() == 1, "M14-013: Scene node restored");
            if (mainWin.document().sceneNodes.size() == 1) {
                const auto& loadedNode = mainWin.document().sceneNodes[0];
                check(loadedNode.cadHandleId == -1,
                      "M14-013: Missing STEP cleanly resets cadHandleId to -1");
                check(loadedNode.renderMesh.vertices.size() == 4,
                      "M14-013: Render mesh preserved for fallback display");
                check(loadedNode.collisionMesh.triangles.size() == 2,
                      "M14-013: Collision mesh preserved for fallback collision detection");
            }
        }

        // 9.3 存在有效 STEP（相对路径 + 同文件去重加载）：重新装入内核并更新 cadHandleId
        {
            const std::string validStepProjPath = tempDir + "/proj_valid_step.json";
            app::ProjectDocument docValid;

            // 节点 1: 使用相对路径 "TestPart.step"
            core::SceneNode nodePart1;
            nodePart1.id = "part_valid_1";
            nodePart1.name = solidName;
            nodePart1.type = core::SceneNodeType::Part;
            nodePart1.sourceCadFile = "TestPart.step"; // 相对路径
            nodePart1.cadHandleId = 888; // 故意赋予旧失效 handle

            // 节点 2: 引用同一个 STEP 文件（测试去重加载）
            core::SceneNode nodePart2;
            nodePart2.id = "part_valid_2";
            nodePart2.name = solidName;
            nodePart2.type = core::SceneNodeType::Part;
            nodePart2.sourceCadFile = "TestPart.step"; // 同文件
            nodePart2.cadHandleId = 999;

            docValid.sceneNodes.push_back(nodePart1);
            docValid.sceneNodes.push_back(nodePart2);

            io::ProjectSerializer::saveToFile(docValid, validStepProjPath);

            std::string openErr;
            const bool ok = mainWin.openProject(validStepProjPath, &openErr);
            check(ok, "M14-013: Open project with valid STEP relative path succeeded", openErr);

            check(mainWin.document().sceneNodes.size() == 2, "M14-013: Both nodes restored");
            if (mainWin.document().sceneNodes.size() == 2) {
                const auto& n1 = mainWin.document().sceneNodes[0];
                const auto& n2 = mainWin.document().sceneNodes[1];

                check(n1.cadHandleId > 0 && n1.cadHandleId != 888,
                      "M14-013: Node 1 re-bound to fresh cadHandleId > 0");
                check(n2.cadHandleId > 0 && n2.cadHandleId != 999,
                      "M14-013: Node 2 re-bound to fresh cadHandleId > 0");
                check(n1.cadHandleId == n2.cadHandleId,
                      "M14-013: De-duplicated loading assigns same handleId for same STEP solid");

                // 验证在当前内核中可以获取其 CAD 拓扑边
                const auto edges = mainWin.cadKernel().getEdges(n1.cadHandleId);
                check(!edges.empty(), "M14-013: Re-bound cadHandleId has valid topological edges in kernel");
            }
        }

        std::filesystem::remove_all(tempDir, ec);
    }
}
