#pragma once

#include "olp/core/ProjectModel.h"
#include "olp/runtime/BoardOutlineFit.h"
#include "olp/runtime/DeviceTypes.h"

#include <string>
#include <vector>

namespace olp::runtime {

// ---------------------------------------------------------------------------
// Frame conventions (VE-D5) -- all geometry in robot BASE absolute mm.
// Do NOT multiply FittedBoardOutline.segments by SceneNode.localTransform.
//
// Contour is CCW in base XY:
//   edge tangent t = normalize(dx, dy)
//   outward normal n_out = normalize(dy, -dx)   // left-of-t is inward
// Contact TCP XY = p_edge + n_out * d,  d = R - delta
// Contact Z      = pose.zMm - 0.5 * thicknessMm
//
// Tool frame (right-handed, locked by tests):
//   +X_tool = t (travel)
//   +Z_tool = world -Z (0,0,-1) downward
//   +Y_tool = Z_tool x X_tool  (= n_out for planar CCW edges; outward)
//   Pressing into the board is along -Y_tool (= -n_out, inward).
// WPR from Transform4d::toWPR() with columns (X,Y,Z).
// ---------------------------------------------------------------------------

enum class EdgeRollTag {
    Approach,
    Contact,
    Retract
};

struct EdgeRollParams {
    double rollerRadiusMm{10.0};       // R
    double squeezeMm{2.5};             // delta; d = R - delta
    double contactSpeedMmPerSec{20.0};
    double approachDistMm{40.0};
    double retractDistMm{40.0};
    double approachSpeedMmPerSec{10.0};
    double retractSpeedMmPerSec{10.0};
    double sampleSpacingMm{5.0};       // dense L along edges
};

struct EdgeRollWaypoint {
    CartesianWaypointMm cartesian{};
    double speedMmPerSec{20.0};
    EdgeRollTag tag{EdgeRollTag::Contact};
};

struct EdgeRollPath {
    std::string placementId{};
    EdgeRollParams params{};
    std::vector<EdgeRollWaypoint> waypoints{};
    bool closedContact{true};
    double offsetDistMm{0.0};          // d = R - delta
    std::size_t entrySegmentIndex{0};  // longest edge (min index on ties)
};

// Plan Approach -> closed Contact loop -> Retract. No IK / FCL / .ls.
DeviceResult<EdgeRollPath> planEdgeRollPath(
    const FittedBoardOutline& outline,
    const EdgeRollParams& params = EdgeRollParams{});

// D6 helpers: strip to IRobot::executeTrajectory payloads / OLP Targets (UFRAME=I).
std::vector<CartesianWaypointMm> edgeRollPathToCartesian(const EdgeRollPath& path);

std::vector<core::Target> fillTargetsFromEdgeRollPath(const EdgeRollPath& path);

} // namespace olp::runtime
