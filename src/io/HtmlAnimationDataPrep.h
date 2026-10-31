#pragma once

#include "olp/algo/CollisionAndSim.h"
#include "olp/core/ProjectModel.h"
#include "olp/device/RobotModel.h"
#include "olp/io/HtmlAnimationExporter.h"

#include <array>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

namespace olp::io {

struct CompactMeshData {
    std::vector<core::Vec3> vertices{};
    std::vector<std::array<std::uint32_t, 3>> triangles{};
    core::Vec3 bboxMin{0.0, 0.0, 0.0};
    core::Vec3 bboxMax{0.0, 0.0, 0.0};
};

struct WaypointExportRecord {
    std::size_t index{0};
    std::string id;
    std::string name;
    std::string opName;
    std::string role;
    bool isWeld{false};
    core::Vec3 worldPos{0.0, 0.0, 0.0};
    core::Vec3 dirZ{0.0, 0.0, 1.0};
    std::size_t frameIdx{0};
};

struct OperationExportRecord {
    std::size_t index{0};
    std::string id;
    std::string name;
    std::string seamId;
    std::size_t startFrameIdx{0};
    std::size_t weldStartFrameIdx{0};
    double startTimeSec{0.0};
    double weldStartTimeSec{0.0};
};

// String escaping & JSON writing utilities
std::string escapeJson(const std::string& s);
std::string escapeHtmlText(const std::string& s);
void writeMeshArraysJson(std::ostream& os, const CompactMeshData& mesh);
void writeTransform12Json(std::ostream& os, const core::Transform4d& tf);
std::string roleNameString(core::CollisionRole role);
std::string targetRoleString(core::TargetRole role);

// Mesh generation & loading
core::TriMesh makeBoxTriMesh(const core::Vec3& rawMin, const core::Vec3& rawMax);
bool loadBinaryRobotStlMm(const std::string& filePath, core::TriMesh& outMesh);
core::TriMesh makeFallbackRobotLinkMesh(std::size_t linkIdx);

// Tool0 transform and welding torch mesh in link_6 coordinate space
core::Transform4d computeFlangeToTool0(const device::RobotModel& robot);
core::TriMesh makeWeldingTorchMeshInLink6(
    const core::Transform4d& utoolOffset,
    const core::Transform4d& flangeToTool0,
    core::Vec3* outTcpLocalInLink6 = nullptr);

// Decimation & spatial hash compaction
CompactMeshData compactAndDecimateMesh(
    const core::TriMesh& src,
    const core::Transform4d& worldTf,
    std::size_t maxTriangles);

// Animation frames normalization & decimation
std::vector<algo::BakedFrame> normalizeAndDecimateFrames(
    const std::vector<algo::BakedFrame>& rawFrames,
    const core::Transform4d& robotBaseInWorld,
    const core::Transform4d& utoolOffset,
    const device::RobotModel& robotModel,
    const HtmlExportOptions& options);

// Operations and waypoints extraction
void extractOperationsAndWaypoints(
    const core::Program& program,
    const core::Transform4d& uframeInWorld,
    const std::vector<algo::BakedFrame>& decimatedFrames,
    std::vector<OperationExportRecord>& outOps,
    std::vector<WaypointExportRecord>& outWps);

} // namespace olp::io
