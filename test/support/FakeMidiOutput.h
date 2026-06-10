#pragma once

#include <cstdint>
#include <vector>

#include "core/MidiOutput.h"

// Counts transport + clock calls for assertions, and records all note events.
struct FakeMidiOutput : core::MidiOutput {
    uint16_t lastBpm = 0;
    int starts = 0, continues = 0, stops = 0;

    struct Ev { bool isOn; uint8_t channel; uint8_t note; uint8_t vel; };
    std::vector<Ev> events;

    void setClockBpm(uint16_t bpm) override { lastBpm = bpm; }
    void sendStart()    override { ++starts; }
    void sendContinue() override { ++continues; }
    void sendStop()     override { ++stops; }
    void sendNoteOn (uint8_t channel, uint8_t note, uint8_t velocity) override {
        events.push_back({true, channel, note, velocity});
    }
    void sendNoteOff(uint8_t channel, uint8_t note) override {
        events.push_back({false, channel, note, 0});
    }

    uint32_t pendingTicks = 0;   // tests set this; consumeClockTicks() drains it
    uint32_t consumeClockTicks() override { uint32_t n = pendingTicks; pendingTicks = 0; return n; }
};
