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
            torchPoseAtSeam, nodes, webNodeId, true, minClearance, hitPart,
            {seamOpt->face1PartId, seamOpt->face2PartId});
        check(!falseAlarm, "Wire-tip collision exemption prevents false alarm at weld root",
              "hit=" + hitPart + " clearance=" + std::to_string(minClearance));

        // Place torch body directly inside Fixture_Clamp_Left (-190..-140, -140..-70, 12..55)
        olp::core::Transform4d collidingPose = torchPoseAtSeam;
        collidingPose.pos = {-165.0, -105.0, 30.0};
        const bool clampDetected = colEngine.checkTorchPoseCollision(
            collidingPose, nodes, webNodeId, true, minClearance, hitPart);
        check(clampDetected && hitPart.find("Fixture_Clamp_Left") != std::string::npos,
              "Detect torch collision against Fixture_Clamp_Left (" + hitPart + ")");
    }
}


void testSweepCollisionBetweenClearTargets() {
    std::cout << "\n--- Test 9: Torch sweep collision between clear targets ---\n";
    const auto robot = olp::device::RobotModel::createFanucM10iA();
    olp::algo::OpwKinematics kin(robot);
    olp::core::UnitsSettings settings;
    olp::algo::CollisionAndSimEngine engine(kin, settings);
    const auto utool = olp::core::Transform4d::identity();
    const auto home = robot.homeJointsRad;
    auto endJoints = home;
    endJoints[0] += 0.4;

    const double jDist = olp::algo::OpwKinematics::weightedJointDistance(home, endJoints);
    const double sampleStep = 0.05;
    const double segDuration = std::max(0.2, jDist / 1.5);
    const int numSteps = std::max(2, static_cast<int>(std::ceil(segDuration / sampleStep)));
    const int midStep = std::max(1, numSteps / 2);
    const double alpha = static_cast<double>(midStep) / static_cast<double>(numSteps);
    std::array<double, 6> midJoints{};
    for (size_t k = 0; k < 6; ++k) {
        midJoints[k] = home[k] + alpha * (endJoints[k] - home[k]);
    }
    const auto midTcp = kin.forwardKinematics(midJoints, utool).tcpPoseInBase;
    const olp::core::Vec3 gateCenter = midTcp.pos + (-midTcp.rot.colZ()) * 95.0;
    constexpr double half = 6.0;

    olp::core::SceneNode gate;
    gate.id = "sweep_gate";
    gate.name = "Sweep_Gate";
    gate.type = olp::core::SceneNodeType::Part;
    gate.visible = true;
    gate.collisionRole = olp::core::CollisionRole::Fixture;
    gate.collisionMesh.bboxMin = gateCenter - olp::core::Vec3{half, half, half};
    gate.collisionMesh.bboxMax = gateCenter + olp::core::Vec3{half, half, half};
    const std::vector<olp::core::SceneNode> scene{gate};

    const auto startTcp = kin.forwardKinematics(home, utool).tcpPoseInBase;
    const auto endTcp = kin.forwardKinematics(endJoints, utool).tcpPoseInBase;
    const auto identity = olp::core::Transform4d::identity();
    double startClearance = 0.0;
    double endClearance = 0.0;
    std::string startHit;
    std::string endHit;
    const bool startHits = engine.checkTorchPoseCollision(
        startTcp, scene, "", false, startClearance, startHit);
    const bool endHits = engine.checkTorchPoseCollision(
        endTcp, scene, "", false, endClearance, endHit);
    check(!startHits && !endHits,
          "Sweep gate misses both ends of the joint move",
          "start=" + std::to_string(startClearance) + " end=" + std::to_string(endClearance));

    olp::core::Program program;
    program.id = "prog_sweep";
    program.startState.homeJointsRad = home;
    olp::core::Operation op;
    op.id = "op_sweep";
    olp::core::Target target;
    target.id = "via_end";
    target.name = "Joint_End";
    target.role = olp::core::TargetRole::VIA;
    target.motion = olp::core::MotionType::J;
    target.poseInUFrame = endTcp;
    op.targets.push_back(target);
    program.operations.push_back(op);

    // Endpoint poses are physically clear before simulation (see startHits/endHits above).
    const auto sim = engine.validateAndSimulateProgram(
        program,
        olp::core::Transform4d::identity(),
        olp::core::Transform4d::identity(),
        utool,
        scene,
        sampleStep);
    bool sweepHit = false;
    std::string issueText;
    for (const auto& issue : sim.issues) {
        issueText += issue.description + "; ";
        if (issue.state == olp::core::TargetValidationState::Collision &&
            issue.description.find("Sweep collides with Sweep_Gate") != std::string::npos) {
            sweepHit = true;
        }
    }
    const auto& endCache = program.operations[0].targets[0].cache;
    // FIX-EXPORT-GATE-002: mid-segment hard hit must write back target.cache.status.
    // Old assertion required endpointClear (no writeback); intentionally flipped.
    check(sweepHit,
          "Joint interpolation reports a torch sweep through a fixture the targets miss",
          issueText);
    check(endCache.status == olp::core::TargetValidationState::Collision,
          "FIX-EXPORT-GATE-002: mid-segment hard sweep writes Collision back to target.cache.status",
          "status=" + std::to_string(static_cast<int>(endCache.status)) + " msg=" + endCache.issueMessage);
    check(endCache.valid,
          "FIX-EXPORT-GATE-002: cache.valid stays true when endpoint IK exists (collision is status-only)");
    const auto lsVal = olp::io::FanucLsPostProcessor::validateProgram(program);
    check(!lsVal.ok && lsVal.hasErrors(),
          "FIX-EXPORT-GATE-002: after validateAndSimulateProgram, default validateProgram rejects mid-segment Collision writeback");
}


void testNearSweepDoesNotShortCircuitHardCollision() {
    std::cout << "\n--- Test 9b: FIX-EXPORT-GATE-002 NearCollision must not short-circuit hard sweep ---\n";
    const auto robot = olp::device::RobotModel::createFanucM10iA();
    olp::algo::OpwKinematics kin(robot);
    olp::core::UnitsSettings settings;
    // Inflate safe clearance so approaching samples become NearCollision before the mid hard hit.
    settings.collisionSafeClearanceMm = 120.0;
    olp::algo::CollisionAndSimEngine engine(kin, settings);
    const auto utool = olp::core::Transform4d::identity();
    const auto home = robot.homeJointsRad;
    auto endJoints = home;
    endJoints[0] += 0.4;

    const double jDist = olp::algo::OpwKinematics::weightedJointDistance(home, endJoints);
    const double sampleStep = 0.05;
    const double segDuration = std::max(0.2, jDist / 1.5);
    const int numSteps = std::max(2, static_cast<int>(std::ceil(segDuration / sampleStep)));
    const int midStep = std::max(1, numSteps / 2);
    const double alpha = static_cast<double>(midStep) / static_cast<double>(numSteps);
    std::array<double, 6> midJoints{};
    for (size_t k = 0; k < 6; ++k) {
        midJoints[k] = home[k] + alpha * (endJoints[k] - home[k]);
    }
    const auto midTcp = kin.forwardKinematics(midJoints, utool).tcpPoseInBase;
    const olp::core::Vec3 gateCenter = midTcp.pos + (-midTcp.rot.colZ()) * 95.0;
    constexpr double half = 6.0;

    olp::core::SceneNode gate;
    gate.id = "sweep_gate_near_then_hard";
    gate.name = "Sweep_Gate_NearThenHard";
    gate.type = olp::core::SceneNodeType::Part;
    gate.visible = true;
    gate.collisionRole = olp::core::CollisionRole::Fixture;
    gate.collisionMesh.bboxMin = gateCenter - olp::core::Vec3{half, half, half};
    gate.collisionMesh.bboxMax = gateCenter + olp::core::Vec3{half, half, half};
    const std::vector<olp::core::SceneNode> scene{gate};

    const auto endTcp = kin.forwardKinematics(endJoints, utool).tcpPoseInBase;
    olp::core::Program program;
    program.id = "prog_sweep_near_hard";
    program.startState.homeJointsRad = home;
    olp::core::Operation op;
    op.id = "op_sweep_near_hard";
    olp::core::Target target;
    target.id = "via_end";
    target.name = "Joint_End";
    target.role = olp::core::TargetRole::VIA;
    target.motion = olp::core::MotionType::J;
    target.poseInUFrame = endTcp;
    op.targets.push_back(target);
    program.operations.push_back(op);

    const auto sim = engine.validateAndSimulateProgram(
        program,
        olp::core::Transform4d::identity(),
        olp::core::Transform4d::identity(),
        utool,
        scene,
        sampleStep);

    bool sawNear = false;
    bool sawHard = false;
    std::string issueText;
    for (const auto& issue : sim.issues) {
        issueText += issue.description + "; ";
        if (issue.state == olp::core::TargetValidationState::NearCollision) {
            sawNear = true;
        }
        if (issue.state == olp::core::TargetValidationState::Collision) {
            sawHard = true;
        }
    }
    const auto& endCache = program.operations[0].targets[0].cache;
    check(sawHard,
          "FIX-EXPORT-GATE-002: hard sweep Collision is recorded even if NearCollision appeared earlier",
          issueText);
    check(sawNear,
          "FIX-EXPORT-GATE-005: NearCollision must be observed before hard Collision (locks no-short-circuit)",
          issueText);
    check(endCache.status == olp::core::TargetValidationState::Collision,
          "FIX-EXPORT-GATE-002: final target.cache.status is Collision (not stuck on NearCollision)",
          "status=" + std::to_string(static_cast<int>(endCache.status)) +
              " nearIssueSeen=" + std::string(sawNear ? "yes" : "no") +
              " msg=" + endCache.issueMessage);
    check(endCache.valid,
          "FIX-EXPORT-GATE-002: cache.valid remains true after Near→Collision upgrade");
}


void testHardTierCollisionPrefersOverConfigJump() {
    std::cout << "\n--- Test 9c: FIX-EXPORT-GATE-005 Hard-tier Collision replaces ConfigJump ---\n";
    using S = olp::core::TargetValidationState;
    // Soft → Hard always upgrades
    check(olp::algo::CollisionAndSimEngine::shouldUpgradeCacheStatus(S::Valid, S::Collision),
          "FIX-EXPORT-GATE-005: Valid → Collision upgrades");
    check(olp::algo::CollisionAndSimEngine::shouldUpgradeCacheStatus(S::NearCollision, S::Collision),
          "FIX-EXPORT-GATE-005: NearCollision → Collision upgrades");
    // Same Hard tier: Collision must replace ConfigJump / Unreachable (OPEN-P1-4)
    check(olp::algo::CollisionAndSimEngine::shouldUpgradeCacheStatus(S::ConfigJump, S::Collision),
          "FIX-EXPORT-GATE-005: ConfigJump → Collision upgrades (Collision preferred within Hard)");
    check(olp::algo::CollisionAndSimEngine::shouldUpgradeCacheStatus(S::Unreachable, S::Collision),
          "FIX-EXPORT-GATE-005: Unreachable → Collision upgrades within Hard");
    check(olp::algo::CollisionAndSimEngine::shouldUpgradeCacheStatus(S::JointLimitExceeded, S::Collision),
          "FIX-EXPORT-GATE-005: JointLimitExceeded → Collision upgrades within Hard");
    // Must not downgrade Collision to ConfigJump / Near
    check(!olp::algo::CollisionAndSimEngine::shouldUpgradeCacheStatus(S::Collision, S::ConfigJump),
          "FIX-EXPORT-GATE-005: Collision is not replaced by ConfigJump");
    check(!olp::algo::CollisionAndSimEngine::shouldUpgradeCacheStatus(S::Collision, S::NearCollision),
          "FIX-EXPORT-GATE-005: Collision is not replaced by NearCollision");
    check(!olp::algo::CollisionAndSimEngine::shouldUpgradeCacheStatus(S::Collision, S::Valid),
          "FIX-EXPORT-GATE-005: Collision is not replaced by Valid");
}


void testApproachTransitClearsWall() {
    std::cout << "\n--- Test 10: Home-to-approach transit clears a wall ---\n";
    const auto robot = olp::device::RobotModel::createFanucM10iA();
    olp::algo::OpwKinematics kin(robot);
    olp::core::UnitsSettings settings;
    olp::algo::CollisionAndSimEngine engine(kin, settings);
    const auto utool = olp::core::Transform4d::identity();
    const auto home = robot.homeJointsRad;
    const auto homeTcp = kin.forwardKinematics(home, utool).tcpPoseInBase;

    olp::core::Transform4d approach = homeTcp;
    approach.pos.x += 100.0;
    approach.pos.y += 350.0;
    approach.pos.z -= 180.0;
    approach.rot = homeTcp.rot;

    olp::core::SceneNode wall;
    wall.id = "transit_wall";
    wall.name = "Transit_Wall";
    wall.type = olp::core::SceneNodeType::Part;
    wall.visible = true;
    wall.collisionRole = olp::core::CollisionRole::Fixture;
    wall.collisionMesh.bboxMin = {650.0, 90.0, approach.pos.z - 40.0};
    wall.collisionMesh.bboxMax = {850.0, 140.0, homeTcp.pos.z - 30.0};
    const std::vector<olp::core::SceneNode> scene{wall};

    const auto identity = olp::core::Transform4d::identity();
    double startClearance = 0.0;
    double endClearance = 0.0;
    std::string startHit;
    std::string endHit;
    const bool startHits = engine.checkRobotAndTorchCollision(
        homeTcp, home, identity, utool, scene, "", false, startClearance, startHit, {});

    const auto appSols = kin.inverseKinematics(approach, utool, false);
    const auto appChosen = kin.selectBestSolution(appSols, home, std::nullopt, true);
    bool endHits = false;
    if (appChosen) {
        endHits = engine.checkRobotAndTorchCollision(
            approach, appChosen->jointsRad, identity, utool, scene, "", false, endClearance, endHit, {});
    }
    check(!startHits && !endHits,
          "Transit wall misses the home pose and the approach pose",
          "start=" + std::to_string(startClearance) + " end=" + std::to_string(endClearance));

    olp::core::Program program;
    program.id = "prog_transit";
    program.startState.homeJointsRad = home;
    olp::core::Operation op;
    op.id = "op_transit";
    olp::core::Target target;
    target.id = "approach";
    target.name = "APPROACH";
    target.role = olp::core::TargetRole::APPROACH;
    target.motion = olp::core::MotionType::J;
    target.speedValue = 50.0;
    target.poseInUFrame = approach;
    const olp::core::Vec3 approachPos = target.poseInUFrame.pos;
    op.targets.push_back(target);
    program.operations.push_back(op);

    auto before = engine.validateAndSimulateProgram(
        program, identity, identity, utool, scene, 0.05);
    bool sweepHit = false;
    for (const auto& issue : before.issues) {
        if (issue.state == olp::core::TargetValidationState::Collision &&
            issue.description.find("Sweep collides with Transit_Wall") != std::string::npos) {
            sweepHit = true;
        }
    }
    check(sweepHit, "Joint move from home to the approach sweeps the wall");

    const bool inserted = engine.insertSafeTransitBeforeApproach(
        program, identity, identity, utool, scene, 0.05);
    check(inserted, "A torch transit over the wall is found");

    bool clearancePoint = false;
    for (const auto& kept : program.operations[0].targets) {
        if (kept.role == olp::core::TargetRole::APPROACH) {
            check((kept.poseInUFrame.pos - approachPos).norm() < 1e-6,
                  "The approach pose itself stays where it was");
        }
        if (kept.name.rfind("CLEARANCE_", 0) == 0) {
            clearancePoint = true;
        }
    }
    check(clearancePoint, "The transit adds a clearance point above the wall");

    auto after = engine.validateAndSimulateProgram(
        program, identity, identity, utool, scene, 0.05);
    bool blocking = false;
    std::string issueText;
    for (const auto& issue : after.issues) {
        issueText += issue.targetName + ": " + issue.description + "; ";
        if (issue.state == olp::core::TargetValidationState::Collision ||
            issue.state == olp::core::TargetValidationState::Unreachable ||
            issue.state == olp::core::TargetValidationState::JointLimitExceeded ||
            issue.state == olp::core::TargetValidationState::ConfigJump) {
            blocking = true;
        }
    }
    check(!blocking, "The clearance transit has no blocking collision or reach issue", issueText);
}


void testKinematicsReachabilitySingularityAndTwistScan() {
    std::cout << "\n--- Test 17: M10 Kinematics Reachability, Singularity Warning, Wire-Tip Collision Exemption & Twist Angle Scan ---\n";

    const auto robot = olp::device::RobotModel::createFanucM10iA();
    olp::algo::OpwKinematics kin(robot);

    // 1. Joint Limits & 5° Warning Margin Check (checkJointLimits)
    {
        bool within = false;
        bool nearLimit = false;

        // Home joints: all nominal within limits
        kin.checkJointLimits(robot.homeJointsRad, within, nearLimit);
        check(within, "Home posture is strictly within joint limits");
        check(!nearLimit, "Home posture is not near any joint limits");

        // J1 upper limit is 170 deg (approx 2.967 rad), warning margin is 5 deg (0.08726 rad)
        // Angle at 168 deg (within limit, but inside 5 deg warning margin)
        auto jNear = robot.homeJointsRad;
        jNear[0] = olp::core::deg2rad(168.0);
        kin.checkJointLimits(jNear, within, nearLimit);
        check(within, "J1 at 168° is within joint limit (< 170°)");
        check(nearLimit, "J1 at 168° triggers near-limit warning (< 5° margin)");

        // J1 at 175 deg (exceeds 170 deg upper limit)
        auto jOver = robot.homeJointsRad;
        jOver[0] = olp::core::deg2rad(175.0);
        kin.checkJointLimits(jOver, within, nearLimit);
        check(!within, "J1 at 175° is correctly flagged as exceeding joint limit");

        // J2 lower limit is -90 deg (-1.5708 rad), test -88 deg (near lower limit)
        auto j2Near = robot.homeJointsRad;
        j2Near[1] = olp::core::deg2rad(-88.0);
        kin.checkJointLimits(j2Near, within, nearLimit);
        check(within && nearLimit, "J2 at -88° triggers near-lower-limit warning");

        // J2 at -95 deg (exceeds lower limit)
        auto j2Over = robot.homeJointsRad;
        j2Over[1] = olp::core::deg2rad(-95.0);
        kin.checkJointLimits(j2Over, within, nearLimit);
        check(!within, "J2 at -95° is correctly flagged as exceeding joint limit");
    }

    // 2. Singularity Detection & Manipulability Index (checkSingularity)
    {
        double manip = 0.0;

        // Nominal posture: safe, non-singular, healthy manipulability
        bool isSing = kin.checkSingularity(robot.homeJointsRad, manip);
        check(!isSing, "Home posture is not near any kinematic singularities");
        check(manip > 0.05, "Home posture has healthy manipulability index (> 0.05, actual: " + std::to_string(manip) + ")");

        // Wrist singularity: J5 = 0.0 deg (axes 4 and 6 become collinear)
        auto jWristSing = robot.homeJointsRad;
        jWristSing[4] = 0.0;
        isSing = kin.checkSingularity(jWristSing, manip);
        check(isSing, "Singularity detected when wrist J5 = 0° (axes 4 and 6 collinear)");
        check(manip < 0.001, "Manipulability index drops to ~0 at wrist singularity (actual: " + std::to_string(manip) + ")");

        // Elbow singularity: arm in collinear extension (q2 + psi = 0)
        const double psi = std::atan2(robot.opw.a2, robot.opw.c3);
        auto qElbow = kin.displayToModelJoints(robot.homeJointsRad);
        qElbow[2] = -psi;
        const auto jElbowSing = kin.modelToDisplayJoints(qElbow);
        isSing = kin.checkSingularity(jElbowSing, manip);
        check(isSing, "Elbow singularity correctly detected when forearm is in collinear extension");

        // Shoulder singularity: wrist center on J1 rotation axis
        auto jShoulderSing = robot.homeJointsRad;
        jShoulderSing[1] = olp::core::deg2rad(45.0);
        jShoulderSing[2] = olp::core::deg2rad(-120.0);
        kin.checkSingularity(jShoulderSing, manip);
        check(manip >= 0.0 && manip <= 1.0, "Manipulability index stays strictly bounded in [0, 1]");
    }

    // 3. FANUC Configuration Consistency & Configuration Jump Detection
    {
        const auto utoolOffset = olp::core::Transform4d::fromTranslation(0.0, 0.0, 150.0);
        const auto fkRes = kin.forwardKinematics(robot.homeJointsRad, utoolOffset);
        const auto homeConfig = fkRes.config;

        const auto ikSols = kin.inverseKinematics(fkRes.tcpPoseInBase, utoolOffset, false);
        check(ikSols.size() >= 2, "Inverse kinematics returns multiple candidate solutions (count: " + std::to_string(ikSols.size()) + ")");

        const auto chosen = kin.selectBestSolution(ikSols, robot.homeJointsRad, homeConfig, true);
        check(chosen.has_value(), "selectBestSolution finds matching solution with locked home configuration");
        if (chosen.has_value()) {
            check(chosen->config.flip == homeConfig.flip, "Selected solution matches locked flip configuration");
            check(chosen->config.up == homeConfig.up, "Selected solution matches locked up configuration");
            check(chosen->config.top == homeConfig.top, "Selected solution matches locked top configuration");
        }

        olp::core::FanucConfig impossibleConfig = homeConfig;
        impossibleConfig.flip = !homeConfig.flip;
        impossibleConfig.up = !homeConfig.up;
        impossibleConfig.top = !homeConfig.top;
        const auto noMatch = kin.selectBestSolution(ikSols, robot.homeJointsRad, impossibleConfig, true);
        if (!noMatch.has_value()) {
            check(true, "ConfigJump prevented: selectBestSolution returns nullopt when locked configuration cannot be preserved");
        } else {
            check(noMatch->config.flip == impossibleConfig.flip, "Found candidate matching forced configuration");
        }
    }

    // 4. Wire-Tip Collision Exemption vs Non-Welding Collision
    {
        olp::core::UnitsSettings settings;
        settings.collisionSafeClearanceMm = 5.0;
        olp::algo::CollisionAndSimEngine simEngine(kin, settings);

        olp::core::SceneNode workpieceNode;
        workpieceNode.id = "mock_workpiece";
        workpieceNode.name = "WorkpiecePlate";
        workpieceNode.type = olp::core::SceneNodeType::Part;
        workpieceNode.collisionRole = olp::core::CollisionRole::Workpiece;
        workpieceNode.visible = true;
        workpieceNode.renderMesh.bboxMin = {0.0, 0.0, 0.0};
        workpieceNode.renderMesh.bboxMax = {200.0, 200.0, 20.0};
        workpieceNode.collisionMesh.bboxMin = {0.0, 0.0, 0.0};
        workpieceNode.collisionMesh.bboxMax = {200.0, 200.0, 20.0};

        olp::core::SceneNode clampNode;
        clampNode.id = "mock_clamp";
        clampNode.name = "Fixture_Clamp_Left";
        clampNode.type = olp::core::SceneNodeType::Part;
        clampNode.collisionRole = olp::core::CollisionRole::Fixture;
        clampNode.visible = true;
        clampNode.renderMesh.bboxMin = {100.0, -50.0, 40.0};
        clampNode.renderMesh.bboxMax = {160.0, 50.0, 80.0};
        clampNode.collisionMesh.bboxMin = {100.0, -50.0, 40.0};
        clampNode.collisionMesh.bboxMax = {160.0, 50.0, 80.0};

        std::vector<olp::core::SceneNode> scene = {workpieceNode, clampNode};

        olp::core::Transform4d weldTcp;
        weldTcp.pos = {100.0, 100.0, 20.0};
        weldTcp.rot = olp::core::Mat3::rotX(olp::core::kPi);

        double clearance = 0.0;
        std::string hitPart;

        bool collided = simEngine.checkTorchPoseCollision(
            weldTcp, scene, "mock_workpiece", true, clearance, hitPart, {"mock_workpiece"});
        check(!collided, "Wire-tip collision exemption: welding role touches workpiece without false collision alarm");

        collided = simEngine.checkTorchPoseCollision(
            weldTcp, scene, "mock_workpiece", false, clearance, hitPart, {});
        check(collided || clearance < 5.0, "Non-welding role at same position correctly detects contact/near-collision");

        olp::core::Transform4d clampHitTcp;
        clampHitTcp.pos = {130.0, 0.0, 20.0};
        clampHitTcp.rot = olp::core::Mat3::rotX(olp::core::kPi);
        collided = simEngine.checkTorchPoseCollision(
            clampHitTcp, scene, "mock_workpiece", true, clearance, hitPart, {"mock_workpiece"});
        check(collided, "Torch nozzle colliding with fixture clamp is strictly detected as collision");
        check(hitPart == "Fixture_Clamp_Left", "Colliding part accurately identified as Fixture_Clamp_Left (got: " + hitPart + ")");
    }

    // 5. 72-Sample Twist Angle Feasibility Scan (scanTwistAngles)
    {
        olp::core::UnitsSettings settings;
        olp::algo::CollisionAndSimEngine simEngine(kin, settings);

        const auto robotBase = olp::core::Transform4d::fromTranslation(0.0, 0.0, 500.0);
        const auto utoolOffset = olp::core::Transform4d::fromTranslation(0.0, 0.0, 150.0);

        const auto fkHome = kin.forwardKinematics(robot.homeJointsRad, utoolOffset);
        const olp::core::Transform4d targetPose = robotBase * fkHome.tcpPoseInBase;

        const auto samples = simEngine.scanTwistAngles(
            targetPose,
            robotBase,
            utoolOffset,
            robot.homeJointsRad,
            kin.computeConfig(robot.homeJointsRad),
            {},
            "",
            5.0);

        check(samples.size() == 72, "Twist angle scan produces exactly 72 samples spanning [-180°, 175°]");
        if (samples.size() == 72) {
            check(std::abs(samples.front().twistAngleDeg - (-180.0)) < 1e-4, "First sample angle is -180.0°");
            check(std::abs(samples.back().twistAngleDeg - (175.0)) < 1e-4, "Last sample angle is +175.0°");

            size_t reachableCount = 0;
            for (const auto& s : samples) {
                if (s.reachable) ++reachableCount;
            }
            check(reachableCount > 0, "Twist angle scan identifies reachable sector (reachable: " + std::to_string(reachableCount) + "/72)");
        }

#ifdef OLP_HAS_QT6
        olp::ui::TwistScanWidget widget;
        std::vector<olp::algo::TwistScanSample> testSamples;
        for (double d = -180.0; d < 180.0 - 1e-5; d += 5.0) {
            olp::algo::TwistScanSample ts;
            ts.twistAngleDeg = d;
            if (d >= -30.0 && d <= 50.0) {
                ts.reachable = true;
                ts.collisionFree = true;
                ts.nearSingularityOrLimit = false;
                ts.minClearanceMm = 15.0;
            } else {
                ts.reachable = false;
                ts.collisionFree = false;
            }
            testSamples.push_back(ts);
        }
        widget.setSamples(testSamples, 0.0);
        const auto opt = widget.findOptimalFeasibleAngleDeg();
        check(opt.has_value(), "TwistScanWidget finds optimal feasible angle");
        if (opt.has_value()) {
            check(std::abs(*opt - 10.0) < 1e-4,
                  "Optimal feasible angle is accurately at center of [-30°, 50°] range (+10°, got: " + std::to_string(*opt) + "°)");
        }
#endif
    }

    // 6. Interactive MainWindow::validateOperation & Issue Navigation
    {
        olp::ui::MainWindow mainWin;
        olp::core::SeamDefinition seam;
        seam.id = "seam_m10";
        seam.name = "SeamM10";
        for (int i = 0; i < 3; ++i) {
            olp::core::SeamSampleFrame f;
            f.arcLengthMm = i * 30.0;
            f.position = {600.0 + i * 30.0, 0.0, 500.0};
            f.tangentX = {1.0, 0.0, 0.0};
            f.normalFace1 = {0.0, 1.0, 0.0};
            f.normalFace2 = {0.0, 0.0, 1.0};
            f.bisector = {0.0, 0.70710678, 0.70710678};
            f.frame = olp::core::Transform4d::identity();
            f.frame.pos = f.position;
            f.frame.rot = olp::core::Mat3::rotX(olp::core::deg2rad(180.0));
            seam.sampledFrames.push_back(f);
        }
        mainWin.document().seams.push_back(seam);

        olp::core::SceneNode robotNode;
        robotNode.id = "robot_m10";
        robotNode.type = olp::core::SceneNodeType::Robot;
        robotNode.localTransform = olp::core::Transform4d::fromTranslation(0.0, 0.0, 500.0);
        mainWin.document().sceneNodes.push_back(robotNode);

        olp::core::ToolpathGenParams genParams;
        genParams.endpointsOnlyForLine = false;
        const bool genOk = mainWin.generateOperationFromSelectedSeam(&genParams);
        check(genOk, "MainWindow::generateOperationFromSelectedSeam succeeds");
        check(!mainWin.selectedOperationId().empty(), "Operation is selected after generation");

        const auto simResult = mainWin.validateOperation(mainWin.selectedOperationId());
        check(mainWin.lastSimulationResult().frames.size() == simResult.frames.size(),
              "MainWindow caches lastSimulationResult");

        const auto* op = mainWin.document().findOperation(mainWin.selectedOperationId());
        check(op != nullptr, "Generated operation found in document");
        if (op && !op->targets.empty()) {
            check(op->targets.front().cache.valid, "First target cache is marked valid after validation");
            check(op->targets.front().cache.status != olp::core::TargetValidationState::Unchecked,
                  "First target cache status is evaluated (not Unchecked)");

            // Use WELD_START (target 1 on the seam, z=500) for rotation, SLERP, and exemption tests
            const std::string t0Id = (op->targets.size() > 1) ? op->targets[1].id : op->targets.front().id;
            mainWin.selectTarget(t0Id, op->id);

            // M10-001: Return value, robot joints sync, and Undo/Redo cache symmetry
            const bool zeroRot = mainWin.rotateTargetsTcpZ({t0Id}, 0.0);
            check(!zeroRot, "M10-001: Zero-delta rotation returns false and is not stacked");

            const auto origPose = mainWin.document().findTarget(t0Id)->poseInUFrame;
            const auto origJoints = mainWin.document().findTarget(t0Id)->cache.jointsRad;
            const bool origUserMod = mainWin.document().findTarget(t0Id)->userModified;

            const bool rotOk = mainWin.rotateTargetsTcpZ({t0Id}, olp::core::deg2rad(15.0));
            check(rotOk, "M10-001: rotateTargetsTcpZ returns true when actually mutated");
            check(mainWin.document().findTarget(t0Id)->userModified, "M10-001: Target marked userModified after rotation");
            check(mainWin.document().findTarget(t0Id)->cache.valid, "M10-001: Target cache re-validated and valid after rotation");

            const auto* rNode = mainWin.document().findSceneNode("robot_m10");
            check(rNode != nullptr, "Robot node found in scene");
            if (rNode) {
                check(rNode->currentJointsRad == mainWin.document().findTarget(t0Id)->cache.jointsRad,
                      "M10-001: Robot node currentJointsRad matches newly validated target cache joints");
            }

            // Undo: pose, userModified, cache and robot joints restore exactly
            mainWin.commandManager().undo();
            const auto* tAfterUndo = mainWin.document().findTarget(t0Id);
            check((tAfterUndo->poseInUFrame.pos - origPose.pos).norm() < 1e-4, "M10-001: Target position restored after undo");
            check(std::abs(tAfterUndo->poseInUFrame.toWPR().wDeg - origPose.toWPR().wDeg) < 1e-4,
                  "M10-001: Target orientation restored after undo");
            check(tAfterUndo->userModified == origUserMod, "M10-001: userModified restored after undo");
            check(tAfterUndo->cache.valid, "M10-001: Target cache valid restored after undo");
            check(tAfterUndo->cache.jointsRad == origJoints, "M10-001: Target cache joints restored after undo");
            if (rNode) {
                check(rNode->currentJointsRad == origJoints, "M10-001: Robot node joints restored to match undone cache joints");
            }

            // Redo: pose, userModified, cache and robot joints match redone state
            mainWin.commandManager().redo();
            const auto* tAfterRedo = mainWin.document().findTarget(t0Id);
            check(tAfterRedo->cache.valid, "M10-001: Target cache valid after redo");
            check(tAfterRedo->userModified, "M10-001: userModified true after redo");
            if (rNode) {
                check(rNode->currentJointsRad == tAfterRedo->cache.jointsRad,
                      "M10-001: Robot node joints match redone cache joints");
            }

            // SLERP smoothing across rotated WELD_START and unrotated WELD_END
            const bool smoothOk = mainWin.smoothTargetOrientations();
            check(smoothOk, "M10-001: smoothTargetOrientations returns true when targets smoothed");

            // M10-002: scanSelectedTargetTwistAngles locks solvedConfig and distinguishes welding vs approach role
            const auto targetSamples = mainWin.scanSelectedTargetTwistAngles(5.0);
            check(targetSamples.size() == 72, "M10-002: scanSelectedTargetTwistAngles returns 72 samples for selected target");

            auto itZero = std::find_if(targetSamples.begin(), targetSamples.end(),
                [](const olp::algo::TwistScanSample& s) { return std::abs(s.twistAngleDeg) < 1e-4; });
            check(itZero != targetSamples.end() && itZero->reachable,
                  "M10-002: Current angle 0° is reachable under locked solvedConfig");

            // Workpiece at weld root (z = 500)
            olp::core::SceneNode wpNode;
            wpNode.id = "wp_test_m10";
            wpNode.name = "WorkpiecePlate";
            wpNode.type = olp::core::SceneNodeType::Part;
            wpNode.collisionRole = olp::core::CollisionRole::Workpiece;
            wpNode.visible = true;
            wpNode.collisionMesh.bboxMin = {550.0, -100.0, 480.0};
            wpNode.collisionMesh.bboxMax = {750.0, 100.0, 500.0};
            mainWin.document().sceneNodes.push_back(wpNode);

            auto* opMut = mainWin.document().findOperation(mainWin.selectedOperationId());
            opMut->targetWorkpieceId = "wp_test_m10";

            auto scanWeld = mainWin.scanSelectedTargetTwistAngles(5.0);
            auto itZeroWeld = std::find_if(scanWeld.begin(), scanWeld.end(),
                [](const olp::algo::TwistScanSample& s) { return std::abs(s.twistAngleDeg) < 1e-4; });
            check(itZeroWeld != scanWeld.end() && itZeroWeld->collisionFree,
                  "M10-002: Welding role wire tip touches workpiece -> collisionFree = true (exemption)",
                  itZeroWeld != scanWeld.end() ? ("clearance=" + std::to_string(itZeroWeld->minClearanceMm)) : "not found");

            // Change role to APPROACH at identical geometric position -> wire tip is NOT exempted
            auto* targetMut = mainWin.document().findTarget(t0Id);
            const auto origRole = targetMut->role;
            targetMut->role = olp::core::TargetRole::APPROACH;
            auto scanApproach = mainWin.scanSelectedTargetTwistAngles(5.0);
            auto itZeroAppr = std::find_if(scanApproach.begin(), scanApproach.end(),
                [](const olp::algo::TwistScanSample& s) { return std::abs(s.twistAngleDeg) < 1e-4; });
            check(itZeroAppr != scanApproach.end() && !itZeroAppr->collisionFree,
                  "M10-002: APPROACH role at same pose touches workpiece -> collisionFree = false (no exemption)");
            targetMut->role = origRole;

            // M10-003: Mid-segment sweep collision appears in SimulationResult and ShellWindow issue list
            // Place obstacle at the midpoint between target 1 and target 2 along torch body offset (95mm)
            const auto midPos = (op->targets[1].poseInUFrame.pos + op->targets[2].poseInUFrame.pos) * 0.5;
            const auto midRot = op->targets[1].poseInUFrame.rot;
            const olp::core::Vec3 clampCenter = midPos + (-midRot.colZ()) * 95.0;
            constexpr double h = 8.0;

            olp::core::SceneNode sweepClamp;
            sweepClamp.id = "sweep_clamp_m10";
            sweepClamp.name = "Fixture_Clamp_Middle";
            sweepClamp.type = olp::core::SceneNodeType::Part;
            sweepClamp.collisionRole = olp::core::CollisionRole::Fixture;
            sweepClamp.visible = true;
            sweepClamp.collisionMesh.bboxMin = clampCenter - olp::core::Vec3{h, h, h};
            sweepClamp.collisionMesh.bboxMax = clampCenter + olp::core::Vec3{h, h, h};
            mainWin.document().sceneNodes.push_back(sweepClamp);

            const auto simSweep = mainWin.validateOperation(mainWin.selectedOperationId());
            bool foundSweepIssue = false;
            std::string allIssues;
            for (const auto& issue : simSweep.issues) {
                allIssues += issue.description + " | ";
                if (issue.description.find("Fixture_Clamp_Middle") != std::string::npos) {
                    foundSweepIssue = true;
                    break;
                }
            }
            check(foundSweepIssue, "M10-003: SimulationResult reports sweep collision against mid-segment clamp", allIssues);

#ifdef OLP_HAS_QT6
            olp::ui::ShellWindow shell(mainWin);
            const auto listWidgets = shell.findChildren<QListWidget*>();
            bool shellHasSweepIssue = false;
            for (auto* lw : listWidgets) {
                for (int i = 0; i < lw->count(); ++i) {
                    auto* item = lw->item(i);
                    if (item && item->text().contains("Fixture_Clamp_Middle")) {
                        shellHasSweepIssue = true;
                        check(!item->data(Qt::UserRole).toString().isEmpty(), "M10-003: Issue item carries targetId");
                        check(!item->data(Qt::UserRole + 1).toString().isEmpty(), "M10-003: Issue item carries operationId");
                        break;
                    }
                }
                if (shellHasSweepIssue) break;
            }
            check(shellHasSweepIssue, "M10-003: ShellWindow issueList_ displays sweep collision item for navigation");
#endif

            // M10-007: Undo/Redo restores downstream targets' caches and lastSimulationResult.issues
            const auto target2Id = op->targets[2].id;
            const auto target3Id = op->targets[3].id;
            const auto t2JointsBefore = mainWin.document().findTarget(target2Id)->cache.jointsRad;
            const auto t2ConfigBefore = mainWin.document().findTarget(target2Id)->cache.solvedConfig.toString();
            const auto t3JointsBefore = mainWin.document().findTarget(target3Id)->cache.jointsRad;
            std::vector<std::string> issuesBefore;
            for (const auto& iss : mainWin.lastSimulationResult().issues) {
                issuesBefore.push_back(iss.targetId + ":" + iss.description);
            }

            // Perform rotation on Target 1 (WELD_START)
            const bool rotAgainOk = mainWin.rotateTargetsTcpZ({t0Id}, olp::core::deg2rad(20.0));
            check(rotAgainOk, "M10-007: Rotate target 1 by 20 deg succeeds");
            const auto t2JointsAfter = mainWin.document().findTarget(target2Id)->cache.jointsRad;
            const auto t2ConfigAfter = mainWin.document().findTarget(target2Id)->cache.solvedConfig.toString();
            const auto t3JointsAfter = mainWin.document().findTarget(target3Id)->cache.jointsRad;
            std::vector<std::string> issuesAfter;
            for (const auto& iss : mainWin.lastSimulationResult().issues) {
                issuesAfter.push_back(iss.targetId + ":" + iss.description);
            }

            // Undo: Downstream targets' caches and lastSimulationResult.issues must match pre-rotation exactly
            mainWin.commandManager().undo();
            const auto t2JointsUndone = mainWin.document().findTarget(target2Id)->cache.jointsRad;
            const auto t2ConfigUndone = mainWin.document().findTarget(target2Id)->cache.solvedConfig.toString();
            const auto t3JointsUndone = mainWin.document().findTarget(target3Id)->cache.jointsRad;
            std::vector<std::string> issuesUndone;
            for (const auto& iss : mainWin.lastSimulationResult().issues) {
                issuesUndone.push_back(iss.targetId + ":" + iss.description);
            }
            check(t2JointsUndone == t2JointsBefore, "M10-007: Downstream target 2 joints restored after undo");
            check(t2ConfigUndone == t2ConfigBefore, "M10-007: Downstream target 2 config restored after undo");
            check(t3JointsUndone == t3JointsBefore, "M10-007: Downstream target 3 joints restored after undo");
            check(issuesUndone == issuesBefore, "M10-007: lastSimulationResult.issues restored after undo");

            // Redo: Downstream targets' caches and lastSimulationResult.issues must match post-rotation exactly
            mainWin.commandManager().redo();
            const auto t2JointsRedone = mainWin.document().findTarget(target2Id)->cache.jointsRad;
            const auto t2ConfigRedone = mainWin.document().findTarget(target2Id)->cache.solvedConfig.toString();
            const auto t3JointsRedone = mainWin.document().findTarget(target3Id)->cache.jointsRad;
            std::vector<std::string> issuesRedone;
            for (const auto& iss : mainWin.lastSimulationResult().issues) {
                issuesRedone.push_back(iss.targetId + ":" + iss.description);
            }
            check(t2JointsRedone == t2JointsAfter, "M10-007: Downstream target 2 joints match after redo");
            check(t2ConfigRedone == t2ConfigAfter, "M10-007: Downstream target 2 config matches after redo");
            check(t3JointsRedone == t3JointsAfter, "M10-007: Downstream target 3 joints match after redo");
            check(issuesRedone == issuesAfter, "M10-007: lastSimulationResult.issues match after redo");

            // Undoing operation generation removes deleted targets and leaves zero stale issues
            while (mainWin.commandManager().canUndo() && mainWin.document().findOperation(mainWin.selectedOperationId()) != nullptr) {
                mainWin.commandManager().undo();
            }
            check(mainWin.lastSimulationResult().issues.empty(), "M10-007: lastSimulationResult.issues cleared after undoing operation creation");
#ifdef OLP_HAS_QT6
            shell.refreshFromDocument();
            bool hasDeletedTargetIssue = false;
            for (auto* lw : listWidgets) {
                for (int i = 0; i < lw->count(); ++i) {
                    if (lw->item(i)->data(Qt::UserRole).toString() == QString::fromStdString(t0Id)) {
                        hasDeletedTargetIssue = true;
                        break;
                    }
                }
            }
            check(!hasDeletedTargetIssue, "M10-007: Deleted operation targets do not appear in ShellWindow issue list");
#endif
        }
    }
}


