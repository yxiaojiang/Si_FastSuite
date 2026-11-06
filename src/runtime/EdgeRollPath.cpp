#include "olp/runtime/EdgeRollPath.h"

#include <cmath>
#include <utility>

namespace olp::runtime {
namespace {

struct Vec2 {
    double x{0.0};
    double y{0.0};

    Vec2 operator+(const Vec2& o) const noexcept { return {x + o.x, y + o.y}; }
    Vec2 operator-(const Vec2& o) const noexcept { return {x - o.x, y - o.y}; }
    Vec2 operator*(double s) const noexcept { return {x * s, y * s}; }
    [[nodiscard]] double norm() const noexcept { return std::sqrt(x * x + y * y); }
};

DeviceResult<Vec2> unitOrFail(const Vec2& v, const char* what) {
    const double n = v.norm();
    if (!(n > 1e-9) || !std::isfinite(n)) {
        return DeviceResult<Vec2>::failure(
            DeviceError::InvalidArgument,
            std::string("planEdgeRollPath: degenerate ") + what);
    }
    return DeviceResult<Vec2>::success(Vec2{v.x / n, v.y / n});
}

Vec2 edgeTangent(const LineSegmentMm& seg) noexcept {
    return {seg.p1.x - seg.p0.x, seg.p1.y - seg.p0.y};
}

// CCW outward normal: n_out = normalize(dy, -dx)
Vec2 outwardNormalFromTangent(const Vec2& tUnit) noexcept {
    return {tUnit.y, -tUnit.x};
}

core::Transform4d toolFrameFromTangent(const Vec2& tUnit) noexcept {
    const core::Vec3 x{tUnit.x, tUnit.y, 0.0};
    const core::Vec3 z{0.0, 0.0, -1.0};
    // Right-handed: Y = Z x X (= n_out for planar CCW travel).
    const core::Vec3 y = z.cross(x).normalized();
    core::Transform4d T;
    T.rot = core::Mat3::fromColumns(x, y, z);
    T.pos = {0.0, 0.0, 0.0};
    return T;
}

CartesianWaypointMm makeCartesian(
    double x,
    double y,
    double z,
    const Vec2& tUnit) {
    core::Transform4d T = toolFrameFromTangent(tUnit);
    T.pos = {x, y, z};
    const core::WprPose wpr = T.toWPR();
    CartesianWaypointMm wp;
    wp.positionMm = T.pos;
    wp.wDeg = wpr.wDeg;
    wp.pDeg = wpr.pDeg;
    wp.rDeg = wpr.rDeg;
    return wp;
}

DeviceStatus validateParams(const EdgeRollParams& p) {
    if (!(p.rollerRadiusMm > 0.0) || !std::isfinite(p.rollerRadiusMm)) {
        return DeviceStatus::failure(
            DeviceError::InvalidArgument, "rollerRadiusMm must be finite and > 0");
    }
    if (!(p.squeezeMm > 0.0) || !std::isfinite(p.squeezeMm)) {
        return DeviceStatus::failure(
            DeviceError::InvalidArgument, "squeezeMm must be finite and > 0");
    }
    if (!(p.rollerRadiusMm > p.squeezeMm)) {
        return DeviceStatus::failure(
            DeviceError::InvalidArgument, "rollerRadiusMm must be > squeezeMm (d=R-delta)");
    }
    auto posSpeed = [](double v, const char* name) -> DeviceStatus {
        if (!(v > 0.0) || !std::isfinite(v)) {
            return DeviceStatus::failure(
                DeviceError::InvalidArgument,
                std::string(name) + " must be finite and > 0");
        }
        return DeviceStatus::success();
    };
    if (auto st = posSpeed(p.contactSpeedMmPerSec, "contactSpeedMmPerSec"); !st.ok()) {
        return st;
    }
    if (auto st = posSpeed(p.approachDistMm, "approachDistMm"); !st.ok()) {
        return st;
    }
    if (auto st = posSpeed(p.retractDistMm, "retractDistMm"); !st.ok()) {
        return st;
    }
    if (auto st = posSpeed(p.approachSpeedMmPerSec, "approachSpeedMmPerSec"); !st.ok()) {
        return st;
    }
    if (auto st = posSpeed(p.retractSpeedMmPerSec, "retractSpeedMmPerSec"); !st.ok()) {
        return st;
    }
    if (auto st = posSpeed(p.sampleSpacingMm, "sampleSpacingMm"); !st.ok()) {
        return st;
    }
    return DeviceStatus::success();
}

struct SegGeom {
    LineSegmentMm seg{};
    Vec2 tUnit{};
    Vec2 nOut{};
    double length{0.0};
};

DeviceResult<std::vector<SegGeom>> buildSegGeom(const FittedBoardOutline& outline) {
    std::vector<SegGeom> out;
    out.reserve(outline.segments.size());
    for (const auto& seg : outline.segments) {
        if (!std::isfinite(seg.p0.x) || !std::isfinite(seg.p0.y) ||
            !std::isfinite(seg.p1.x) || !std::isfinite(seg.p1.y)) {
            return DeviceResult<std::vector<SegGeom>>::failure(
                DeviceError::InvalidArgument,
                "planEdgeRollPath: non-finite segment vertex");
        }
        const Vec2 raw = edgeTangent(seg);
        auto tRes = unitOrFail(raw, "edge tangent");
        if (!tRes.ok()) {
            return DeviceResult<std::vector<SegGeom>>::failure(
                tRes.status.code, tRes.status.message);
        }
        SegGeom g;
        g.seg = seg;
        g.tUnit = tRes.value;
        g.nOut = outwardNormalFromTangent(g.tUnit);
        g.length = raw.norm();
        if (!(g.length > 1e-9)) {
            return DeviceResult<std::vector<SegGeom>>::failure(
                DeviceError::InvalidArgument,
                "planEdgeRollPath: zero-length segment");
        }
        out.push_back(g);
    }
    return DeviceResult<std::vector<SegGeom>>::success(std::move(out));
}

std::size_t longestSegmentIndex(const std::vector<SegGeom>& geoms) {
    std::size_t best = 0;
    double bestLen = -1.0;
    for (std::size_t i = 0; i < geoms.size(); ++i) {
        if (geoms[i].length > bestLen + 1e-12) {
            bestLen = geoms[i].length;
            best = i;
        }
    }
    return best; // ties keep smallest index via strict >
}

void appendSampledEdge(
    const SegGeom& g,
    double u0,
    double u1,
    double zContact,
    double d,
    double spacing,
    double speed,
    EdgeRollTag tag,
    bool includeStart,
    std::vector<EdgeRollWaypoint>& out) {
    if (u1 < u0) {
        std::swap(u0, u1);
    }
    const double span = (u1 - u0) * g.length;
    int steps = static_cast<int>(std::ceil(span / spacing));
    if (steps < 1) {
        steps = 1;
    }
    for (int i = 0; i <= steps; ++i) {
        if (i == 0 && !includeStart) {
            continue;
        }
        const double u = u0 + (u1 - u0) * (static_cast<double>(i) / static_cast<double>(steps));
        const double ex = g.seg.p0.x + (g.seg.p1.x - g.seg.p0.x) * u;
        const double ey = g.seg.p0.y + (g.seg.p1.y - g.seg.p0.y) * u;
        const double x = ex + g.nOut.x * d;
        const double y = ey + g.nOut.y * d;
        EdgeRollWaypoint wp;
        wp.cartesian = makeCartesian(x, y, zContact, g.tUnit);
        wp.speedMmPerSec = speed;
        wp.tag = tag;
        out.push_back(wp);
    }
}

} // namespace

DeviceResult<EdgeRollPath> planEdgeRollPath(
    const FittedBoardOutline& outline,
    const EdgeRollParams& params) {
    const auto pst = validateParams(params);
    if (!pst.ok()) {
        return DeviceResult<EdgeRollPath>::failure(pst.code, pst.message);
    }
    if (outline.segments.size() < 3) {
        return DeviceResult<EdgeRollPath>::failure(
            DeviceError::InvalidArgument,
            "planEdgeRollPath needs at least 3 outline segments");
    }
    if (!std::isfinite(outline.pose.zMm) || !std::isfinite(outline.thicknessMm) ||
        !(outline.thicknessMm > 0.0)) {
        return DeviceResult<EdgeRollPath>::failure(
            DeviceError::InvalidArgument,
            "planEdgeRollPath: invalid pose.z or thickness");
    }

    auto geomsRes = buildSegGeom(outline);
    if (!geomsRes.ok()) {
        return DeviceResult<EdgeRollPath>::failure(
            geomsRes.status.code, geomsRes.status.message);
    }
    const auto& geoms = geomsRes.value;
    const std::size_t entry = longestSegmentIndex(geoms);
    const double d = params.rollerRadiusMm - params.squeezeMm;
    const double zContact = outline.pose.zMm - 0.5 * outline.thicknessMm;

    const SegGeom& entryG = geoms[entry];
    // Contact start S = mid-edge + n_out * d
    const double midX = 0.5 * (entryG.seg.p0.x + entryG.seg.p1.x);
    const double midY = 0.5 * (entryG.seg.p0.y + entryG.seg.p1.y);
    const double sX = midX + entryG.nOut.x * d;
    const double sY = midY + entryG.nOut.y * d;

    EdgeRollPath path;
    path.placementId = outline.placementId;
    path.params = params;
    path.offsetDistMm = d;
    path.entrySegmentIndex = entry;
    path.closedContact = true;

    // Approach: S + n_out * approachDist -> S
    {
        const double ax = sX + entryG.nOut.x * params.approachDistMm;
        const double ay = sY + entryG.nOut.y * params.approachDistMm;
        EdgeRollWaypoint a0;
        a0.cartesian = makeCartesian(ax, ay, zContact, entryG.tUnit);
        a0.speedMmPerSec = params.approachSpeedMmPerSec;
        a0.tag = EdgeRollTag::Approach;
        path.waypoints.push_back(a0);

        EdgeRollWaypoint a1;
        a1.cartesian = makeCartesian(sX, sY, zContact, entryG.tUnit);
        a1.speedMmPerSec = params.approachSpeedMmPerSec;
        a1.tag = EdgeRollTag::Approach;
        path.waypoints.push_back(a1);
    }

    // Contact loop starting at mid of entry segment (u=0.5), full CCW lap back to S.
    const std::size_t n = geoms.size();
    // First half of entry edge: u=0.5 -> 1.0
    appendSampledEdge(
        entryG, 0.5, 1.0, zContact, d, params.sampleSpacingMm,
        params.contactSpeedMmPerSec, EdgeRollTag::Contact,
        /*includeStart=*/true, path.waypoints);

    // Remaining full edges after entry
    for (std::size_t k = 1; k < n; ++k) {
        const SegGeom& g = geoms[(entry + k) % n];
        appendSampledEdge(
            g, 0.0, 1.0, zContact, d, params.sampleSpacingMm,
            params.contactSpeedMmPerSec, EdgeRollTag::Contact,
            /*includeStart=*/true, path.waypoints);
    }

    // Second half of entry edge: u=0.0 -> 0.5 (closes to S). Skip duplicate at u=0
    // if previous edge already ended at entry.p0 offset (different n_out -> corner).
    appendSampledEdge(
        entryG, 0.0, 0.5, zContact, d, params.sampleSpacingMm,
        params.contactSpeedMmPerSec, EdgeRollTag::Contact,
        /*includeStart=*/true, path.waypoints);

    // Force exact closure at S (last contact == first contact geometrically).
    {
        EdgeRollWaypoint closeWp;
        closeWp.cartesian = makeCartesian(sX, sY, zContact, entryG.tUnit);
        closeWp.speedMmPerSec = params.contactSpeedMmPerSec;
        closeWp.tag = EdgeRollTag::Contact;
        path.waypoints.push_back(closeWp);
    }

    // Retract: S -> S + n_out * retractDist
    {
        EdgeRollWaypoint r0;
        r0.cartesian = makeCartesian(sX, sY, zContact, entryG.tUnit);
        r0.speedMmPerSec = params.retractSpeedMmPerSec;
        r0.tag = EdgeRollTag::Retract;
        path.waypoints.push_back(r0);

        const double rx = sX + entryG.nOut.x * params.retractDistMm;
        const double ry = sY + entryG.nOut.y * params.retractDistMm;
        EdgeRollWaypoint r1;
        r1.cartesian = makeCartesian(rx, ry, zContact, entryG.tUnit);
        r1.speedMmPerSec = params.retractSpeedMmPerSec;
        r1.tag = EdgeRollTag::Retract;
        path.waypoints.push_back(r1);
    }

    if (path.waypoints.size() < 4) {
        return DeviceResult<EdgeRollPath>::failure(
            DeviceError::InternalError,
            "planEdgeRollPath produced too few waypoints");
    }
    return DeviceResult<EdgeRollPath>::success(std::move(path));
}

std::vector<CartesianWaypointMm> edgeRollPathToCartesian(const EdgeRollPath& path) {
    std::vector<CartesianWaypointMm> out;
    out.reserve(path.waypoints.size());
    for (const auto& wp : path.waypoints) {
        out.push_back(wp.cartesian);
    }
    return out;
}

std::vector<core::Target> fillTargetsFromEdgeRollPath(const EdgeRollPath& path) {
    std::vector<core::Target> targets;
    targets.reserve(path.waypoints.size());
    int idx = 0;
    for (const auto& wp : path.waypoints) {
        core::Target t;
        t.id = "edge_roll_" + std::to_string(idx++);
        switch (wp.tag) {
        case EdgeRollTag::Approach:
            t.name = "Approach";
            t.role = core::TargetRole::APPROACH;
            break;
        case EdgeRollTag::Contact:
            t.name = "Contact";
            t.role = core::TargetRole::WELD_VIA;
            break;
        case EdgeRollTag::Retract:
            t.name = "Retract";
            t.role = core::TargetRole::RETRACT;
            break;
        }
        t.motion = core::MotionType::L;
        t.speedValue = wp.speedMmPerSec;
        t.termination = core::TerminationType::CNT;
        t.cntValue = 100;
        core::WprPose wpr;
        wpr.x = wp.cartesian.positionMm.x;
        wpr.y = wp.cartesian.positionMm.y;
        wpr.z = wp.cartesian.positionMm.z;
        wpr.wDeg = wp.cartesian.wDeg;
        wpr.pDeg = wp.cartesian.pDeg;
        wpr.rDeg = wp.cartesian.rDeg;
        t.poseInUFrame = core::Transform4d::fromWPR(wpr);
        targets.push_back(std::move(t));
    }
    return targets;
}

} // namespace olp::runtime
