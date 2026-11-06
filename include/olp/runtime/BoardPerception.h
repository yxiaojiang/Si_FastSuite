#pragma once

#include "olp/runtime/DeviceTypes.h"
#include "olp/runtime/HandEyeCalibration.h"
#include "olp/runtime/ICamera.h"

#include <string>
#include <vector>

namespace olp::runtime {

// Horizontal board pose in robot base (3DoF demo): XY translation + yaw about Z.
struct BoardPoseMm {
    double xMm{0.0};
    double yMm{0.0};
    double zMm{0.0};
    double yawRad{0.0}; // radians; see BoardPerception::yawConvention
};

// Mode 2 synthetic-contour perception result (VE-D3).
// Upper edge for the demo IS the transformed polygon - no real depth / Open3D / PCL.
struct BoardPerception {
    std::string placementId{};
    std::string frameId{"base"};
    BoardPoseMm pose{};
    std::vector<ContourPointMm> edgeBaseMm{};
    double thicknessMm{18.0};
    bool closed{true};

    // Yaw is the atan2 of the first edge after CCW orientation (dx, dy in base XY).
    // Documented alternative (not used here): XY PCA major-axis angle.
    static constexpr const char* yawConvention = "first_edge_atan2_after_ccw";
};

// Pure function: camera-frame contour + hand-eye -> base-frame perception.
// Steps: transformContourToBase -> finite/N/area checks -> logical close (shoelace)
// -> unify CCW -> centroid (vertex mean) + first-edge yaw + mean z.
DeviceResult<BoardPerception> perceiveFromContour(
    const ContourFrame& frameCam,
    const HandEyeCalibration& calib,
    double minAbsAreaMm2 = 100.0);

// Capture from ICamera then perceiveFromContour. No vendor SDK.
DeviceResult<BoardPerception> perceiveBoard(
    ICamera& camera,
    const HandEyeCalibration& calib,
    double minAbsAreaMm2 = 100.0);

} // namespace olp::runtime
