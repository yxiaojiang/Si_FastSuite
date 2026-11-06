#include "test_support.h"

#include "olp/io/Utf8Path.h"
#include "olp/runtime/ContourDataset.h"
#include "olp/runtime/HandEyeCalibration.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
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

bool transformsClose(
    const olp::core::Transform4d& a,
    const olp::core::Transform4d& b,
    double eps = 1e-9) {
    if (!nearlyEqual(a.pos.x, b.pos.x, eps) ||
        !nearlyEqual(a.pos.y, b.pos.y, eps) ||
        !nearlyEqual(a.pos.z, b.pos.z, eps)) {
        return false;
    }
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            if (!nearlyEqual(
                    a.rot.m[static_cast<size_t>(r)][static_cast<size_t>(c)],
                    b.rot.m[static_cast<size_t>(r)][static_cast<size_t>(c)],
                    eps)) {
                return false;
            }
        }
    }
    return true;
}

} // namespace

void testVisionHandEyeCalibration() {
    std::cout << "\n--- VE-D2: Hand-eye calibration model + contour transform ---\n";

#if !defined(OLP_HAS_NLOHMANN_JSON)
    check(false, "VE-D2 requires OLP_HAS_NLOHMANN_JSON", "nlohmann_json not found");
    return;
#else
    const std::string calibPath =
        olp::runtime::defaultVisionEdgeCalibDemoPath(workspaceRoot());
    auto loaded = olp::runtime::loadHandEyeCalibrationJson(calibPath);
    check(loaded.ok(), "load calib_demo.json", loaded.status.message);
    if (!loaded.ok()) {
        return;
    }

    const auto& calib = loaded.value;
    check(calib.schemaVersion == 1, "schemaVersion == 1", "");
    check(calib.lengthUnit == "mm", "lengthUnit is mm", calib.lengthUnit);
    check(calib.T_flange_tcp.has_value(), "optional T_flange_tcp present", "");
    check(olp::runtime::validateHandEyeCalibration(calib).ok(),
          "validateHandEyeCalibration accepts demo fixture",
          "");

    // JSON round-trip
    namespace fs = std::filesystem;
    const fs::path tmpDir = fs::temp_directory_path() / "ve_d2_calib_roundtrip";
    std::error_code ec;
    fs::remove_all(tmpDir, ec);
    fs::create_directories(tmpDir, ec);
    const std::string roundPath =
        olp::io::pathToUtf8(tmpDir / "calib_roundtrip.json");
    auto saved = olp::runtime::saveHandEyeCalibrationJson(calib, roundPath);
    check(saved.ok(), "saveHandEyeCalibrationJson round-trip", saved.message);
    auto reloaded = olp::runtime::loadHandEyeCalibrationJson(roundPath);
    check(reloaded.ok(), "reload round-trip JSON", reloaded.status.message);
    if (reloaded.ok()) {
        check(transformsClose(calib.T_cam_base, reloaded.value.T_cam_base),
              "T_cam_base round-trip equal",
              "");
        check(reloaded.value.T_flange_tcp.has_value() &&
                  transformsClose(
                      *calib.T_flange_tcp, *reloaded.value.T_flange_tcp),
              "T_flange_tcp round-trip equal",
              "");
        check(reloaded.value.notes.find("Demo fixture") != std::string::npos ||
                  reloaded.value.notes.find("not") != std::string::npos,
              "notes survive round-trip",
              reloaded.value.notes);
    }

    // Missing T_cam_base
    {
        const fs::path badDir = tmpDir / "missing_field";
        fs::create_directories(badDir, ec);
        const fs::path badFile = badDir / "missing.json";
        {
            std::ofstream out(badFile);
            out << R"({"schemaVersion":1,"lengthUnit":"mm","notes":"x"})" << "\n";
        }
        bool threw = false;
        std::string throwWhat;
        olp::runtime::DeviceResult<olp::runtime::HandEyeCalibration> bad;
        try {
            bad = olp::runtime::loadHandEyeCalibrationJson(
                olp::io::pathToUtf8(badFile));
        } catch (const std::exception& ex) {
            threw = true;
            throwWhat = ex.what();
        }
        check(!threw, "missing T_cam_base does not throw", throwWhat);
        check(!bad.ok() &&
                  bad.status.code == olp::runtime::DeviceError::InvalidArgument,
              "missing T_cam_base -> InvalidArgument",
              bad.status.message);
    }

    // Bad rotation (reflection / det ~= -1)
    {
        const fs::path badFile = tmpDir / "bad_det.json";
        {
            std::ofstream out(badFile);
            out << R"({
  "schemaVersion": 1,
  "lengthUnit": "mm",
  "T_cam_base": {
    "rotation": [[-1,0,0],[0,1,0],[0,0,1]],
    "translationMm": [0,0,0]
  }
})" << "\n";
        }
        bool threw = false;
        std::string throwWhat;
        olp::runtime::DeviceResult<olp::runtime::HandEyeCalibration> bad;
        try {
            bad = olp::runtime::loadHandEyeCalibrationJson(
                olp::io::pathToUtf8(badFile));
        } catch (const std::exception& ex) {
            threw = true;
            throwWhat = ex.what();
        }
        check(!threw, "bad det matrix does not throw", throwWhat);
        check(!bad.ok() &&
                  bad.status.code == olp::runtime::DeviceError::InvalidArgument,
              "bad det matrix -> InvalidArgument",
              bad.status.message);
    }

    // Non-orthogonal / non-finite
    {
        olp::runtime::HandEyeCalibration broken = calib;
        broken.T_cam_base.rot.m[0][0] = 2.0;
        auto st = olp::runtime::validateHandEyeCalibration(broken);
        check(!st.ok() && st.code == olp::runtime::DeviceError::InvalidArgument,
              "non-unit rotation rejected",
              st.message);

        broken = calib;
        broken.T_cam_base.pos.x =
            std::numeric_limits<double>::quiet_NaN();
        st = olp::runtime::validateHandEyeCalibration(broken);
        check(!st.ok(), "NaN translation rejected", st.message);
    }

    // placement_a gold sample under calib_demo extrinsic
    const std::string datasetDir =
        olp::runtime::defaultVisionEdgeDatasetDir(workspaceRoot());
    auto dataset = olp::runtime::ContourDataset::loadFromDirectory(datasetDir);
    check(dataset.ok(), "reload vision_edge dataset for transform gold",
          dataset.status.message);
    if (!dataset.ok()) {
        fs::remove_all(tmpDir, ec);
        return;
    }
    auto frameA = dataset.value.frame("placement_a");
    check(frameA.ok(), "load placement_a for gold", frameA.status.message);
    if (!frameA.ok()) {
        fs::remove_all(tmpDir, ec);
        return;
    }

    auto baseFrame =
        olp::runtime::transformContourToBase(frameA.value, calib);
    check(baseFrame.ok(), "transformContourToBase placement_a",
          baseFrame.status.message);
    check(baseFrame.ok() && baseFrame.value.frameId == "base",
          "transformed frameId is base",
          baseFrame.ok() ? baseFrame.value.frameId : "");
    if (baseFrame.ok() && baseFrame.value.polygonMm.size() >= 1) {
        // Gold: Rz(90)*[50,40,0] + [1000,500,800] = [960,550,800]
        const auto& p0 = baseFrame.value.polygonMm[0];
        check(nearlyEqual(p0.x, 960.0) && nearlyEqual(p0.y, 550.0) &&
                  nearlyEqual(p0.z, 800.0),
              "placement_a[0] gold in base (tol 1e-6)",
              "got=(" + std::to_string(p0.x) + "," + std::to_string(p0.y) +
                  "," + std::to_string(p0.z) + ")");
    }
    if (baseFrame.ok() && frameA.value.polygonMm.size() ==
                              baseFrame.value.polygonMm.size()) {
        // Spot-check last vertex: cam (50,240,0) -> (1000-240, 500+50, 800)=(760,550,800)
        const auto& plast =
            baseFrame.value.polygonMm[baseFrame.value.polygonMm.size() - 1];
        check(nearlyEqual(plast.x, 760.0) && nearlyEqual(plast.y, 550.0) &&
                  nearlyEqual(plast.z, 800.0),
              "placement_a[last] gold in base (tol 1e-6)",
              "got=(" + std::to_string(plast.x) + "," + std::to_string(plast.y) +
                  "," + std::to_string(plast.z) + ")");
    }

    fs::remove_all(tmpDir, ec);
#endif
}
