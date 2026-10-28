#include "test_support.h"

#include "olp/algo/CollisionAndSim.h"
#include "olp/algo/OpwKinematics.h"
#include "olp/algo/SeamAndToolpathAlgo.h"
#include "olp/algo/TrajectorySimulator.h"
#include "olp/cad/OcctCadKernel.h"
#include "olp/core/ProjectModel.h"
#include "olp/device/RobotModel.h"
#include "olp/io/FanucLsPostProcessor.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <vector>

void testTJointSampleSceneEndToEndRegression() {
    std::cout << "\n--- Test 23: Scene 1 T-Joint Workpiece End-to-End Automated Regression (DIR-001 Step 7) ---\n";

    // =========================================================================
    // 1. Station & Workpiece Import (Workpiece_TJoint_With_Clamps.step)
    // =========================================================================
    olp::cad::OcctCadKernel cad;
    std::vector<olp::core::SceneNode> nodes;
    const std::string stepPath = std::string(OLP_SOURCE_DIR) +
        "/SampleScenes/Scene1_TJoint_And_FANUC_M10iA/Workpiece_TJoint_With_Clamps.step";

    const bool importOk = cad.importStep(stepPath, nodes);
    check(importOk && nodes.size() == 4,
          "T-Joint regression: import 4 solids from Workpiece_TJoint_With_Clamps.step",
          "count=" + std::to_string(nodes.size()));

    int webHandle = -1;
    int baseHandle = -1;
    std::string webNodeId;
    std::string baseNodeId;
    std::vector<int> workpieceHandles;
    std::set<std::string> solidNames;

    for (const auto& node : nodes) {
        solidNames.insert(node.name);
        if (node.collisionRole == olp::core::CollisionRole::Workpiece) {
            workpieceHandles.push_back(node.cadHandleId);
            if (node.name.find("Vertical_Web_Plate") != std::string::npos) {
                webHandle = node.cadHandleId;
                webNodeId = node.id;
            } else if (node.name.find("Base_Plate") != std::string::npos) {
                baseHandle = node.cadHandleId;
                baseNodeId = node.id;
            }
        }
    }

    const bool hasExpectedSolids =
        solidNames.count("Base_Plate_400x300x12") == 1 &&
        solidNames.count("Vertical_Web_Plate_400x12x160") == 1 &&
        solidNames.count("Fixture_Clamp_Left") == 1 &&
        solidNames.count("Fixture_Clamp_Right") == 1;
    check(hasExpectedSolids && webHandle >= 0 && baseHandle >= 0,
          "T-Joint regression: verify expected part names & handles for web, base, and clamps");

    // Robot & Tool Definition
    const auto robotModel = olp::device::RobotModel::createFanucM10iA();
    olp::algo::OpwKinematics kin(robotModel);
    olp::core::UnitsSettings settings;
    settings.collisionSafeClearanceMm = 5.0;
    settings.wireTipExemptionRadiusMm = 10.0;
    olp::algo::CollisionAndSimEngine simEngine(kin, settings);

    // Verify FANUC M-10iA OPW geometry contract
    check(robotModel.opw.a1 == 150.0 && robotModel.opw.c1 == 450.0 &&
          robotModel.opw.c2 == 600.0 && robotModel.opw.c3 == 640.0,
          "T-Joint regression: FANUC M-10iA OPW parameters match kinematics specification");

    // Workcell spatial arrangement: Robot base at origin, Workpiece UFRAME placed in dexterous front workspace
    const auto robotBaseInWorld = olp::core::Transform4d::identity();
    const auto uframeInWorld = olp::core::Transform4d::fromTranslation(750.0, 0.0, 300.0);
    const auto utoolOffset = olp::core::Transform4d::fromTranslation(0.0, 0.0, 150.0);

    for (auto& node : nodes) {
        node.localTransform = uframeInWorld;
    }

    // =========================================================================
    // 2. Cross-Part Edge Picking & T-Joint Fillet Seam Recognition
    // =========================================================================
    // Pick Edge 3 on Vertical_Web_Plate (400 mm front T-joint junction edge with Base_Plate)
    constexpr int kFrontSeamEdgeIndex = 3;
    constexpr double kSampleStepMm = 50.0;
    auto seamOpt = olp::algo::SeamAndToolpathAlgo::buildSeamFromPickedEdge(
        cad, webHandle, kFrontSeamEdgeIndex, workpieceHandles, kSampleStepMm, 5.0);

    check(seamOpt.has_value(), "T-Joint regression: build seam from picked edge succeeds");
    if (!seamOpt.has_value()) {
        return;
    }

    check(seamOpt->face1PartId != seamOpt->face2PartId,
          "T-Joint regression: cross-part seam flanks recognized across web plate and base plate",
          "face1=" + seamOpt->face1PartId + " face2=" + seamOpt->face2PartId);

    const auto& frames = seamOpt->sampledFrames;
    check(frames.size() >= 8,
          "T-Joint regression: seam sampling produces >= 8 frames along 400 mm seam",
          "count=" + std::to_string(frames.size()));

    const auto webEdges = cad.getEdges(webHandle);
    check(std::abs(webEdges[kFrontSeamEdgeIndex].lengthMm - 400.0) < 0.1,
          "T-Joint regression: picked edge length matches 400 mm workpiece span",
          "edgeLength=" + std::to_string(webEdges[kFrontSeamEdgeIndex].lengthMm));

    const double seamLengthMm = frames.back().arcLengthMm - frames.front().arcLengthMm;
    check(std::abs(seamLengthMm - 390.0) < 1.0,
          "T-Joint regression: seam path spans 390 mm (400 mm edge with 5 mm trims)",
          "length=" + std::to_string(seamLengthMm));

    // =========================================================================
    // 3. Torch Dual-Side Angle Bisector Solving & Local Frame Orthonormality
    // =========================================================================
    const olp::core::Vec3 expectedBisector = olp::core::Vec3{0.0, -1.0, 1.0}.normalized();
    bool allBisectorsAccurate = true;
    bool allFramesOrthonormal = true;
    double maxBisectorDevDeg = 0.0;

    for (const auto& sf : frames) {
        const double bisectorDot = std::abs(sf.bisector.dot(expectedBisector));
        const double devDeg = olp::core::rad2deg(std::acos(std::clamp(bisectorDot, 0.0, 1.0)));
        maxBisectorDevDeg = std::max(maxBisectorDevDeg, devDeg);
        if (devDeg > 0.01) {
            allBisectorsAccurate = false;
        }

        const auto cX = sf.frame.rot.colX();
        const auto cY = sf.frame.rot.colY();
        const auto cZ = sf.frame.rot.colZ();
        const double det = cX.dot(cY.cross(cZ));
        if (std::abs(det - 1.0) > 1e-4) {
            allFramesOrthonormal = false;
        }
    }

    check(allBisectorsAccurate,
          "T-Joint regression: dual-side flank angle bisector matches 45 deg across all frames",
          "maxDev=" + std::to_string(maxBisectorDevDeg) + " deg");
    check(allFramesOrthonormal,
          "T-Joint regression: all sampled seam frames form valid right-handed orthonormal frames");

    // =========================================================================
    // 4. Wire-Tip Collision Exemption at Weld Root vs Strict Fixture Clamps
    // =========================================================================
    const auto& firstFrame = frames.front();
    const auto torchPoseAtRoot = olp::algo::SeamAndToolpathAlgo::applyWeldingAngles(
        firstFrame.frame, 0.0, 10.0, 0.0);
    const auto torchPoseAtRootWorld = uframeInWorld * torchPoseAtRoot;

    // Test 4a: Wire-tip exemption ACTIVE during welding role
    double weldClearance = 1000.0;
    std::string weldHitPart;
    const bool weldCollided = simEngine.checkTorchPoseCollision(
        torchPoseAtRootWorld, nodes, webNodeId, true, weldClearance, weldHitPart,
        {seamOpt->face1PartId, seamOpt->face2PartId});
    check(!weldCollided,
          "T-Joint regression: wire-tip exemption prevents false collision alarm at weld root",
          "hitPart=" + weldHitPart + " clearance=" + std::to_string(weldClearance));

    // Test 4b: Wire-tip exemption INACTIVE during non-welding transport role
    double nonWeldClearance = 0.0;
    std::string nonWeldHitPart;
    const bool nonWeldCollided = simEngine.checkTorchPoseCollision(
        torchPoseAtRootWorld, nodes, webNodeId, false, nonWeldClearance, nonWeldHitPart, {});
    check(nonWeldCollided,
          "T-Joint regression: touching wire tip detected as contact/collision when welding exemption is off",
          "hitPart=" + nonWeldHitPart + " clearance=" + std::to_string(nonWeldClearance));

    // Test 4c: Strict collision detection against clamps (Fixture_Clamp_Left & Fixture_Clamp_Right)
    olp::core::Transform4d clampTestPose = torchPoseAtRoot;
    clampTestPose.pos = {-165.0, -105.0, 30.0}; // Inside Fixture_Clamp_Left bounding volume
    const auto clampWorldPose = uframeInWorld * clampTestPose;

    double clampClearance = 0.0;
    std::string clampHitPart;
    const bool clampCollided = simEngine.checkTorchPoseCollision(
        clampWorldPose, nodes, webNodeId, true, clampClearance, clampHitPart,
        {seamOpt->face1PartId, seamOpt->face2PartId});
    check(clampCollided && clampHitPart.find("Fixture_Clamp_Left") != std::string::npos,
          "T-Joint regression: torch body collision strictly detected against Fixture_Clamp_Left",
          "hitPart=" + clampHitPart);

    // =========================================================================
    // 5. Obstacle Avoidance, Toolpath Generation, Trajectory Simulation & .ls Export
    // =========================================================================
    olp::core::Program prog;
    prog.id = "prog_tjoint_001";
    prog.name = "TJOINT_001";
    prog.uframeNumber = 1;
    prog.utoolNumber = 1;

    olp::core::Operation op;
    op.id = "op_tjoint_fillet";
    op.name = "Fillet_Web_Front";
    op.seamId = seamOpt->id;
    op.targetWorkpieceId = webNodeId;
    op.exemptionPartIds = {seamOpt->face1PartId, seamOpt->face2PartId};
    op.genParams.travelAngleDeg = 10.0;
    op.genParams.approachDistMm = 45.0;
    op.genParams.retractDistMm = 45.0;
    op.genParams.endpointsOnlyForLine = false; // Retain all via waypoints along the seam

    // Generate targets and verify structure
    olp::algo::SeamAndToolpathAlgo::generateOperationTargets(*seamOpt, uframeInWorld, op);
    check(op.targets.size() >= 10,
          "T-Joint regression: operation generated targets containing approach, weld via, and retract",
          "targetCount=" + std::to_string(op.targets.size()));

    // Verify process events attached
    check(!op.targets.empty() && op.targets.front().role == olp::core::TargetRole::APPROACH,
          "T-Joint regression: first target is APPROACH");
    check(op.targets.size() > 1 && op.targets[1].role == olp::core::TargetRole::WELD_START &&
          !op.targets[1].events.empty() &&
          op.targets[1].events.front().type == olp::core::ProcessEventType::ArcStart,
          "T-Joint regression: WELD_START carries ArcStart[1] process event");
    check(op.targets.size() > 2 && op.targets[op.targets.size() - 2].role == olp::core::TargetRole::WELD_END &&
          !op.targets[op.targets.size() - 2].events.empty() &&
          op.targets[op.targets.size() - 2].events.front().type == olp::core::ProcessEventType::ArcEnd,
          "T-Joint regression: WELD_END carries ArcEnd[1] process event");
    check(op.targets.back().role == olp::core::TargetRole::RETRACT,
          "T-Joint regression: last target is RETRACT");

    // Obstacle Avoidance: Trim adjustment to ensure clearance from clamps
    const bool trimCleared = olp::algo::SeamAndToolpathAlgo::extendStartTrimUntilTorchClears(
        cad, webHandle, kFrontSeamEdgeIndex, workpieceHandles, kSampleStepMm,
        simEngine, uframeInWorld, nodes, *seamOpt, op);
    check(trimCleared, "T-Joint regression: seam start trim clears fixture clamp proximity");

    prog.operations.push_back(op);

    // Obstacle Avoidance: Safe transit insertion from Home to Approach
    const bool transitOk = simEngine.insertSafeTransitBeforeApproach(
        prog, robotBaseInWorld, uframeInWorld, utoolOffset, nodes, 0.05);
    check(transitOk, "T-Joint regression: safe transit from Home to Approach verified without collision");

    // Full Program Simulation & OPW Kinematic Reachability Validation
    const auto simRes = simEngine.validateAndSimulateProgram(
        prog, robotBaseInWorld, uframeInWorld, utoolOffset, nodes, 0.05);

    check(!simRes.hasConfigJumpInLinear,
          "T-Joint regression: full trajectory simulation has no configuration jumps in linear moves");

    bool allTargetsValid = true;
    for (const auto& target : prog.operations.front().targets) {
        if (!target.cache.valid ||
            target.cache.status == olp::core::TargetValidationState::Unreachable ||
            target.cache.status == olp::core::TargetValidationState::JointLimitExceeded ||
            target.cache.status == olp::core::TargetValidationState::ConfigJump) {
            allTargetsValid = false;
        }
    }
    check(allTargetsValid,
          "T-Joint regression: all targets have valid OPW kinematic inverse solutions within limits");

    // Trajectory Motion Planning & Frame Baking
    olp::algo::TrajectorySimulator trajectory(kin, robotModel);
    const auto motion = trajectory.plan(
        prog, robotBaseInWorld, uframeInWorld, utoolOffset, 30.0);

    check(!motion.frames.empty(),
          "T-Joint regression: trajectory planner baked animation frames (> 0)",
          "frames=" + std::to_string(motion.frames.size()));
    check(motion.totalCycleTimeSec > 0.0 && motion.arcWeldTimeSec > 0.0 && motion.airCutTimeSec > 0.0,
          "T-Joint regression: cycle time split (weld + air-cut) strictly positive",
          "total=" + std::to_string(motion.totalCycleTimeSec) +
          "s weld=" + std::to_string(motion.arcWeldTimeSec) +
          "s air=" + std::to_string(motion.airCutTimeSec) + "s");

    // Post-Processing: FANUC .ls Generation & Validation
    const auto lsValidation = olp::io::FanucLsPostProcessor::validateProgram(prog);
    check(lsValidation.ok && !lsValidation.hasErrors(),
          "T-Joint regression: FANUC .ls post-processor program validation succeeds without errors");

    const std::string lsText = olp::io::FanucLsPostProcessor::generateLsString(prog);
    const bool lsStructureOk =
        lsText.find("/PROG  TJOINT_001") != std::string::npos &&
        lsText.find("/ATTR") != std::string::npos &&
        lsText.find("/APPL") != std::string::npos &&
        lsText.find("/MN") != std::string::npos &&
        lsText.find("Arc Start[1]") != std::string::npos &&
        lsText.find("Arc End[1]") != std::string::npos &&
        lsText.find("/POS") != std::string::npos &&
        lsText.find("/END") != std::string::npos;
    check(lsStructureOk,
          "T-Joint regression: generated FANUC .ls program contains valid /PROG, /MN, Arc Start/End, /POS, /END");

    const std::string exportPath = std::string(OLP_SOURCE_DIR) + "/build/TJOINT_001.ls";
    const bool fileExportOk = olp::io::FanucLsPostProcessor::exportToFile(prog, exportPath);
    check(fileExportOk, "T-Joint regression: exported FANUC .ls file to disk successfully");
}
