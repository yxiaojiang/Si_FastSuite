#include "olp/runtime/BoardPerception.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace olp::runtime {
namespace {

constexpr double kCloseEpsMm = 1e-6;

bool isFinitePoint(const ContourPointMm& p) noexcept {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}

// Signed shoelace area in XY (mm^2). Positive => CCW for standard right-handed XY.
double signedAreaXy(const std::vector<ContourPointMm>& poly) noexcept {
    if (poly.size() < 3) {
        return 0.0;
    }
    double sum = 0.0;
    const std::size_t n = poly.size();
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t j = (i + 1) % n;
        sum += poly[i].x * poly[j].y - poly[j].x * poly[i].y;
    }
    return 0.5 * sum;
}

void reversePolygon(std::vector<ContourPointMm>& poly) {
    // Full ring reverse to flip winding (CW <-> CCW). Deterministic for tests.
    std::reverse(poly.begin(), poly.end());
}

} // namespace

DeviceResult<BoardPerception> perceiveFromContour(
    const ContourFrame& frameCam,
    const HandEyeCalibration& calib,
    double minAbsAreaMm2) {
    if (!(minAbsAreaMm2 > 0.0) || !std::isfinite(minAbsAreaMm2)) {
        return DeviceResult<BoardPerception>::failure(
            DeviceError::InvalidArgument,
            "minAbsAreaMm2 must be a positive finite value");
    }

    auto base = transformContourToBase(frameCam, calib);
    if (!base.ok()) {
        return DeviceResult<BoardPerception>::failure(
            base.status.code, base.status.message);
    }

    std::vector<ContourPointMm> edge = std::move(base.value.polygonMm);
    if (edge.size() < 3) {
        return DeviceResult<BoardPerception>::failure(
            DeviceError::InvalidArgument,
            "perceiveFromContour needs at least 3 contour points");
    }
    for (const auto& p : edge) {
        if (!isFinitePoint(p)) {
            return DeviceResult<BoardPerception>::failure(
                DeviceError::InvalidArgument,
                "perceiveFromContour: non-finite base contour coordinate");
        }
    }

    // Logical close for area: connect last->first without duplicating vertices.
    // If first/last already coincide within eps, still keep original point count.
    const double areaSigned = signedAreaXy(edge);
    const double areaAbs = std::fabs(areaSigned);
    if (!(areaAbs >= minAbsAreaMm2)) {
        return DeviceResult<BoardPerception>::failure(
            DeviceError::InvalidArgument,
            "perceiveFromContour: contour area below threshold");
    }

    // Unify to CCW (positive shoelace).
    if (areaSigned < 0.0) {
        reversePolygon(edge);
    }

    // Vertex-mean centroid (documented). z = mean z.
    double sx = 0.0;
    double sy = 0.0;
    double sz = 0.0;
    for (const auto& p : edge) {
        sx += p.x;
        sy += p.y;
        sz += p.z;
    }
    const double invN = 1.0 / static_cast<double>(edge.size());
    BoardPoseMm pose;
    pose.xMm = sx * invN;
    pose.yMm = sy * invN;
    pose.zMm = sz * invN;

    // Yaw convention: atan2 of first edge after CCW orientation.
    const double dx = edge[1].x - edge[0].x;
    const double dy = edge[1].y - edge[0].y;
    if (!std::isfinite(dx) || !std::isfinite(dy) ||
        (std::fabs(dx) < kCloseEpsMm && std::fabs(dy) < kCloseEpsMm)) {
        return DeviceResult<BoardPerception>::failure(
            DeviceError::InvalidArgument,
            "perceiveFromContour: first edge is degenerate for yaw");
    }
    pose.yawRad = std::atan2(dy, dx);

    BoardPerception out;
    out.placementId = base.value.placementId;
    out.frameId = "base";
    out.pose = pose;
    out.edgeBaseMm = std::move(edge);
    out.thicknessMm = base.value.boardThicknessMm;
    out.closed = true;
    return DeviceResult<BoardPerception>::success(std::move(out));
}

DeviceResult<BoardPerception> perceiveBoard(
    ICamera& camera,
    const HandEyeCalibration& calib,
    double minAbsAreaMm2) {
    auto captured = camera.captureContour();
    if (!captured.ok()) {
        return DeviceResult<BoardPerception>::failure(
            captured.status.code, captured.status.message);
    }
    return perceiveFromContour(captured.value, calib, minAbsAreaMm2);
}

} // namespace olp::runtime
