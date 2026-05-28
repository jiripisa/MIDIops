#pragma once

#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "core/MidiInput.h"

class RtMidiIn;

// MIDI input backed by RtMidi. Opens a *virtual* CoreMIDI input port so that
// other apps (Ableton, the IAC Driver, etc.) can route MIDI into it.
class RtMidiInput : public core::MidiInput {
public:
    explicit RtMidiInput(std::string portName);
    ~RtMidiInput() override;

    RtMidiInput(const RtMidiInput&)            = delete;
    RtMidiInput& operator=(const RtMidiInput&) = delete;

    // Opens the virtual port. Prints a diagnostic to stderr on failure.
    void begin();

    bool poll(core::MidiMessage& out) override;

    // Direct injection used by the keyboard test harness in host main.cpp.
    void inject(const core::MidiMessage& msg);

private:
    static void rtMidiCallback(double timestamp,
                               std::vector<unsigned char>* bytes,
                               void* userdata);

    std::string                 portName_;
    std::unique_ptr<RtMidiIn>   midi_;
    std::mutex                  mu_;
    std::deque<core::MidiMessage> queue_;
};
