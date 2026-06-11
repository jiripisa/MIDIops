#include "MidiMessage.h"

#include <cstdio>

namespace core {

bool MidiMessage::isChannelVoice() const {
    switch (type) {
        case MidiType::NoteOff:
        case MidiType::NoteOn:
        case MidiType::PolyAftertouch:
        case MidiType::ControlChange:
        case MidiType::ProgramChange:
        case MidiType::ChannelAftertouch:
        case MidiType::PitchBend:
            return true;
        default:
            return false;
    }
}

const char* MidiMessage::typeName(MidiType t) {
    switch (t) {
        case MidiType::NoteOff:           return "NOTEOFF";
        case MidiType::NoteOn:            return "NOTEON";
        case MidiType::PolyAftertouch:    return "POLYAT";
        case MidiType::ControlChange:    return "CC";
        case MidiType::ProgramChange:     return "PROG";
        case MidiType::ChannelAftertouch: return "CHAT";
        case MidiType::PitchBend:         return "BEND";
        default:                          return "?";
    }
}

std::string MidiMessage::noteName(uint8_t note) {
    static const char* const kNames[12] = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };
    const int n = note % 12;
    const int octave = static_cast<int>(note / 12) - 1; // MIDI 60 = C4
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%s%d", kNames[n], octave);
    return std::string(buf);
}

int16_t MidiMessage::pitchBendValue() const {
    const int raw = static_cast<int>(data1 & 0x7F) | (static_cast<int>(data2 & 0x7F) << 7);
    return static_cast<int16_t>(raw - 8192);
}

} // namespace core
