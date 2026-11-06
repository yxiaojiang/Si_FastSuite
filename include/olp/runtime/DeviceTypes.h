#pragma once

#include "olp/core/MathTypes.h"

#include <array>
#include <string>
#include <utility>
#include <vector>

namespace olp::runtime {

// Unified device-layer error codes for camera / robot / IO adapters.
// Real SDK drivers (future) and Mock adapters share this vocabulary.
enum class DeviceError {
    Ok = 0,
    NotConnected,
    NotFound,
    InvalidArgument,
    IoFailure,
    Timeout,
    NotSupported,
    InternalError
};

inline const char* deviceErrorName(DeviceError code) noexcept {
    switch (code) {
    case DeviceError::Ok: return "Ok";
    case DeviceError::NotConnected: return "NotConnected";
    case DeviceError::NotFound: return "NotFound";
    case DeviceError::InvalidArgument: return "InvalidArgument";
    case DeviceError::IoFailure: return "IoFailure";
    case DeviceError::Timeout: return "Timeout";
    case DeviceError::NotSupported: return "NotSupported";
    case DeviceError::InternalError: return "InternalError";
    }
    return "Unknown";
}

struct DeviceStatus {
    DeviceError code{DeviceError::Ok};
    std::string message{};

    [[nodiscard]] bool ok() const noexcept { return code == DeviceError::Ok; }

    static DeviceStatus success(std::string message = {}) {
        return DeviceStatus{DeviceError::Ok, std::move(message)};
    }

    static DeviceStatus failure(DeviceError code, std::string message) {
        return DeviceStatus{code, std::move(message)};
    }
};

template <typename T>
struct DeviceResult {
    DeviceStatus status{};
    T value{};

    [[nodiscard]] bool ok() const noexcept { return status.ok(); }

    static DeviceResult success(T value, std::string message = {}) {
        DeviceResult out;
        out.status = DeviceStatus::success(std::move(message));
        out.value = std::move(value);
        return out;
    }

    static DeviceResult failure(DeviceError code, std::string message) {
        DeviceResult out;
        out.status = DeviceStatus::failure(code, std::move(message));
        return out;
    }
};

// Demo contour point in millimeters (board frame or camera frame, see ContourFrame::frameId).
struct ContourPointMm {
    double x{0.0};
    double y{0.0};
    double z{0.0};
};

// One captured / replayed board outline for Vision_Edge demo Mode 2.
struct ContourFrame {
    std::string placementId{};   // e.g. "placement_a", "placement_b"
    std::string frameId{"camera"};
    std::vector<ContourPointMm> polygonMm{};
    double boardThicknessMm{18.0};
    // Synthetic point-cloud metadata only; no real depth buffer in VE-D1.
    int fakePointCount{0};
    double timestampSec{0.0};
};

struct RobotProgram {
    std::string name{};
    std::string content{}; // Native controller text (e.g. FANUC .ls) or empty stub.
};

struct CartesianWaypointMm {
    core::Vec3 positionMm{};
    // FANUC-style WPR degrees when populated; identity attitude if all zero.
    double wDeg{0.0};
    double pDeg{0.0};
    double rDeg{0.0};
};

struct JointWaypointRad {
    std::array<double, 6> jointsRad{{0.0, 0.0, 0.0, 0.0, 0.0, 0.0}};
};

struct RobotState {
    bool connected{false};
    bool programRunning{false};
    std::string lastProgramName{};
    std::string statusText{"idle"};
};

} // namespace olp::runtime
