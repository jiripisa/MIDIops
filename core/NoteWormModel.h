#pragma once
#include <cstdint>
#include "core/MidiMessage.h"

namespace core {

class NoteWormModel {
public:
    static constexpr int      kMaxWorms       = 64;
    static constexpr uint32_t kScrollPxPerSec = 50;
    static constexpr int      kPostRollMargin = 200;

    struct Worm {
        bool     live    = false;
        bool     growing = false;
        bool     isOutput = false;
        uint8_t  note    = 0;
        uint8_t  channel = 0;
        int16_t  topY    = 0;
        int16_t  bottomY = 0;
        uint32_t startMs = 0;
        uint32_t endMs   = 0;
    };

    void onNoteOn (uint8_t channel, uint8_t note);
    void onNoteOff(uint8_t channel, uint8_t note);
    void onEngineNoteOn (uint8_t channel, uint8_t note);
    void onEngineNoteOff(uint8_t channel, uint8_t note);
    void releaseAllOnChannel(uint8_t channel);
    void clearInput();
    void tick(uint32_t nowMs);

    const Worm* worms()    const { return worms_; }
    int         maxWorms() const { return kMaxWorms; }

    uint8_t  pressedChannelFor(uint8_t note) const;
    uint8_t  outPressedChannelFor(uint8_t note) const;
    uint32_t lastTickMs() const { return lastTickMs_; }

private:
    void advanceWorms(int dy);
    int  spawnWorm(uint8_t channel, uint8_t note, bool isOutput, uint32_t nowMs);
    void stopWorm (uint8_t channel, uint8_t note, bool isOutput, uint32_t nowMs);

    uint16_t notePressedBy_[128]    = {};
    uint16_t outNotePressedBy_[128] = {};
    Worm     worms_[kMaxWorms]      = {};
    uint32_t lastTickMs_            = 0;
    uint32_t scrollAccumMs_         = 0;
    bool     started_               = false;
};

} // namespace core
