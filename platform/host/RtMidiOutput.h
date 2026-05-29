#pragma once

#include <memory>
#include <string>

#include "core/MidiOutput.h"

class RtMidiOut;

// MIDI output via RtMidi. Opens a *virtual* CoreMIDI source port so other
// apps on the Mac (Ableton, etc.) can subscribe to the simulator's MIDI
// Clock stream just like they would to a real USB-MIDI device.
class RtMidiOutput : public core::MidiOutput {
public:
    explicit RtMidiOutput(std::string portName);
    ~RtMidiOutput() override;

    RtMidiOutput(const RtMidiOutput&)            = delete;
    RtMidiOutput& operator=(const RtMidiOutput&) = delete;

    // Opens the virtual port. Prints a diagnostic to stderr on failure
    // (subsequent send* calls then silently drop their messages).
    void begin();

    void sendClock()    override;
    void sendStart()    override;
    void sendContinue() override;
    void sendStop()     override;

private:
    void sendByte(unsigned char status);

    std::string                portName_;
    std::unique_ptr<RtMidiOut>  midi_;
};
