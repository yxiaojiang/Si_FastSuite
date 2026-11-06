#pragma once

#include "olp/runtime/ContourDataset.h"
#include "olp/runtime/ICamera.h"

#include <string>

namespace olp::runtime {

// Disk-backed camera Mock: replays fixed placement A / B contour frames.
// Does not open network sockets or vendor SDKs.
class MockCamera final : public ICamera {
public:
    explicit MockCamera(ContourDataset dataset);

    DeviceStatus connect() override;
    DeviceStatus disconnect() override;
    [[nodiscard]] bool isConnected() const noexcept override;

    DeviceResult<ContourFrame> captureContour() override;
    DeviceStatus selectPlacement(const std::string& placementId) override;

    [[nodiscard]] const std::string& selectedPlacementId() const noexcept {
        return selectedPlacementId_;
    }

private:
    ContourDataset dataset_;
    bool connected_{false};
    std::string selectedPlacementId_{};
};

} // namespace olp::runtime
