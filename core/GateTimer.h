#pragma once

#include <cstdint>

namespace core {

// Tracks one sounding note's gate. arm() on NoteOn; tick() returns true exactly
// once, when the gate elapses and the NoteOff should be sent; clear() on manual
// close. Shared by ArpEngine and BerlinEngine so both carry identical gate
// timing semantics.
class GateTimer {
public:
    void arm(uint8_t note, int gateTicks) {
        sounding_ = true;
        note_     = note;
        gate_     = gateTicks < 1 ? 1 : gateTicks;
        age_      = 0;
    }
    void clear() { sounding_ = false; }

    bool    sounding() const { return sounding_; }
    uint8_t note()     const { return note_; }

    // Advance one tick; returns true when the gate just elapsed (caller sends
    // NoteOff and we clear).
    bool tick() {
        if (!sounding_) return false;
        ++age_;
        if (age_ < gate_) return false;
        sounding_ = false;
        return true;
    }

private:
    bool    sounding_ = false;
    uint8_t note_     = 0;
    int     gate_     = 0;
    int     age_      = 0;
};

} // namespace core
