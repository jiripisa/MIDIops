#pragma once

#include <cstdint>

namespace core {

enum class ArpDirection : uint8_t { Up = 0, Down, UpDown, DownUp, Random, kCount };
enum class ArpRate : uint8_t { Quarter = 0, Eighth, EighthT, Sixteenth, SixteenthT, ThirtySecond, kCount };
enum class ArpVelocityMode : uint8_t { Fixed = 0, FollowInput, Accent, kCount };

// MIDI clock ticks (24 PPQN) per arpeggio step for each rate.
inline int arpRateTicks(ArpRate r) {
    switch (r) {
        case ArpRate::Quarter:      return 24;
        case ArpRate::Eighth:       return 12;
        case ArpRate::EighthT:      return 8;
        case ArpRate::Sixteenth:    return 6;
        case ArpRate::SixteenthT:   return 4;
        case ArpRate::ThirtySecond: return 3;
        default:                    return 6;
    }
}

struct ArpParams {
    uint8_t         steps         = 3;     // 1..16
    ArpRate         rate          = ArpRate::Sixteenth;
    uint8_t         gatePercent   = 80;    // 10..100
    ArpDirection    direction     = ArpDirection::Up;
    int8_t          octave        = 0;     // -2..+2
    uint8_t         swingPercent  = 50;    // 50..75
    ArpVelocityMode velocityMode  = ArpVelocityMode::Fixed;
    uint8_t         fixedVelocity = 100;   // 1..127
    bool            latch         = false;
};

} // namespace core
