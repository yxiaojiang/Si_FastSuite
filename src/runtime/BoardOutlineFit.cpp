#include "olp/runtime/BoardOutlineFit.h"

#include <cmath>
#include <sstream>

namespace olp::runtime {
namespace {

bool isFinitePoint(const ContourPointMm& p) noexcept {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}

bool isFinitePose(const BoardPoseMm& pose) noexcept {
    return std::isfinite(pose.xMm) && std::isfinite(pose.yMm) &&
           std::isfinite(pose.zMm) && std::isfinite(pose.yawRad);
}

constexpr double kMinEdgeLenMm = 1e-6;

core::Transform4d transformFromBoardPose(const BoardPoseMm& pose) noexcept {
    core::Transform4d T;
    T.pos = {pose.xMm, pose.yMm, pose.zMm};
    T.rot = core::Mat3::rotZ(pose.yawRad);
    return T;
}

} // namespace

double LineSegmentMm::lengthXyMm() const noexcept {
    const double dx = p1.x - p0.x;
    const double dy = p1.y - p0.y;
    return std::sqrt(dx * dx + dy * dy);
}

DeviceResult<FittedBoardOutline> fitBoardOutline(const BoardPerception& perception) {
    if (perception.edgeBaseMm.size() < 3) {
        return DeviceResult<FittedBoardOutline>::failure(
            DeviceError::InvalidArgument,
            "fitBoardOutline needs at least 3 edge vertices");
    }
    if (!isFinitePose(perception.pose)) {
        return DeviceResult<FittedBoardOutline>::failure(
            DeviceError::InvalidArgument,
            "fitBoardOutline: board pose has non-finite components");
    }
    if (!std::isfinite(perception.thicknessMm) || perception.thicknessMm <= 0.0) {
        return DeviceResult<FittedBoardOutline>::failure(
            DeviceError::InvalidArgument,
            "fitBoardOutline: thicknessMm must be positive and finite");
    }

    const auto& verts = perception.edgeBaseMm;
    for (const auto& p : verts) {
        if (!isFinitePoint(p)) {
            return DeviceResult<FittedBoardOutline>::failure(
                DeviceError::InvalidArgument,
                "fitBoardOutline: non-finite edge vertex");
        }
    }

    FittedBoardOutline out;
    out.placementId = perception.placementId;
    out.pose = perception.pose;
    out.thicknessMm = perception.thicknessMm;
    out.segments.reserve(verts.size());

    const std::size_t n = verts.size();
    for (std::size_t i = 0; i < n; ++i) {
        LineSegmentMm seg;
        seg.p0 = verts[i];
        seg.p1 = verts[(i + 1) % n];
        const double len = seg.lengthXyMm();
        if (!(len >= kMinEdgeLenMm)) {
            std::ostringstream oss;
            oss << "fitBoardOutline: zero-length edge at index " << i;
            return DeviceResult<FittedBoardOutline>::failure(
                DeviceError::InvalidArgument, oss.str());
        }
        out.segments.push_back(seg);
    }

    if (n == 4) {
        out.sideLengthsMm.reserve(4);
        for (const auto& seg : out.segments) {
            out.sideLengthsMm.push_back(seg.lengthXyMm());
        }
    }

    return DeviceResult<FittedBoardOutline>::success(std::move(out));
}

DeviceResult<TransientBoardWorkpiece> makeTransientBoardWorkpiece(
    const BoardPerception& perception) {
    auto fitted = fitBoardOutline(perception);
    if (!fitted.ok()) {
        return DeviceResult<TransientBoardWorkpiece>::failure(
            fitted.status.code, fitted.status.message);
    }

    TransientBoardWorkpiece tw;
    tw.outline = std::move(fitted.value);
    tw.perception = perception;

    core::SceneNode node;
    const std::string placement =
        perception.placementId.empty() ? std::string("unknown")
                                       : perception.placementId;
    node.id = "vision_board_" + placement;
    node.name = "Vision Edge transient board (" + placement + ")";
    node.type = core::SceneNodeType::Part;
    node.collisionRole = core::CollisionRole::Workpiece;
    node.localTransform = transformFromBoardPose(perception.pose);
    node.parentId.clear();
    node.visible = true;
    node.locked = false;
    node.sourceCadFile = "vision_edge_transient";
    node.cadHandleId = -1;
    // renderMesh / collisionMesh intentionally empty for headless VE-D4 gate.
    tw.node = std::move(node);

    return DeviceResult<TransientBoardWorkpiece>::success(std::move(tw));
}

} // namespace olp::runtime
