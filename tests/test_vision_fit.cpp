#include "test_support.h"

#include "olp/io/Utf8Path.h"
#include "olp/runtime/BoardOutlineFit.h"
#include "olp/runtime/BoardPerception.h"
#include "olp/runtime/ContourDataset.h"
#include "olp/runtime/HandEyeCalibration.h"

#include <cmath>
#include <iostream>
#include <string>

namespace {

std::string workspaceRoot() {
#if defined(OLP_SOURCE_DIR)
    return std::string(OLP_SOURCE_DIR);
#else
    return olp::io::resolveWorkspaceOrAppDir();
#endif
}

bool nearlyEqual(double a, double b, double eps = 1e-6) {
    return std::fabs(a - b) <= eps;
}

bool transformsPoseMatch(
    const olp::core::Transform4d& T,
    const olp::runtime::BoardPoseMm& pose,
    double eps = 1e-9) {
    if (!nearlyEqual(T.pos.x, pose.xMm, eps) ||
        !nearlyEqual(T.pos.y, pose.yMm, eps) ||
        !nearlyEqual(T.pos.z, pose.zMm, eps)) {
        return false;
    }
    const olp::core::Mat3 expect = olp::core::Mat3::rotZ(pose.yawRad);
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            if (!nearlyEqual(
                    T.rot.m[static_cast<size_t>(r)][static_cast<size_t>(c)],
                    expect.m[static_cast<size_t>(r)][static_cast<size_t>(c)],
                    eps)) {
                return false;
            }
        }
    }
    return true;
}

} // namespace

void testVisionBoardOutlineFit() {
    std::cout << "\n--- VE-D4: Outline fit + transient SceneNode descriptor ---\n";

#if !defined(OLP_HAS_NLOHMANN_JSON)
    check(false, "VE-D4 requires OLP_HAS_NLOHMANN_JSON", "");
    return;
#else
    const std::string datasetDir =
        olp::runtime::defaultVisionEdgeDatasetDir(workspaceRoot());
    const std::string calibPath =
        olp::runtime::defaultVisionEdgeCalibDemoPath(workspaceRoot());

    auto dataset = olp::runtime::ContourDataset::loadFromDirectory(datasetDir);
    auto calib = olp::runtime::loadHandEyeCalibrationJson(calibPath);
    check(dataset.ok() && calib.ok(), "VE-D4 load dataset+calib",
          dataset.ok() ? calib.status.message : dataset.status.message);
    if (!dataset.ok() || !calib.ok()) {
        return;
    }

    auto frameA = dataset.value.frame("placement_a");
    auto frameB = dataset.value.frame("placement_b");
    check(frameA.ok() && frameB.ok(), "VE-D4 load placement frames", "");
    auto percA =
        olp::runtime::perceiveFromContour(frameA.value, calib.value);
    auto percB =
        olp::runtime::perceiveFromContour(frameB.value, calib.value);
    check(percA.ok() && percB.ok(), "VE-D4 perceive A/B",
          percA.ok() ? percB.status.message : percA.status.message);
    if (!percA.ok() || !percB.ok()) {
        return;
    }

    auto fitA = olp::runtime::fitBoardOutline(percA.value);
    check(fitA.ok(), "fitBoardOutline placement_a", fitA.status.message);
    if (fitA.ok()) {
        check(fitA.value.segments.size() == 4,
              "placement_a has 4 straight segments",
              "n=" + std::to_string(fitA.value.segments.size()));
        check(fitA.value.sideLengthsMm.size() == 4,
              "placement_a side length summary present",
              "");
        bool sidesOk = true;
        for (double len : fitA.value.sideLengthsMm) {
            if (!nearlyEqual(len, 200.0, 1e-6)) {
                sidesOk = false;
            }
        }
        check(sidesOk, "placement_a side lengths ~= 200 mm", "");
        // Closed: last.p1 equals first.p0
        check(nearlyEqual(fitA.value.segments.back().p1.x,
                          fitA.value.segments.front().p0.x) &&
                  nearlyEqual(fitA.value.segments.back().p1.y,
                              fitA.value.segments.front().p0.y),
              "placement_a segments logically closed",
              "");
        check(fitA.value.placementId == "placement_a" &&
                  nearlyEqual(fitA.value.pose.xMm, percA.value.pose.xMm) &&
                  nearlyEqual(fitA.value.thicknessMm, percA.value.thicknessMm),
              "fit preserves placement/pose/thickness",
              "");
    }

    auto fitB = olp::runtime::fitBoardOutline(percB.value);
    check(fitB.ok(), "fitBoardOutline placement_b", fitB.status.message);
    if (fitA.ok() && fitB.ok()) {
        check(fitB.value.segments.size() == 4,
              "placement_b has 4 segments",
              "");
        const double dPose = std::hypot(
            fitB.value.pose.xMm - fitA.value.pose.xMm,
            fitB.value.pose.yMm - fitA.value.pose.yMm);
        check(dPose > 1.0, "A/B fitted poses distinguishable",
              "dXY=" + std::to_string(dPose));
        // B is also a 200mm square (rotated); sides still ~200
        bool sidesOk = fitB.value.sideLengthsMm.size() == 4;
        for (double len : fitB.value.sideLengthsMm) {
            if (!nearlyEqual(len, 200.0, 1e-3)) {
                sidesOk = false;
            }
        }
        check(sidesOk, "placement_b side lengths ~= 200 mm", "");
    }

    auto twA =
        olp::runtime::makeTransientBoardWorkpiece(percA.value);
    check(twA.ok(), "makeTransientBoardWorkpiece A", twA.status.message);
    if (twA.ok()) {
        const auto& node = twA.value.node;
        check(node.type == olp::core::SceneNodeType::Part,
              "transient node type Part",
              "");
        check(node.collisionRole == olp::core::CollisionRole::Workpiece,
              "transient node CollisionRole::Workpiece",
              "");
        check(node.cadHandleId == -1,
              "transient cadHandleId == -1 (no OCCT)",
              "id=" + std::to_string(node.cadHandleId));
        check(node.sourceCadFile == "vision_edge_transient",
              "sourceCadFile marker",
              node.sourceCadFile);
        check(node.id == "vision_board_placement_a",
              "stable node id for placement_a",
              node.id);
        check(node.renderMesh.vertices.empty() &&
                  node.collisionMesh.vertices.empty(),
              "meshes empty for headless gate",
              "");
        check(transformsPoseMatch(node.localTransform, percA.value.pose),
              "localTransform matches board pose (Z yaw)",
              "");
        check(twA.value.outline.segments.size() == 4 &&
                  twA.value.perception.placementId == "placement_a",
              "descriptor carries outline+perception",
              "");
    }

    auto twB =
        olp::runtime::makeTransientBoardWorkpiece(percB.value);
    check(twB.ok(), "makeTransientBoardWorkpiece B", twB.status.message);
    if (twA.ok() && twB.ok()) {
        check(twB.value.node.id == "vision_board_placement_b",
              "stable node id for placement_b",
              twB.value.node.id);
        check(twA.value.node.id != twB.value.node.id,
              "A/B transient node ids differ",
              "");
        check(!transformsPoseMatch(
                  twB.value.node.localTransform, percA.value.pose),
              "B node transform differs from A pose",
              "");
    }

    // Failure paths
    olp::runtime::BoardPerception bad = percA.value;
    bad.edgeBaseMm.resize(2);
    auto few = olp::runtime::fitBoardOutline(bad);
    check(!few.ok() &&
              few.status.code == olp::runtime::DeviceError::InvalidArgument,
          "fitBoardOutline rejects N<3",
          few.status.message);

    bad = percA.value;
    bad.edgeBaseMm[1] = bad.edgeBaseMm[0]; // zero-length first edge
    auto zero = olp::runtime::fitBoardOutline(bad);
    check(!zero.ok() &&
              zero.status.code == olp::runtime::DeviceError::InvalidArgument,
          "fitBoardOutline rejects zero-length edge",
          zero.status.message);

    bool threw = false;
    std::string throwWhat;
    try {
        (void)olp::runtime::makeTransientBoardWorkpiece(bad);
    } catch (const std::exception& ex) {
        threw = true;
        throwWhat = ex.what();
    }
    check(!threw, "makeTransientBoardWorkpiece does not throw on bad input",
          throwWhat);
    auto twBad = olp::runtime::makeTransientBoardWorkpiece(bad);
    check(!twBad.ok(), "makeTransientBoardWorkpiece fails on bad input",
          twBad.status.message);
#endif
}
