#pragma once

#include <cstdint>

#include "core/Scale.h"
#include "core/app/Mode.h"

namespace core {

enum class ClockSource : uint8_t { Internal = 0, External = 1 };

enum class TransportMode : uint8_t { Off = 0, Send, Receive, kCount };

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
    virtual uint8_t  midiOutChannel() const = 0;          // 1..16
    virtual void     setMidiOutChannel(uint8_t ch) = 0;
    virtual uint8_t  midiInChannel() const = 0;           // 0 = OMNI, 1..16
    virtual void     setMidiInChannel(uint8_t ch) = 0;
    virtual ClockSource clockSource() const = 0;
    virtual void     setClockSource(ClockSource s) = 0;
    virtual TransportMode transportMode() const = 0;
    virtual void          setTransportMode(TransportMode m) = 0;
    // A capturing mode reports its local transport action. The shell mirrors
    // transportState_ (top bar) and emits MIDI transport when mode == Send.
    // Does NOT call back into the mode's onTransport (the mode already acted).
    virtual void          notifyLocalTransport(Transport t) = 0;
};

} // namespace core
