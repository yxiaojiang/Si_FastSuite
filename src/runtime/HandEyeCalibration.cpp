#include "olp/runtime/HandEyeCalibration.h"
#include "olp/io/Utf8Path.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>

#if defined(OLP_HAS_NLOHMANN_JSON)
#include <nlohmann/json.hpp>
#endif

namespace olp::runtime {
namespace {

bool isFiniteVec3(const core::Vec3& v) noexcept {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

bool nearlyEqual(double a, double b, double tol) noexcept {
    return std::fabs(a - b) <= tol;
}

#if defined(OLP_HAS_NLOHMANN_JSON)
nlohmann::json transformToJson(const core::Transform4d& T) {
    nlohmann::json j;
    j["rotation"] = nlohmann::json::array({
        nlohmann::json::array({T.rot.m[0][0], T.rot.m[0][1], T.rot.m[0][2]}),
        nlohmann::json::array({T.rot.m[1][0], T.rot.m[1][1], T.rot.m[1][2]}),
        nlohmann::json::array({T.rot.m[2][0], T.rot.m[2][1], T.rot.m[2][2]}),
    });
    j["translationMm"] = nlohmann::json::array({T.pos.x, T.pos.y, T.pos.z});
    return j;
}

DeviceResult<core::Transform4d> transformFromJson(
    const nlohmann::json& j,
    const std::string& label) {
    if (!j.is_object()) {
        return DeviceResult<core::Transform4d>::failure(
            DeviceError::InvalidArgument,
            label + " must be a JSON object");
    }
    if (!j.contains("rotation") || !j["rotation"].is_array() ||
        j["rotation"].size() != 3) {
        return DeviceResult<core::Transform4d>::failure(
            DeviceError::InvalidArgument,
            label + " missing 3x3 rotation array");
    }
    if (!j.contains("translationMm") || !j["translationMm"].is_array() ||
        j["translationMm"].size() != 3) {
        return DeviceResult<core::Transform4d>::failure(
            DeviceError::InvalidArgument,
            label + " missing translationMm[3]");
    }

    try {
        core::Transform4d T;
        for (int r = 0; r < 3; ++r) {
            const auto& row = j["rotation"].at(r);
            if (!row.is_array() || row.size() != 3) {
                return DeviceResult<core::Transform4d>::failure(
                    DeviceError::InvalidArgument,
                    label + " rotation row must have 3 numbers");
            }
            for (int c = 0; c < 3; ++c) {
                T.rot.m[static_cast<size_t>(r)][static_cast<size_t>(c)] =
                    row.at(c).get<double>();
            }
        }
        T.pos.x = j["translationMm"].at(0).get<double>();
        T.pos.y = j["translationMm"].at(1).get<double>();
        T.pos.z = j["translationMm"].at(2).get<double>();
        return DeviceResult<core::Transform4d>::success(std::move(T));
    } catch (const nlohmann::json::exception& ex) {
        return DeviceResult<core::Transform4d>::failure(
            DeviceError::InvalidArgument,
            label + " JSON field type error: " + ex.what());
    } catch (const std::exception& ex) {
        return DeviceResult<core::Transform4d>::failure(
            DeviceError::InvalidArgument,
            label + " JSON field error: " + ex.what());
    }
}
#endif

} // namespace

DeviceStatus validateTransform(
    const core::Transform4d& T,
    const std::string& label,
    double orthoTol,
    double detTol) {
    if (!isFiniteVec3(T.pos)) {
        return DeviceStatus::failure(
            DeviceError::InvalidArgument,
            label + " translation is not finite");
    }
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            if (!std::isfinite(T.rot.m[static_cast<size_t>(r)][static_cast<size_t>(c)])) {
                return DeviceStatus::failure(
                    DeviceError::InvalidArgument,
                    label + " rotation contains non-finite values");
            }
        }
    }

    const core::Vec3 x = T.rot.colX();
    const core::Vec3 y = T.rot.colY();
    const core::Vec3 z = T.rot.colZ();
    if (!nearlyEqual(x.norm(), 1.0, orthoTol) ||
        !nearlyEqual(y.norm(), 1.0, orthoTol) ||
        !nearlyEqual(z.norm(), 1.0, orthoTol)) {
        return DeviceStatus::failure(
            DeviceError::InvalidArgument,
            label + " rotation columns are not unit length");
    }
    if (!nearlyEqual(x.dot(y), 0.0, orthoTol) ||
        !nearlyEqual(y.dot(z), 0.0, orthoTol) ||
        !nearlyEqual(z.dot(x), 0.0, orthoTol)) {
        return DeviceStatus::failure(
            DeviceError::InvalidArgument,
            label + " rotation columns are not orthogonal");
    }

    const double det = T.rot.determinant();
    if (!nearlyEqual(det, 1.0, detTol)) {
        std::ostringstream oss;
        oss << label << " rotation det=" << det << " (expected +1)";
        return DeviceStatus::failure(DeviceError::InvalidArgument, oss.str());
    }
    return DeviceStatus::success();
}

DeviceStatus validateHandEyeCalibration(
    const HandEyeCalibration& calib,
    double orthoTol,
    double detTol) {
    if (calib.schemaVersion < 1) {
        return DeviceStatus::failure(
            DeviceError::InvalidArgument,
            "schemaVersion must be >= 1");
    }
    if (calib.lengthUnit != "mm") {
        return DeviceStatus::failure(
            DeviceError::InvalidArgument,
            "lengthUnit must be \"mm\" in VE-D2 demo schema");
    }
    auto st = validateTransform(calib.T_cam_base, "T_cam_base", orthoTol, detTol);
    if (!st.ok()) {
        return st;
    }
    if (calib.T_flange_tcp.has_value()) {
        st = validateTransform(
            *calib.T_flange_tcp, "T_flange_tcp", orthoTol, detTol);
        if (!st.ok()) {
            return st;
        }
    }
    return DeviceStatus::success();
}

DeviceResult<HandEyeCalibration> loadHandEyeCalibrationJson(
    const std::string& pathUtf8) {
#if !defined(OLP_HAS_NLOHMANN_JSON)
    return DeviceResult<HandEyeCalibration>::failure(
        DeviceError::NotSupported,
        "Hand-eye JSON requires nlohmann_json (OLP_HAS_NLOHMANN_JSON)");
#else
    std::ifstream in(olp::io::utf8ToPath(pathUtf8));
    if (!in) {
        return DeviceResult<HandEyeCalibration>::failure(
            DeviceError::IoFailure,
            "Failed to open hand-eye JSON: " + pathUtf8);
    }

    nlohmann::json root;
    try {
        in >> root;
    } catch (const std::exception& ex) {
        return DeviceResult<HandEyeCalibration>::failure(
            DeviceError::InvalidArgument,
            std::string("JSON parse error in ") + pathUtf8 + ": " + ex.what());
    }

    try {
        if (!root.is_object()) {
            return DeviceResult<HandEyeCalibration>::failure(
                DeviceError::InvalidArgument,
                "Hand-eye JSON root must be an object: " + pathUtf8);
        }
        if (!root.contains("T_cam_base")) {
            return DeviceResult<HandEyeCalibration>::failure(
                DeviceError::InvalidArgument,
                "Hand-eye JSON missing T_cam_base: " + pathUtf8);
        }

        HandEyeCalibration calib;
        calib.schemaVersion = root.value("schemaVersion", 1);
        calib.lengthUnit = root.value("lengthUnit", std::string("mm"));
        calib.notes = root.value("notes", std::string{});

        auto cam = transformFromJson(root.at("T_cam_base"), "T_cam_base");
        if (!cam.ok()) {
            return DeviceResult<HandEyeCalibration>::failure(
                cam.status.code, cam.status.message);
        }
        calib.T_cam_base = std::move(cam.value);

        if (root.contains("T_flange_tcp") && !root["T_flange_tcp"].is_null()) {
            auto tool = transformFromJson(root.at("T_flange_tcp"), "T_flange_tcp");
            if (!tool.ok()) {
                return DeviceResult<HandEyeCalibration>::failure(
                    tool.status.code, tool.status.message);
            }
            calib.T_flange_tcp = std::move(tool.value);
        }

        const auto st = validateHandEyeCalibration(calib);
        if (!st.ok()) {
            return DeviceResult<HandEyeCalibration>::failure(st.code, st.message);
        }
        return DeviceResult<HandEyeCalibration>::success(std::move(calib));
    } catch (const nlohmann::json::exception& ex) {
        return DeviceResult<HandEyeCalibration>::failure(
            DeviceError::InvalidArgument,
            std::string("JSON field type error in ") + pathUtf8 + ": " + ex.what());
    } catch (const std::exception& ex) {
        return DeviceResult<HandEyeCalibration>::failure(
            DeviceError::InvalidArgument,
            std::string("JSON field error in ") + pathUtf8 + ": " + ex.what());
    }
#endif
}

DeviceStatus saveHandEyeCalibrationJson(
    const HandEyeCalibration& calib,
    const std::string& pathUtf8) {
#if !defined(OLP_HAS_NLOHMANN_JSON)
    return DeviceStatus::failure(
        DeviceError::NotSupported,
        "Hand-eye JSON requires nlohmann_json (OLP_HAS_NLOHMANN_JSON)");
#else
    const auto st = validateHandEyeCalibration(calib);
    if (!st.ok()) {
        return st;
    }

    nlohmann::json root;
    root["schemaVersion"] = calib.schemaVersion;
    root["lengthUnit"] = calib.lengthUnit;
    root["notes"] = calib.notes;
    root["T_cam_base"] = transformToJson(calib.T_cam_base);
    if (calib.T_flange_tcp.has_value()) {
        root["T_flange_tcp"] = transformToJson(*calib.T_flange_tcp);
    }

    std::ofstream out(olp::io::utf8ToPath(pathUtf8));
    if (!out) {
        return DeviceStatus::failure(
            DeviceError::IoFailure,
            "Failed to write hand-eye JSON: " + pathUtf8);
    }
    try {
        out << root.dump(2) << '\n';
    } catch (const std::exception& ex) {
        return DeviceStatus::failure(
            DeviceError::InternalError,
            std::string("Failed to serialize hand-eye JSON: ") + ex.what());
    }
    if (!out) {
        return DeviceStatus::failure(
            DeviceError::IoFailure,
            "Write stream failed for hand-eye JSON: " + pathUtf8);
    }
    return DeviceStatus::success("Saved " + pathUtf8);
#endif
}

DeviceResult<ContourFrame> transformContourToBase(
    const ContourFrame& frameCam,
    const HandEyeCalibration& calib) {
    const auto st = validateHandEyeCalibration(calib);
    if (!st.ok()) {
        return DeviceResult<ContourFrame>::failure(st.code, st.message);
    }
    if (frameCam.polygonMm.size() < 3) {
        return DeviceResult<ContourFrame>::failure(
            DeviceError::InvalidArgument,
            "transformContourToBase needs at least 3 contour points");
    }

    ContourFrame out = frameCam;
    out.frameId = "base";
    out.polygonMm.clear();
    out.polygonMm.reserve(frameCam.polygonMm.size());
    for (const auto& p : frameCam.polygonMm) {
        const core::Vec3 cam{p.x, p.y, p.z};
        const core::Vec3 base = calib.T_cam_base.transformPoint(cam);
        if (!isFiniteVec3(base)) {
            return DeviceResult<ContourFrame>::failure(
                DeviceError::InvalidArgument,
                "transformContourToBase produced non-finite coordinates");
        }
        out.polygonMm.push_back(ContourPointMm{base.x, base.y, base.z});
    }
    return DeviceResult<ContourFrame>::success(std::move(out));
}

std::string defaultVisionEdgeCalibDemoPath(const std::string& workspaceRootUtf8) {
    namespace fs = std::filesystem;
    const fs::path root = olp::io::utf8ToPath(workspaceRootUtf8);
    return olp::io::pathToUtf8(
        root / "tests" / "data" / "vision_edge" / "calib_demo.json");
}

} // namespace olp::runtime
