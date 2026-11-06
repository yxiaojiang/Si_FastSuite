#pragma once

#include "olp/core/MathTypes.h"
#include "olp/runtime/DeviceTypes.h"

#include <optional>
#include <string>

namespace olp::runtime {

// Demo / offline hand-eye extrinsic bundle for Vision_Edge VE-D2.
// Eye-to-Hand: T_cam_base maps a point in the camera frame into the robot base frame.
// Optional T_flange_tcp is a placeholder TCP (roller contact) for later stages.
// This is a data model + I/O layer only - not a field 9-point / AX=XB calibrator.
struct HandEyeCalibration {
    int schemaVersion{1};
    std::string lengthUnit{"mm"};
    std::string notes{};

    core::Transform4d T_cam_base{core::Transform4d::identity()};
    std::optional<core::Transform4d> T_flange_tcp{};
};

// Validate rotation orthonormality (det ~= +1) and finite translation.
DeviceStatus validateTransform(
    const core::Transform4d& T,
    const std::string& label,
    double orthoTol = 1e-6,
    double detTol = 1e-6);

DeviceStatus validateHandEyeCalibration(
    const HandEyeCalibration& calib,
    double orthoTol = 1e-6,
    double detTol = 1e-6);

// UTF-8 JSON load/save. Failures return DeviceResult/Status (no exception escape).
DeviceResult<HandEyeCalibration> loadHandEyeCalibrationJson(
    const std::string& pathUtf8);

DeviceStatus saveHandEyeCalibrationJson(
    const HandEyeCalibration& calib,
    const std::string& pathUtf8);

// Transform contour polygon from camera frame into robot base via T_cam_base.
// Sets out.frameId to "base". Does not mutate input.
DeviceResult<ContourFrame> transformContourToBase(
    const ContourFrame& frameCam,
    const HandEyeCalibration& calib);

// Default demo fixture path: <workspace>/tests/data/vision_edge/calib_demo.json
std::string defaultVisionEdgeCalibDemoPath(const std::string& workspaceRootUtf8);

} // namespace olp::runtime
