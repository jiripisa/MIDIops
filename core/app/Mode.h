#pragma once

#include <cstdint>

#include "core/MidiMessage.h"

namespace core {

class Display;

// Transport actions produced by the Latch1-3 panel switches.
enum class Transport { Play, Pause, Stop, Reset };

// Raw hardware event, used only by observability modes (Debug). Normal
// modes use the semantic Screen/Mode callbacks instead. Indices are
// 1-based: encoders 1..5, latches 1..3.
struct RawInput {
    enum class Kind { EncoderKnob, EncoderSw, Latch };
    Kind kind;
    int  index   = 0;
    int  delta   = 0;      // EncoderKnob only
    bool on      = false;  // Latch only
};

// One interactive page inside a Mode. Receives encoders 1..4 only;
// encoder 5 is reserved by the shell (screen switch / mode overlay).
class Screen {
public:
    virtual ~Screen() = default;
    virtual const char* name() const = 0;
    virtual void onEnter() {}
    virtual void onExit() {}
    virtual void onEncoder(int index, int delta) { (void)index; (void)delta; }
    virtual void onEncoderSw(int index) { (void)index; }
    virtual void update(uint32_t nowMs) { (void)nowMs; }
    virtual void render(Display& d) const = 0;
};

// A top-level unit of behaviour. Owns its screens and mode-local state.
class Mode {
public:
    virtual ~Mode() = default;
    virtual const char* name() const = 0;
    virtual int     screenCount() const = 0;
    virtual Screen& screen(int i) = 0;
    virtual void onEnter() {}
    virtual void onExit() {}
    virtual void onMidiIn(const MidiMessage& msg) { (void)msg; }
    virtual void onTransport(Transport t) { (void)t; }
    virtual void onRawInput(const RawInput& in) { (void)in; }
    virtual void update(uint32_t nowMs) { (void)nowMs; }
};

} // namespace core
