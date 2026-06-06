#pragma once

#include <cstdint>

#include "core/Scale.h"
#include "core/app/Mode.h"

namespace core {

// Global services the shell exposes to modes. Grows over time (settings,
// scale, note state). Plan 1: tempo + transport read/control.
class AppServices {
public:
    virtual ~AppServices() = default;
    virtual uint16_t bpm() const = 0;
    virtual void     setBpm(uint16_t bpm) = 0;
    virtual Transport transport() const = 0;
    virtual const Scale& scale() const = 0;
    virtual void setScaleType(Scale::Type t) = 0;
    virtual void setScaleRoot(uint8_t pc) = 0;
};

} // namespace core
