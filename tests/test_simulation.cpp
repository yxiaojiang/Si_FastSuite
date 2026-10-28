#include "test_support.h"

#include "olp/algo/OpwKinematics.h"
#include "olp/algo/TrajectorySimulator.h"
#include "olp/device/RobotModel.h"
#include "olp/app/CommandManager.h"
#include "olp/cad/OcctCadKernel.h"
#include "olp/ui/OcctViewport.h"
#include "olp/algo/FclCollisionQueries.h"
#include "olp/ui/MainWindow.h"
#include "olp/ui/TimelinePanel.h" 

#include <cmath>
#include <iostream>
#include <string>

namespace {

olp::core::Target makeTarget(
    const std::string& id,
    olp::core::MotionType motion,
    double speed,
    const olp::core::Transform4d& pose) {
    olp::core::Target target;
    target.id = id;
    target.name = id;
    target.motion = motion;
    target.speedValue = speed;
    target.poseInUFrame = pose;
    return target;
}

double maxFrameGap(const std::vector<olp::algo::BakedFrame>& frames) {
    double gap = 0.0;
    for (size_t i = 1; i < frames.size(); ++i) {
        gap = std::max(gap, frames[i].timeSec - frames[i - 1].timeSec);
    }
    return gap;
}

void checkShortWrap(
    const olp::algo::TrajectorySimulator& simulator,
    const olp::algo::OpwKinematics& kin,
    std::array<double, 6> startJoints,
    int axis,
    double goalDeg,
    const std::string& label) {
    const auto id = olp::core::Transform4d::identity();
    auto goalJoints = startJoints;
    goalJoints[static_cast<size_t>(axis)] = olp::core::deg2rad(goalDeg);
    const auto tcp0 = kin.forwardKinematics(startJoints, id).tcpPoseInBase;
    const auto tcp1 = kin.forwardKinematics(goalJoints, id).tcpPoseInBase;
    olp::core::Program program;
    program.id = label;
    program.startState.homeJointsRad = startJoints;
    olp::core::Operation op;
    op.id = label;
    auto target = makeTarget(label, olp::core::MotionType::L, 50.0, tcp1);
    target.lockConfig = true;
    target.config = kin.computeConfig(startJoints);
    op.targets.push_back(target);
    program.operations.push_back(op);
    const auto plan = simulator.plan(program, id, id, id, 30.0);
    std::string issues;
    for (const auto& issue : plan.issues) {
        if (!issues.empty()) {
            issues += " | ";
        }
        issues += issue.description;
    }
    check(plan.segments.size() == 1 && !plan.hasConfigJumpInLinear,
          label + " keeps the short step without a missing inverse",
          issues);
    bool onSlerp = plan.segments.size() == 1 && !plan.segments[0].samples.empty();
    double worstMm = 0.0;
    double worstOriDeg = 0.0;
    double worstJointDeg = 0.0;
    if (onSlerp) {
        const auto& samples = plan.segments[0].samples;
        for (size_t i = 0; i < samples.size(); ++i) {
            const auto fk = kin.forwardKinematics(samples[i].jointsRad, id).tcpPoseInBase;
            const auto expected = olp::core::Transform4d::interpolateSlerp(tcp0, tcp1, samples[i].s);
            worstMm = std::max(worstMm, (fk.pos - expected.pos).norm());
            worstOriDeg = std::max(
                worstOriDeg, olp::core::rad2deg(expected.rot.angularDistanceRad(fk.rot)));
            if (i == 0) {
                continue;
            }
            for (size_t joint = 0; joint < 6; ++joint) {
                worstJointDeg = std::max(
                    worstJointDeg,
                    olp::core::rad2deg(std::abs(samples[i].jointsRad[joint] - samples[i - 1].jointsRad[joint])));
            }
        }
        if (worstMm > 0.5 || worstOriDeg > 1.0 || worstJointDeg > 15.0) {
            onSlerp = false;
        }
    }
    check(onSlerp, label + " stays on the spherical interpolation",
          "mm=" + std::to_string(worstMm) + " ori=" + std::to_string(worstOriDeg) +
              " joint=" + std::to_string(worstJointDeg));
}

} // namespace

void testTrajectoryAndPlayback() {
    std::cout << "\n--- Test 18: M11 trapezoid, J/L interpolation, cycle time and playback ---\n";

    const auto tri = olp::algo::makeTrapezoid(1.0, 2.0, 4.0);
    check(std::abs(tri.duration - 1.0) < 1e-9, "Triangular profile duration is 1 s for distance 1, v=2, a=4");
    check(std::abs(tri.position(0.5) - 0.5) < 1e-9, "Triangular profile is halfway at half time");
    check(std::abs(tri.position(1.0) - 1.0) < 1e-9, "Triangular profile ends on the target distance");

    const auto trap = olp::algo::makeTrapezoid(1.0, 1.0, 4.0);
    check(std::abs(trap.tCruise - 0.75) < 1e-9, "Trapezoid cruise lasts 0.75 s");
    check(std::abs(trap.duration - 1.25) < 1e-9, "Trapezoid duration is 1.25 s");
    check(std::abs(trap.position(1.0) - 0.875) < 1e-9, "Trapezoid position at t=1 s is 0.875");
    check(trap.velocity(0.5) <= 1.0 + 1e-9, "Trapezoid cruise velocity stays at the limit");

    const auto model = olp::device::RobotModel::createFanucM10iA();
    olp::algo::OpwKinematics kin(model);
    const auto home = model.homeJointsRad;
    auto q1 = home;
    q1[0] += 0.5;
    const auto id = olp::core::Transform4d::identity();
    const auto tcpHome = kin.forwardKinematics(home, id).tcpPoseInBase;
    const auto tcpJ = kin.forwardKinematics(q1, id).tcpPoseInBase;

    olp::core::Program jointProgram;
    jointProgram.id = "prog_j";
    jointProgram.startState.homeJointsRad = home;
    olp::core::Operation jointOp;
    jointOp.id = "op_j";
    jointOp.name = "Joint";
    jointOp.targets.push_back(makeTarget("j_move", olp::core::MotionType::J, 100.0, tcpJ));
    jointProgram.operations.push_back(jointOp);

    olp::algo::TrajectorySimulator simulator(kin, model);
    const auto jointPlan = simulator.plan(jointProgram, id, id, id, 30.0);
    check(jointPlan.segments.size() == 1, "A single joint move produces one segment");
    const double vS = model.jointLimits[0].maxVelRadPerSec / 0.5;
    const double aS = model.jointLimits[0].maxAccRadPerSec2 / 0.5;
    const auto expectedJ = olp::algo::makeTrapezoid(1.0, vS, aS);
    check(!jointPlan.segments.empty() &&
              std::abs(jointPlan.segments[0].durationSec - expectedJ.duration) < 1e-6,
          "Synchronized J duration matches the slowest axis trapezoid");
    check(!jointPlan.segments.empty() &&
              olp::algo::OpwKinematics::weightedJointDistance(
                  jointPlan.segments[0].samples.back().jointsRad, q1) < 1e-4,
          "Joint move ends on the posture whose FK was commanded");

    const auto mid = olp::algo::sampleTrajectory(jointPlan, kin, expectedJ.duration * 0.5);
    check(std::abs(mid.jointsRad[0] - 0.25) < 1e-5, "All axes share one path parameter: J1 is halfway at half time");
    bool othersHeld = true;
    for (size_t axis = 1; axis < 6; ++axis) {
        if (std::abs(mid.jointsRad[axis] - home[axis]) > 1e-5) {
            othersHeld = false;
        }
    }
    check(othersHeld, "Axes with no command stay put while J1 moves");

    double peak = 0.0;
    if (!jointPlan.segments.empty()) {
        for (int i = 1; i < 20; ++i) {
            const double t = expectedJ.duration * static_cast<double>(i) / 20.0;
            peak = std::max(peak, jointPlan.segments[0].profile.velocity(t) * 0.5);
        }
    }
    check(peak <= model.jointLimits[0].maxVelRadPerSec + 1e-6,
          "J1 speed stays within the joint velocity limit");

    jointProgram.operations[0].targets[0].speedValue = 50.0;
    const auto slower = simulator.plan(jointProgram, id, id, id, 30.0);
    check(!slower.segments.empty() && !jointPlan.segments.empty() &&
              slower.segments[0].durationSec > jointPlan.segments[0].durationSec + 1e-4,
          "A 50% joint override takes longer than 100%");

    auto goal = tcpHome;
    goal.pos.x += 20.0;
    olp::core::Program lineProgram;
    lineProgram.id = "prog_l";
    lineProgram.startState.homeJointsRad = home;
    olp::core::Operation lineOp;
    lineOp.id = "op_l";
    lineOp.targets.push_back(makeTarget("line", olp::core::MotionType::L, 10.0, goal));
    lineProgram.operations.push_back(lineOp);
    const auto linePlan = simulator.plan(lineProgram, id, id, id, 30.0);
    const auto cart = olp::algo::makeTrapezoid(
        1.0, 10.0 / 20.0, olp::algo::kEstimatedCartesianAccelMmPerSec2 / 20.0);
    check(!linePlan.segments.empty() &&
              linePlan.segments[0].durationSec + 1e-4 >= 20.0 / 10.0,
          "Linear move is not faster than the programmed mm/s");
    check(!linePlan.segments.empty() &&
              linePlan.segments[0].durationSec + 1e-4 >= cart.duration,
          "Programmed 10 mm/s is the binding limit for a short slow line");
    bool onLine = !linePlan.segments.empty();
    double worstLineMm = 0.0;
    if (!linePlan.segments.empty()) {
        const olp::core::Vec3 ab = goal.pos - tcpHome.pos;
        for (int i = 1; i <= 4; ++i) {
            const auto frame = olp::algo::sampleTrajectory(
                linePlan, kin, linePlan.segments[0].durationSec * i / 5.0);
            const olp::core::Vec3 ap = frame.tcpWorldPos - tcpHome.pos;
            const double dist = ab.norm() > 1e-9 ? ap.cross(ab).norm() / ab.norm() : ap.norm();
            worstLineMm = std::max(worstLineMm, dist);
            if (dist > 2.0) {
                onLine = false;
            }
        }
    }
    check(onLine, "Linear TCP stays within 2 mm of the commanded segment",
          "worst=" + std::to_string(worstLineMm));
    const auto lineEnd = olp::algo::sampleTrajectory(linePlan, kin, linePlan.totalCycleTimeSec);
    check((lineEnd.tcpWorldPos - goal.pos).norm() < 0.5, "Linear move finishes on the commanded TCP");
    check(!linePlan.hasConfigJumpInLinear, "A 20 mm line does not trip the 15 degree joint-step check");

    auto acrossStart = home;
    acrossStart[5] = olp::core::deg2rad(179.0);
    auto acrossGoalJoints = acrossStart;
    acrossGoalJoints[5] = olp::core::deg2rad(-179.0);
    const auto tcpAcrossStart = kin.forwardKinematics(acrossStart, id).tcpPoseInBase;
    const auto tcpAcrossGoal = kin.forwardKinematics(acrossGoalJoints, id).tcpPoseInBase;
    olp::core::Program acrossProgram;
    acrossProgram.id = "prog_wrap";
    acrossProgram.startState.homeJointsRad = acrossStart;
    olp::core::Operation acrossOp;
    acrossOp.id = "op_wrap";
    auto wrapTarget = makeTarget("wrap", olp::core::MotionType::L, 50.0, tcpAcrossGoal);
    wrapTarget.lockConfig = true;
    wrapTarget.config = kin.computeConfig(acrossStart);
    acrossOp.targets.push_back(wrapTarget);
    acrossProgram.operations.push_back(acrossOp);
    const auto acrossPlan = simulator.plan(acrossProgram, id, id, id, 30.0);
    check(acrossPlan.segments.size() == 1, "A line across +/-180 degrees stays one segment");
    std::string acrossIssue;
    for (const auto& issue : acrossPlan.issues) {
        if (!acrossIssue.empty()) {
            acrossIssue += " | ";
        }
        acrossIssue += issue.description;
    }
    check(!acrossPlan.hasConfigJumpInLinear,
          "A short joint step across +/-180 degrees does not trip the 15 degree check",
          acrossIssue);
    bool acrossShort = acrossPlan.segments.size() == 1 && !acrossPlan.segments[0].samples.empty();
    double acrossWorstDeg = 0.0;
    if (acrossShort) {
        const auto& samples = acrossPlan.segments[0].samples;
        for (size_t i = 1; i < samples.size(); ++i) {
            for (size_t axis = 0; axis < 6; ++axis) {
                acrossWorstDeg = std::max(
                    acrossWorstDeg,
                    olp::core::rad2deg(std::abs(samples[i].jointsRad[axis] - samples[i - 1].jointsRad[axis])));
            }
        }
        const double endTravel = std::abs(samples.back().jointsRad[5] - samples.front().jointsRad[5]);
        if (endTravel > olp::core::deg2rad(15.0) || acrossWorstDeg > 15.0) {
            acrossShort = false;
        }
    }
    check(acrossShort,
          "Linear samples across +/-180 degrees use the short joint step",
          "worst=" + std::to_string(acrossWorstDeg));
    const double acrossDuration = acrossPlan.segments.empty() ? 0.0 : acrossPlan.segments[0].durationSec;
    check(acrossDuration > 0.0 && acrossDuration < 0.5,
          "Cycle time across +/-180 degrees follows the short step, not a full turn");
    bool acrossTracked = false;
    if (!acrossPlan.segments.empty()) {
        const auto midFrame = olp::algo::sampleTrajectory(acrossPlan, kin, acrossDuration * 0.5);
        const auto midPose = olp::core::Transform4d::interpolateSlerp(tcpAcrossStart, tcpAcrossGoal, 0.5);
        const auto midFk = kin.forwardKinematics(midFrame.jointsRad, id).tcpPoseInBase;
        acrossTracked =
            std::abs(midFrame.jointsRad[5] - acrossStart[5]) < olp::core::deg2rad(10.0) &&
            midPose.rot.angularDistanceRad(midFk.rot) < olp::core::deg2rad(5.0);
    }
    check(acrossTracked, "Playback across +/-180 degrees follows the short orientation");

    auto j6Neg = home;
    j6Neg[5] = olp::core::deg2rad(-179.0);
    checkShortWrap(simulator, kin, j6Neg, 5, 179.0, "J6 from -179 to +179");
    auto j4Neg = home;
    j4Neg[3] = olp::core::deg2rad(-170.0);
    checkShortWrap(simulator, kin, j4Neg, 3, 170.0, "J4 from -170 to +170");
    auto j4Pos = home;
    j4Pos[3] = olp::core::deg2rad(170.0);
    checkShortWrap(simulator, kin, j4Pos, 3, -170.0, "J4 from +170 to -170");

    auto weldGoal = tcpHome;
    weldGoal.pos.x += 30.0;
    auto weldEnd = tcpHome;
    weldEnd.pos.x += 60.0;
    olp::core::Program weldProgram;
    weldProgram.id = "prog_w";
    weldProgram.startState.homeJointsRad = home;
    olp::core::Operation weldOp;
    weldOp.id = "op_weld";
    weldOp.name = "Weld";
    weldOp.targets.push_back(makeTarget("approach", olp::core::MotionType::J, 100.0, tcpJ));
    auto start = makeTarget("weld_start", olp::core::MotionType::L, 50.0, weldGoal);
    start.role = olp::core::TargetRole::WELD_START;
    start.events.push_back({olp::core::ProcessEventType::ArcStart, 1, 0.0, ""});
    auto end = makeTarget("weld_end", olp::core::MotionType::L, 50.0, weldEnd);
    end.role = olp::core::TargetRole::WELD_END;
    end.events.push_back({olp::core::ProcessEventType::ArcEnd, 1, 0.0, ""});
    auto retractPose = tcpHome;
    retractPose.pos.z += 20.0;
    auto retract = makeTarget("retract", olp::core::MotionType::L, 100.0, retractPose);
    retract.role = olp::core::TargetRole::RETRACT;
    weldOp.targets.push_back(start);
    weldOp.targets.push_back(end);
    weldOp.targets.push_back(retract);
    weldProgram.operations.push_back(weldOp);
    olp::core::Operation extra;
    extra.id = "op_air";
    extra.name = "Air";
    extra.targets.push_back(makeTarget("extra", olp::core::MotionType::J, 100.0, tcpJ));
    weldProgram.operations.push_back(extra);

    const auto weldPlan = simulator.plan(weldProgram, id, id, id, 30.0);
    check(weldPlan.segments.size() == 5, "Weld program keeps one segment per reachable target");
    double weldSum = 0.0;
    double airSum = 0.0;
    if (weldPlan.segments.size() == 5) {
        airSum = weldPlan.segments[0].durationSec + weldPlan.segments[1].durationSec +
                 weldPlan.segments[3].durationSec + weldPlan.segments[4].durationSec;
        weldSum = weldPlan.segments[2].durationSec;
    }
    check(std::abs(weldPlan.arcWeldTimeSec - weldSum) < 1e-8,
          "Weld time is only the motion after Arc Start and before Arc End");
    check(std::abs(weldPlan.airCutTimeSec - airSum) < 1e-8, "Air time is the rest of the cycle");
    check(std::abs(weldPlan.totalCycleTimeSec - (weldPlan.arcWeldTimeSec + weldPlan.airCutTimeSec)) < 1e-8,
          "Total cycle time is weld plus air");
    check(weldPlan.operations.size() == 2, "Cycle time is split per operation");
    double opSum = 0.0;
    for (const auto& row : weldPlan.operations) {
        opSum += row.totalTimeSec;
    }
    check(std::abs(opSum - weldPlan.totalCycleTimeSec) < 1e-8, "Operation totals add up to the cycle");

    check(!weldPlan.frames.empty() && weldPlan.frames.front().timeSec == 0.0,
          "Bake starts at t = 0 from the program start state");
    check(!weldPlan.frames.empty() &&
              olp::algo::OpwKinematics::weightedJointDistance(weldPlan.frames.front().jointsRad, home) < 1e-8,
          "The first frame is the start-state joint posture");
    check(!weldPlan.frames.empty() &&
              std::abs(weldPlan.frames.back().timeSec - weldPlan.totalCycleTimeSec) < 1e-6,
          "Bake ends at the cycle time");
    check(maxFrameGap(weldPlan.frames) <= (1.0 / 30.0) + 1e-6,
          "Baked frames are no coarser than 30 FPS");
    const auto& baked = weldPlan.frames.front();
    const auto fk = kin.forwardKinematics(baked.jointsRad, id);
    check((baked.linkWorldTransforms[6].pos - fk.linkTransforms[6].pos).norm() < 1e-6,
          "Baked link transforms match forward kinematics");

    olp::core::Program missing;
    missing.startState.homeJointsRad = home;
    olp::core::Operation missingOp;
    missingOp.id = "missing";
    auto far = tcpHome;
    far.pos = {1.0e6, 1.0e6, 1.0e6};
    missingOp.targets.push_back(makeTarget("nowhere", olp::core::MotionType::L, 100.0, far));
    missing.operations.push_back(missingOp);
    const auto missed = simulator.plan(missing, id, id, id, 30.0);
    check(!missed.issues.empty(), "An unreachable target is reported and does not throw");
    check(!missed.frames.empty() && missed.frames.front().timeSec == 0.0,
          "A failed target still leaves the start frame");

    olp::algo::PlaybackCursor cursor;
    cursor.bind(weldPlan, kin);
    cursor.stop();
    check(cursor.timeSec() == 0.0, "Stop returns playback to the start state");
    check(olp::algo::OpwKinematics::weightedJointDistance(cursor.sample().jointsRad, home) < 1e-8,
          "Stopped playback samples the start posture");
    cursor.play();
    cursor.advanceWall(0.1);
    check(std::abs(cursor.timeSec() - 0.1) < 1e-9, "Playing advances by wall time at 1x");
    cursor.pause();
    cursor.advanceWall(1.0);
    check(std::abs(cursor.timeSec() - 0.1) < 1e-9, "Pause ignores wall time");
    cursor.setSpeedMultiplier(2.0);
    cursor.play();
    cursor.advanceWall(0.1);
    check(std::abs(cursor.timeSec() - 0.3) < 1e-9, "2x playback advances twice the wall time");
    cursor.seek(-5.0);
    check(cursor.timeSec() == 0.0, "Seek clamps below zero");
    cursor.seek(1.0e9);
    check(std::abs(cursor.timeSec() - weldPlan.totalCycleTimeSec) < 1e-8, "Seek clamps at the cycle end");
    cursor.stop();
    cursor.stepTarget(1);
    check(!weldPlan.segments.empty() &&
              std::abs(cursor.timeSec() - weldPlan.segments[0].durationSec) < 1e-8,
          "Point step lands on the next target arrival");
    check(cursor.seekTarget("weld_end"), "Seek by target id finds the weld end");
    if (weldPlan.segments.size() >= 3) {
        const double arrival = weldPlan.segments[2].startTimeSec + weldPlan.segments[2].durationSec;
        check(std::abs(cursor.timeSec() - arrival) < 1e-8, "Seek target uses that point's arrival time");
    }
    const double weldArrival = cursor.timeSec();
    cursor.playFromStartIfFinished();
    check(cursor.state() == olp::algo::PlaybackState::Playing,
          "Play before the end keeps the current time playing");
    check(std::abs(cursor.timeSec() - weldArrival) < 1e-8,
          "Play before the end does not rewind");
    cursor.seek(1.0e9);
    cursor.playFromStartIfFinished();
    check(cursor.state() == olp::algo::PlaybackState::Playing, "Play from a finished cursor restarts");
    check(cursor.timeSec() == 0.0, "Restarting a finished playback returns to the start state");

#ifdef OLP_HAS_OCCT
    // M11-005 & M11-007: Viewport robot link presentation driven by URDF link chain and baked linkWorldTransforms
    olp::app::ProjectDocument viewportDoc;
    olp::cad::OcctCadKernel viewportCad;
    olp::core::SceneNode robotSceneNode;
    robotSceneNode.id = "robot_active_fanuc_m10ia";
    robotSceneNode.type = olp::core::SceneNodeType::Robot;
    robotSceneNode.collisionRole = olp::core::CollisionRole::RobotLink;
    robotSceneNode.robotModelId = "fanuc_m10ia";
    robotSceneNode.currentJointsRad = home;
    robotSceneNode.localTransform = olp::core::Transform4d::fromTranslation(0.0, 0.0, 500.0);
    viewportDoc.sceneNodes.push_back(robotSceneNode);

    olp::app::CommandManager cmdMgr(50);
    olp::ui::OcctViewport viewport(viewportDoc, cmdMgr, &viewportCad);
    viewport.syncScene();

    const auto initialTransforms = viewport.playbackRobotTransforms();
    const auto expectedUrdfHome = olp::algo::forwardRobotLinkWorldFrames(home, robotSceneNode.localTransform);

    // M11-007: Verify all 7 links in Home posture match URDF link chain (pos < 1 mm, rot < 1 deg)
    for (size_t k = 0; k < 7; ++k) {
        const double posErr = (initialTransforms[k].pos - expectedUrdfHome[k].pos).norm();
        check(posErr < 1.0,
              "M11-007: Initial link " + std::to_string(k) + " pos matches URDF chain (< 1 mm)");
        const double rotErrDeg = olp::core::rad2deg(initialTransforms[k].rot.angularDistanceRad(expectedUrdfHome[k].rot));
        check(rotErrDeg < 1.0,
              "M11-007: Initial link " + std::to_string(k) + " rot matches URDF chain (< 1 deg)");
    }
    // Reviewer explicit gate: link 4 position (< 1 mm, not OPW 640 mm error) and link 6 posture (< 1 deg, not 180 deg flipped)
    const double link4PosErr = (initialTransforms[4].pos - expectedUrdfHome[4].pos).norm();
    check(link4PosErr < 1.0,
          "M11-007: Home link 4 pos matches URDF within 1 mm (not OPW 640 mm wrist offset)");
    const double link6RotErrDeg = olp::core::rad2deg(initialTransforms[6].rot.angularDistanceRad(expectedUrdfHome[6].rot));
    check(link6RotErrDeg < 1.0,
          "M11-007: Home link 6 posture matches URDF within 1 deg (not OPW 180 deg flipped)");

    // M11-005: Sample mid-trajectory frame and drive viewport link transforms
    cursor.seek(weldPlan.totalCycleTimeSec * 0.5);
    const auto activeFrame = cursor.sample();
    viewport.setPlaybackRobotTransforms(activeFrame.linkWorldTransforms);

    const auto playingTransforms = viewport.playbackRobotTransforms();
    for (size_t k = 0; k < 7; ++k) {
        const double playPosErr = (playingTransforms[k].pos - activeFrame.linkWorldTransforms[k].pos).norm();
        check(playPosErr < 1e-3,
              "M11-005: Playing link " + std::to_string(k) + " pos follows baked frame linkWorldTransforms");
        const double playRotErrDeg = olp::core::rad2deg(playingTransforms[k].rot.angularDistanceRad(activeFrame.linkWorldTransforms[k].rot));
        check(playRotErrDeg < 1e-3,
              "M11-005: Playing link " + std::to_string(k) + " rot follows baked frame linkWorldTransforms");
    }

    // M11-005: Clear preview and verify all links reset back to initial URDF scene posture
    viewport.clearPlaybackPreview();
    const auto resetTransforms = viewport.playbackRobotTransforms();
    for (size_t k = 0; k < 7; ++k) {
        const double resetPosErr = (resetTransforms[k].pos - expectedUrdfHome[k].pos).norm();
        check(resetPosErr < 1e-3,
              "M11-005: clearPlaybackPreview resets link " + std::to_string(k) + " pos to scene posture");
        const double resetRotErrDeg = olp::core::rad2deg(resetTransforms[k].rot.angularDistanceRad(expectedUrdfHome[k].rot));
        check(resetRotErrDeg < 1e-3,
              "M11-005: clearPlaybackPreview resets link " + std::to_string(k) + " rot to scene posture");
    }

    // M11-008: TimelinePanel stopPlayback() stops without publishing start frame TCP sphere or link poses
    olp::ui::MainWindow testWin;
    olp::ui::TimelinePanel timeline(testWin);
    bool hookFrameCalledOnStop = false;
    bool hookClearCalledOnStop = false;
    timeline.setViewportHooks(
        [&](const olp::algo::BakedFrame&) { hookFrameCalledOnStop = true; },
        [&](const std::vector<olp::core::Vec3>&) {},
        [&]() { hookClearCalledOnStop = true; });
    timeline.stopPlayback();
    check(hookClearCalledOnStop,
          "M11-008: stopPlayback() invokes clearPreview to clear TCP sphere and restore scene posture");
    check(!hookFrameCalledOnStop,
          "M11-008: stopPlayback() does not publish start frame, preventing TCP sphere reappearance");
#endif
}
