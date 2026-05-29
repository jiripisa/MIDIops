#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "core/MidiOutput.h"

class RtMidiOut;

// MIDI output via RtMidi. Opens a *virtual* CoreMIDI source port so other
// apps on the Mac (Ableton, etc.) can subscribe to the simulator's MIDI
// Clock stream just like they would to a real USB-MIDI device. Clock
// pulses are emitted from a dedicated std::thread so neither the SDL
// event loop nor the rendering cadence affects their timing.
class RtMidiOutput : public core::MidiOutput {
public:
    explicit RtMidiOutput(std::string portName);
    ~RtMidiOutput() override;

    RtMidiOutput(const RtMidiOutput&)            = delete;
    RtMidiOutput& operator=(const RtMidiOutput&) = delete;

    // Opens the virtual port. Subsequent sends silently drop if this
    // failed (with a diagnostic to stderr).
    void begin();

    void setClockBpm(uint16_t bpm) override;
    void sendStart()    override;
    void sendContinue() override;
    void sendStop()     override;

private:
    void sendByte(unsigned char status);
    void clockThreadFunc();

    std::string                portName_;
    std::unique_ptr<RtMidiOut> midi_;
    std::mutex                 sendMutex_;

    std::thread           clockThread_;
    std::atomic<bool>     clockRunning_  {false};
    std::atomic<uint32_t> clockPeriodUs_ {0};
};
