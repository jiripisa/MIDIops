#pragma once

#include <cstdint>
#include <string>

namespace core {

enum class MidiType : uint8_t {
    Unknown            = 0x00,
    NoteOff            = 0x80,
    NoteOn             = 0x90,
    PolyAftertouch     = 0xA0,
    ControlChange      = 0xB0,
    ProgramChange      = 0xC0,
    ChannelAftertouch  = 0xD0,
    PitchBend          = 0xE0,
    // Realtime (single-byte, no channel, ignored by isChannelVoice())
    Clock              = 0xF8,
    Start              = 0xFA,
    Continue           = 0xFB,
    Stop               = 0xFC,
};

struct MidiMessage {
    MidiType type = MidiType::Unknown;
    uint8_t  channel = 1;   // 1..16
    uint8_t  data1   = 0;
    uint8_t  data2   = 0;

    bool isChannelVoice() const;

    // Short uppercase mnemonic for the message type (e.g. "NOTEON").
    static const char* typeName(MidiType t);

    // MIDI note number -> "C4", "F#3", etc. (Middle C = 60 = "C4").
    static std::string noteName(uint8_t note);

    // 14-bit signed pitch-bend value (-8192..+8191) reconstructed from data1/data2.
    int16_t pitchBendValue() const;
};

} // namespace core
