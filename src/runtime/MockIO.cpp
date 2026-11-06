#include "olp/runtime/MockIO.h"

namespace olp::runtime {

DeviceStatus MockIO::connect() {
    connected_ = true;
    return DeviceStatus::success("MockIO connected");
}

DeviceStatus MockIO::disconnect() {
    connected_ = false;
    return DeviceStatus::success("MockIO disconnected");
}

bool MockIO::isConnected() const noexcept {
    return connected_;
}

DeviceResult<bool> MockIO::readDi(int channel) {
    if (!connected_) {
        return DeviceResult<bool>::failure(
            DeviceError::NotConnected,
            "MockIO is not connected");
    }
    if (channel < 0) {
        return DeviceResult<bool>::failure(
            DeviceError::InvalidArgument,
            "DI channel must be >= 0");
    }
    const auto it = di_.find(channel);
    const bool value = (it == di_.end()) ? false : it->second;
    return DeviceResult<bool>::success(value);
}

DeviceStatus MockIO::writeDo(int channel, bool value) {
    if (!connected_) {
        return DeviceStatus::failure(
            DeviceError::NotConnected,
            "MockIO is not connected");
    }
    if (channel < 0) {
        return DeviceStatus::failure(
            DeviceError::InvalidArgument,
            "DO channel must be >= 0");
    }
    do_[channel] = value;
    return DeviceStatus::success("Wrote DO");
}

void MockIO::setDi(int channel, bool value) {
    di_[channel] = value;
}

bool MockIO::doValue(int channel) const {
    const auto it = do_.find(channel);
    return (it == do_.end()) ? false : it->second;
}

} // namespace olp::runtime
