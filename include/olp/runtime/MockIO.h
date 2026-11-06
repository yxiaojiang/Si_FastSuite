#pragma once

#include "olp/runtime/IIO.h"

#include <unordered_map>

namespace olp::runtime {

// In-memory DI/DO Mock for demo IO. Channels default to false.
class MockIO final : public IIO {
public:
    MockIO() = default;

    DeviceStatus connect() override;
    DeviceStatus disconnect() override;
    [[nodiscard]] bool isConnected() const noexcept override;

    DeviceResult<bool> readDi(int channel) override;
    DeviceStatus writeDo(int channel, bool value) override;

    // Test helper: seed a DI value without going through hardware.
    void setDi(int channel, bool value);

    [[nodiscard]] bool doValue(int channel) const;

private:
    bool connected_{false};
    std::unordered_map<int, bool> di_{};
    std::unordered_map<int, bool> do_{};
};

} // namespace olp::runtime
