#pragma once

#include "core/MidiOutput.h"

// Counts transport + clock calls for assertions.
struct FakeMidiOutput : core::MidiOutput {
    uint16_t lastBpm = 0;
    int starts = 0, continues = 0, stops = 0;

    void setClockBpm(uint16_t bpm) override { lastBpm = bpm; }
    void sendStart()    override { ++starts; }
    void sendContinue() override { ++continues; }
    void sendStop()     override { ++stops; }
    void sendNoteOn (uint8_t, uint8_t, uint8_t) override {}
    void sendNoteOff(uint8_t, uint8_t) override {}
};
