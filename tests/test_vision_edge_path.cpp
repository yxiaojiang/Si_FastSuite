#include "test_support.h"

#include "olp/io/Utf8Path.h"
#include "olp/runtime/BoardOutlineFit.h"
#include "olp/runtime/BoardPerception.h"
#include "olp/runtime/ContourDataset.h"
#include "olp/runtime/EdgeRollPath.h"
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

olp::runtime::DeviceResult<olp::runtime::FittedBoardOutline> outlineFor(
    const std::string& placementId) {
    auto dataset = olp::runtime::ContourDataset::loadFromDirectory(
        olp::runtime::defaultVisionEdgeDatasetDir(workspaceRoot()));
    auto calib = olp::runtime::loadHandEyeCalibrationJson(
        olp::runtime::defaultVisionEdgeCalibDemoPath(workspaceRoot()));
    if (!dataset.ok()) {
        return olp::runtime::DeviceResult<olp::runtime::FittedBoardOutline>::failure(
            dataset.status.code, dataset.status.message);
    }
    if (!calib.ok()) {
        return olp::runtime::DeviceResult<olp::runtime::FittedBoardOutline>::failure(
            calib.status.code, calib.status.message);
    }
    auto frame = dataset.value.frame(placementId);
    if (!frame.ok()) {
        return olp::runtime::DeviceResult<olp::runtime::FittedBoardOutline>::failure(
            frame.status.code, frame.status.message);
    }
    auto perc =
        olp::runtime::perceiveFromContour(frame.value, calib.value);
    if (!perc.ok()) {
        return olp::runtime::DeviceResult<olp::runtime::FittedBoardOutline>::failure(
            perc.status.code, perc.status.message);
    }
    return olp::runtime::fitBoardOutline(perc.value);
}

int countTag(
    const olp::runtime::EdgeRollPath& path,
    olp::runtime::EdgeRollTag tag) {
    int n = 0;
    for (const auto& wp : path.waypoints) {
        if (wp.tag == tag) {
            ++n;
        }
    }
    return n;
}

} // namespace

void testVisionEdgeRollPath() {
    std::cout << "\n--- VE-D5: Edge roll path (constant offset, dense L) ---\n";

#if !defined(OLP_HAS_NLOHMANN_JSON)
    check(false, "VE-D5 requires OLP_HAS_NLOHMANN_JSON", "");
    return;
#else
    auto outlineA = outlineFor("placement_a");
    auto outlineB = outlineFor("placement_b");
    check(outlineA.ok() && outlineB.ok(), "VE-D5 build A/B outlines",
          outlineA.ok() ? outlineB.status.message : outlineA.status.message);
    if (!outlineA.ok() || !outlineB.ok()) {
        return;
    }

    olp::runtime::EdgeRollParams params; // defaults R=10, delta=2.5, d=7.5
    auto pathA = olp::runtime::planEdgeRollPath(outlineA.value, params);
    check(pathA.ok(), "planEdgeRollPath placement_a", pathA.status.message);
    if (!pathA.ok()) {
        return;
    }

    check(nearlyEqual(pathA.value.offsetDistMm, 7.5),
          "offset d = R - delta = 7.5",
          std::to_string(pathA.value.offsetDistMm));
    check(pathA.value.entrySegmentIndex == 0,
          "placement_a entry is longest-edge index 0 (tie -> min index)",
          std::to_string(pathA.value.entrySegmentIndex));
    check(countTag(pathA.value, olp::runtime::EdgeRollTag::Approach) >= 2,
          "has Approach waypoints",
          "");
    check(countTag(pathA.value, olp::runtime::EdgeRollTag::Contact) >= 4,
          "has Contact waypoints",
          "");
    check(countTag(pathA.value, olp::runtime::EdgeRollTag::Retract) >= 2,
          "has Retract waypoints",
          "");

    // First contact after approach should be entry mid offset S.
    // Entry edge (960,550)->(960,750), n_out=(1,0), mid=(960,650), S=(967.5,650)
    // z = 800 - 9 = 791
    const auto& wps = pathA.value.waypoints;
    check(wps.size() >= 4, "path has enough waypoints",
          std::to_string(wps.size()));
    const auto& approachEnd = wps[1];
    check(approachEnd.tag == olp::runtime::EdgeRollTag::Approach,
          "second waypoint is Approach end at S",
          "");
    check(nearlyEqual(approachEnd.cartesian.positionMm.x, 967.5) &&
              nearlyEqual(approachEnd.cartesian.positionMm.y, 650.0) &&
              nearlyEqual(approachEnd.cartesian.positionMm.z, 791.0),
          "contact start S gold (967.5,650,791)",
          "xyz=(" + std::to_string(approachEnd.cartesian.positionMm.x) + "," +
              std::to_string(approachEnd.cartesian.positionMm.y) + "," +
              std::to_string(approachEnd.cartesian.positionMm.z) + ")");

    // Horizontal distance from a mid-edge contact sample to supporting edge ~= d
    bool foundContactDist = false;
    for (const auto& wp : wps) {
        if (wp.tag != olp::runtime::EdgeRollTag::Contact) {
            continue;
        }
        // Samples on entry edge have x ~= 967.5 (vertical edge at x=960)
        if (nearlyEqual(wp.cartesian.positionMm.x, 967.5, 1e-3)) {
            const double dist = std::fabs(wp.cartesian.positionMm.x - 960.0);
            check(nearlyEqual(dist, 7.5, 1e-3),
                  "contact TCP standoff to entry edge ~= d",
                  "dist=" + std::to_string(dist));
            foundContactDist = true;
            break;
        }
    }
    check(foundContactDist, "found contact sample on entry edge", "");

    // Attitude gold: tool +Z downward. Rebuild from WPR.
    {
        olp::core::WprPose wpr;
        wpr.x = approachEnd.cartesian.positionMm.x;
        wpr.y = approachEnd.cartesian.positionMm.y;
        wpr.z = approachEnd.cartesian.positionMm.z;
        wpr.wDeg = approachEnd.cartesian.wDeg;
        wpr.pDeg = approachEnd.cartesian.pDeg;
        wpr.rDeg = approachEnd.cartesian.rDeg;
        const auto T = olp::core::Transform4d::fromWPR(wpr);
        const auto zAxis = T.rot.colZ();
        check(nearlyEqual(zAxis.x, 0.0, 1e-6) &&
                  nearlyEqual(zAxis.y, 0.0, 1e-6) &&
                  nearlyEqual(zAxis.z, -1.0, 1e-6),
              "tool +Z is world -Z at entry contact",
              "zAxis=(" + std::to_string(zAxis.x) + "," +
                  std::to_string(zAxis.y) + "," + std::to_string(zAxis.z) + ")");
        const auto xAxis = T.rot.colX();
        check(nearlyEqual(xAxis.x, 0.0, 1e-6) &&
                  nearlyEqual(xAxis.y, 1.0, 1e-6),
              "tool +X follows entry tangent +Y",
              "");
        check(nearlyEqual(T.rot.determinant(), 1.0, 1e-6),
              "tool frame rotation det ~= +1",
              std::to_string(T.rot.determinant()));
    }

    // Closed contact: last Contact before Retract near S
    std::size_t lastContact = wps.size();
    for (std::size_t i = 0; i < wps.size(); ++i) {
        if (wps[i].tag == olp::runtime::EdgeRollTag::Contact) {
            lastContact = i;
        }
    }
    check(lastContact < wps.size(), "found last Contact waypoint", "");
    if (lastContact < wps.size()) {
        check(nearlyEqual(wps[lastContact].cartesian.positionMm.x, 967.5, 1e-3) &&
                  nearlyEqual(wps[lastContact].cartesian.positionMm.y, 650.0, 1e-3),
              "contact loop closes at S",
              "");
    }

    auto pathB = olp::runtime::planEdgeRollPath(outlineB.value, params);
    check(pathB.ok(), "planEdgeRollPath placement_b", pathB.status.message);
    if (pathB.ok()) {
        check(pathA.value.placementId != pathB.value.placementId,
              "A/B path placementIds differ",
              "");
        // Same params, different geometry -> different first contact XY
        const auto& sA = pathA.value.waypoints[1].cartesian.positionMm;
        const auto& sB = pathB.value.waypoints[1].cartesian.positionMm;
        check(std::hypot(sA.x - sB.x, sA.y - sB.y) > 1.0,
              "A/B contact starts differ with same params",
              "delta=" + std::to_string(std::hypot(sA.x - sB.x, sA.y - sB.y)));
    }

    // D6 helpers
    const auto carts = olp::runtime::edgeRollPathToCartesian(pathA.value);
    check(carts.size() == pathA.value.waypoints.size(),
          "edgeRollPathToCartesian size matches",
          "");
    const auto targets =
        olp::runtime::fillTargetsFromEdgeRollPath(pathA.value);
    check(targets.size() == pathA.value.waypoints.size() &&
              targets.front().motion == olp::core::MotionType::L,
          "fillTargetsFromEdgeRollPath produces L targets",
          "");

    // Failure paths
    olp::runtime::EdgeRollParams bad = params;
    bad.squeezeMm = 10.0; // R == delta
    auto fail = olp::runtime::planEdgeRollPath(outlineA.value, bad);
    check(!fail.ok() &&
              fail.status.code == olp::runtime::DeviceError::InvalidArgument,
          "R <= delta rejected",
          fail.status.message);

    olp::runtime::FittedBoardOutline empty = outlineA.value;
    empty.segments.clear();
    bool threw = false;
    try {
        (void)olp::runtime::planEdgeRollPath(empty, params);
    } catch (const std::exception&) {
        threw = true;
    }
    check(!threw, "empty outline does not throw", "");
    auto emptyRes = olp::runtime::planEdgeRollPath(empty, params);
    check(!emptyRes.ok(), "empty outline -> DeviceResult failure",
          emptyRes.status.message);

    check(true, "VE-D5 does not claim empty-mesh FCL clearance", "");
#endif
}
