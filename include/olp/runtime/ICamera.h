#pragma once

#include "olp/runtime/DeviceTypes.h"

#include <string>

namespace olp::runtime {

// Hardware-agnostic camera boundary for Vision_Edge.
// Capture returns a contour frame suitable for demo Mode 2 (no CAD).
// Real SDK adapters must implement this interface without leaking vendor types.
class ICamera {
public:
    virtual ~ICamera() = default;

    virtual DeviceStatus connect() = 0;
    virtual DeviceStatus disconnect() = 0;
    [[nodiscard]] virtual bool isConnected() const noexcept = 0;

    // Capture / replay one contour frame (depth / point cloud may be metadata-only).
    virtual DeviceResult<ContourFrame> captureContour() = 0;

    // Optional: select which named placement the next capture should return (Mock).
    // Real cameras may return NotSupported.
    virtual DeviceStatus selectPlacement(const std::string& placementId) {
        (void)placementId;
        return DeviceStatus::failure(
            DeviceError::NotSupported,
            "selectPlacement is not supported by this camera adapter");
    }
};

} // namespace olp::runtime
