#include "olp/runtime/MockCamera.h"

namespace olp::runtime {

MockCamera::MockCamera(ContourDataset dataset)
    : dataset_(std::move(dataset)) {
    if (!dataset_.placementIds().empty()) {
        selectedPlacementId_ = dataset_.placementIds().front();
    }
}

DeviceStatus MockCamera::connect() {
    if (dataset_.placementIds().empty()) {
        connected_ = false;
        return DeviceStatus::failure(
            DeviceError::NotFound,
            "MockCamera has no placements loaded");
    }
    connected_ = true;
    return DeviceStatus::success("MockCamera connected");
}

DeviceStatus MockCamera::disconnect() {
    connected_ = false;
    return DeviceStatus::success("MockCamera disconnected");
}

bool MockCamera::isConnected() const noexcept {
    return connected_;
}

DeviceStatus MockCamera::selectPlacement(const std::string& placementId) {
    if (!dataset_.hasPlacement(placementId)) {
        return DeviceStatus::failure(
            DeviceError::NotFound,
            "MockCamera unknown placement: " + placementId);
    }
    selectedPlacementId_ = placementId;
    return DeviceStatus::success("Selected placement " + placementId);
}

DeviceResult<ContourFrame> MockCamera::captureContour() {
    if (!connected_) {
        return DeviceResult<ContourFrame>::failure(
            DeviceError::NotConnected,
            "MockCamera is not connected");
    }
    if (selectedPlacementId_.empty()) {
        return DeviceResult<ContourFrame>::failure(
            DeviceError::InvalidArgument,
            "MockCamera has no selected placement");
    }
    return dataset_.frame(selectedPlacementId_);
}

} // namespace olp::runtime
