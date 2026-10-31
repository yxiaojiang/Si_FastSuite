#pragma once

#include "olp/algo/CollisionAndSim.h"
#include "olp/core/ProjectModel.h"
#include "olp/device/RobotModel.h"
#include <cstddef>
#include <string>
#include <vector>

namespace olp::io {

struct HtmlExportOptions {
    std::size_t maxTrianglesPerPartMesh{2500};
    std::size_t maxTrianglesPerRobotLink{1200};
    double keyframePosTolMm{0.25};
    double keyframeJointTolDeg{0.15};
    bool includeRobotLinkMeshes{true};
};

// M13 (P0 Baseline): Single-File Offline HTML5 3D Animation Package Exporter
class HtmlAnimationExporter {
public:
    static bool exportSingleFileHtml(
        const std::string& outputFilePath,
        const core::Program& program,
        const std::vector<core::SceneNode>& sceneNodes,
        const algo::SimulationResult& simResult,
        const device::RobotModel* robotModel = nullptr,
        const HtmlExportOptions& options = {},
        std::string* errorOut = nullptr);
};

} // namespace olp::io
