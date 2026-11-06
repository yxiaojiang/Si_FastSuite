#pragma once

#include "olp/core/ProjectModel.h"
#include "olp/runtime/BoardPerception.h"

#include <string>
#include <vector>

namespace olp::runtime {

// Straight edge between two base-frame contour vertices (mm).
struct LineSegmentMm {
    ContourPointMm p0{};
    ContourPointMm p1{};

    [[nodiscard]] double lengthXyMm() const noexcept;
};

// Minimal Mode 2 outline fit (VE-D4): polyline of straight segments only.
// No RANSAC, arcs, B-splines, OpenCV, or Open3D.
struct FittedBoardOutline {
    std::string placementId{};
    BoardPoseMm pose{};
    double thicknessMm{18.0};
    std::vector<LineSegmentMm> segments{}; // size N; last segment closes to first vertex
    // Populated when N == 4 (demo rectangle side lengths); otherwise empty.
    std::vector<double> sideLengthsMm{};
};

// Descriptor that bridges perception/fit into the existing scene graph type
// without extending ProjectModel. Meshes stay empty; cadHandleId stays -1
// (no OCCT required for the VE-D4 headless gate).
struct TransientBoardWorkpiece {
    FittedBoardOutline outline{};
    BoardPerception perception{};
    core::SceneNode node{};
};

DeviceResult<FittedBoardOutline> fitBoardOutline(const BoardPerception& perception);

DeviceResult<TransientBoardWorkpiece> makeTransientBoardWorkpiece(
    const BoardPerception& perception);

} // namespace olp::runtime
