#pragma once

#include "olp/runtime/DeviceTypes.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace olp::runtime {

// Loads synthetic Vision_Edge contour placements from a dataset directory.
// Supported files per placement: <id>.json (preferred) and/or <id>.csv.
// No camera SDK dependency.
class ContourDataset {
public:
    // Load all placement_*.json / placement_*.csv under datasetDir.
    // Returns failure if directory missing or no placements found.
    static DeviceResult<ContourDataset> loadFromDirectory(const std::string& datasetDirUtf8);

    [[nodiscard]] const std::vector<std::string>& placementIds() const noexcept {
        return placementIds_;
    }

    [[nodiscard]] bool hasPlacement(const std::string& placementId) const;

    // Returns NotFound if placementId is unknown.
    DeviceResult<ContourFrame> frame(const std::string& placementId) const;

private:
    std::vector<std::string> placementIds_{};
    std::unordered_map<std::string, ContourFrame> frames_{};
};

// Resolve default VE-D1 dataset path from workspace root
// (<workspace>/tests/data/vision_edge).
std::string defaultVisionEdgeDatasetDir(const std::string& workspaceRootUtf8);

} // namespace olp::runtime
