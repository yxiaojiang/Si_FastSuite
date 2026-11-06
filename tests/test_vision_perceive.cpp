#include "test_support.h"

#include "olp/io/Utf8Path.h"
#include "olp/runtime/BoardPerception.h"
#include "olp/runtime/ContourDataset.h"
#include "olp/runtime/HandEyeCalibration.h"
#include "olp/runtime/MockCamera.h"

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

double signedAreaXy(const std::vector<olp::runtime::ContourPointMm>& poly) {
    double sum = 0.0;
    const std::size_t n = poly.size();
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t j = (i + 1) % n;
        sum += poly[i].x * poly[j].y - poly[j].x * poly[i].y;
    }
    return 0.5 * sum;
}

} // namespace

void testVisionBoardPerception() {
    std::cout << "\n--- VE-D3: Mode 2 board perception (synthetic contour) ---\n";

#if !defined(OLP_HAS_NLOHMANN_JSON)
    check(false, "VE-D3 requires OLP_HAS_NLOHMANN_JSON", "");
    return;
#else
    const std::string datasetDir =
        olp::runtime::defaultVisionEdgeDatasetDir(workspaceRoot());
    const std::string calibPath =
        olp::runtime::defaultVisionEdgeCalibDemoPath(workspaceRoot());

    auto dataset = olp::runtime::ContourDataset::loadFromDirectory(datasetDir);
    check(dataset.ok(), "VE-D3 load contour dataset", dataset.status.message);
    auto calib = olp::runtime::loadHandEyeCalibrationJson(calibPath);
    check(calib.ok(), "VE-D3 load calib_demo.json", calib.status.message);
    if (!dataset.ok() || !calib.ok()) {
        return;
    }

    // Pure-function path: placement A gold
    auto frameA = dataset.value.frame("placement_a");
    check(frameA.ok(), "VE-D3 frame placement_a", frameA.status.message);
    auto percA =
        olp::runtime::perceiveFromContour(frameA.value, calib.value);
    check(percA.ok(), "perceiveFromContour placement_a", percA.status.message);
    if (percA.ok()) {
        check(percA.value.placementId == "placement_a",
              "perception A placementId",
              percA.value.placementId);
        check(percA.value.frameId == "base", "perception A frameId base", "");
        check(percA.value.closed, "perception A closed flag", "");
        check(percA.value.edgeBaseMm.size() == frameA.value.polygonMm.size(),
              "perception A edge count preserved",
              "n=" + std::to_string(percA.value.edgeBaseMm.size()));
        check(signedAreaXy(percA.value.edgeBaseMm) > 0.0,
              "perception A edge is CCW (positive area)",
              "");
        check(nearlyEqual(percA.value.pose.xMm, 860.0) &&
                  nearlyEqual(percA.value.pose.yMm, 650.0) &&
                  nearlyEqual(percA.value.pose.zMm, 800.0),
              "perception A centroid gold",
              "x=" + std::to_string(percA.value.pose.xMm) +
                  " y=" + std::to_string(percA.value.pose.yMm) +
                  " z=" + std::to_string(percA.value.pose.zMm));
        check(nearlyEqual(percA.value.pose.yawRad, 0.5 * olp::core::kPi),
              "perception A yaw = first-edge atan2 (pi/2)",
              "yaw=" + std::to_string(percA.value.pose.yawRad));
        check(nearlyEqual(percA.value.thicknessMm, 18.0),
              "perception A thickness from frame",
              "");
    }

    auto frameB = dataset.value.frame("placement_b");
    check(frameB.ok(), "VE-D3 frame placement_b", frameB.status.message);
    auto percB =
        olp::runtime::perceiveFromContour(frameB.value, calib.value);
    check(percB.ok(), "perceiveFromContour placement_b", percB.status.message);
    if (percA.ok() && percB.ok()) {
        check(percB.value.placementId == "placement_b",
              "perception B placementId",
              "");
        check(signedAreaXy(percB.value.edgeBaseMm) > 0.0,
              "perception B edge is CCW",
              "");
        check(percB.value.edgeBaseMm.size() == frameB.value.polygonMm.size(),
              "perception B edge count preserved",
              "");
        const double dx = percB.value.pose.xMm - percA.value.pose.xMm;
        const double dy = percB.value.pose.yMm - percA.value.pose.yMm;
        const double dYaw =
            percB.value.pose.yawRad - percA.value.pose.yawRad;
        check(std::hypot(dx, dy) > 1.0,
              "A/B centroids distinguishable",
              "deltaXY=" + std::to_string(std::hypot(dx, dy)));
        check(std::fabs(dYaw) > 0.05,
              "A/B yaw distinguishable",
              "dYaw=" + std::to_string(dYaw));
        // Synthetic B centroid gold under calib_demo (vertex mean after CCW)
        check(nearlyEqual(percB.value.pose.xMm, 840.0, 1e-4) &&
                  nearlyEqual(percB.value.pose.yMm, 685.0, 1e-4),
              "perception B centroid gold (~840,685)",
              "x=" + std::to_string(percB.value.pose.xMm) +
                  " y=" + std::to_string(percB.value.pose.yMm));
    }

    // Reproducibility
    if (frameA.ok()) {
        auto again =
            olp::runtime::perceiveFromContour(frameA.value, calib.value);
        check(again.ok() && percA.ok() &&
                  nearlyEqual(again.value.pose.xMm, percA.value.pose.xMm) &&
                  nearlyEqual(again.value.pose.yMm, percA.value.pose.yMm) &&
                  nearlyEqual(again.value.pose.yawRad, percA.value.pose.yawRad),
              "perceiveFromContour reproducible for A",
              "");
    }

    // ICamera path via MockCamera
    olp::runtime::MockCamera camera(dataset.value);
    check(camera.connect().ok(), "VE-D3 MockCamera connect", "");
    check(camera.selectPlacement("placement_a").ok(),
          "VE-D3 select placement_a",
          "");
    auto boardA =
        olp::runtime::perceiveBoard(camera, calib.value);
    check(boardA.ok() && boardA.value.placementId == "placement_a",
          "perceiveBoard placement_a",
          boardA.status.message);

    check(camera.selectPlacement("placement_b").ok(),
          "VE-D3 select placement_b",
          "");
    auto boardB =
        olp::runtime::perceiveBoard(camera, calib.value);
    check(boardB.ok() && boardB.value.placementId == "placement_b",
          "perceiveBoard placement_b",
          boardB.status.message);
    if (boardA.ok() && boardB.ok()) {
        check(std::hypot(boardB.value.pose.xMm - boardA.value.pose.xMm,
                         boardB.value.pose.yMm - boardA.value.pose.yMm) > 1.0,
              "perceiveBoard A/B poses differ",
              "");
    }

    // Failure paths: disconnected camera
    check(camera.disconnect().ok(), "VE-D3 camera disconnect", "");
    auto disc = olp::runtime::perceiveBoard(camera, calib.value);
    check(!disc.ok() &&
              disc.status.code == olp::runtime::DeviceError::NotConnected,
          "perceiveBoard fails when camera disconnected",
          disc.status.message);

    // Degenerate contour: too few points
    olp::runtime::ContourFrame bad = frameA.value;
    bad.polygonMm.resize(2);
    auto few = olp::runtime::perceiveFromContour(bad, calib.value);
    check(!few.ok() &&
              few.status.code == olp::runtime::DeviceError::InvalidArgument,
          "perceiveFromContour rejects N<3",
          few.status.message);

    // Tiny area
    olp::runtime::ContourFrame tiny = frameA.value;
    tiny.polygonMm = {
        {0.0, 0.0, 0.0},
        {0.1, 0.0, 0.0},
        {0.1, 0.1, 0.0},
        {0.0, 0.1, 0.0},
    };
    auto tinyRes =
        olp::runtime::perceiveFromContour(tiny, calib.value, /*minAbsAreaMm2=*/100.0);
    check(!tinyRes.ok() &&
              tinyRes.status.code == olp::runtime::DeviceError::InvalidArgument,
          "perceiveFromContour rejects area below threshold",
          tinyRes.status.message);

    check(std::string(olp::runtime::BoardPerception::yawConvention)
                  .find("first_edge_atan2") != std::string::npos,
          "yaw convention documented as first_edge_atan2_after_ccw",
          olp::runtime::BoardPerception::yawConvention);
#endif
}
