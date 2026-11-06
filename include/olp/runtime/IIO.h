#pragma once

#include "olp/runtime/DeviceTypes.h"

namespace olp::runtime {

// Minimal digital IO boundary (vacuum / start / ready lights, etc.).
// Demo scope may leave channels unused; interface exists so later stages
// do not invent a second IO vocabulary.
class IIO {
public:
    virtual ~IIO() = default;

    virtual DeviceStatus connect() = 0;
    virtual DeviceStatus disconnect() = 0;
    [[nodiscard]] virtual bool isConnected() const noexcept = 0;

    virtual DeviceResult<bool> readDi(int channel) = 0;
    virtual DeviceStatus writeDo(int channel, bool value) = 0;
};

} // namespace olp::runtime
