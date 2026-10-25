#pragma once

#include "olp/algo/CollisionAndSim.h"
#include "olp/core/ProjectModel.h"
#include <string>
#include <vector>

namespace olp::io {

// M13 (P0 Baseline): Single-File Offline HTML5 3D Animation Package Exporter
class HtmlAnimationExporter {
public:
    static bool exportSingleFileHtml(
        const std::string& outputFilePath,
        const core::Program& program,
        const std::vector<core::SceneNode>& sceneNodes,
        const algo::SimulationResult& simResult);
};

} // namespace olp::io
