#include "TeensyMidiInput.h"

#include <Arduino.h>
#include <usb_midi.h>

void TeensyMidiInput::begin() {
    // The global `usbMIDI` object is initialized by the Teensy core; nothing
    // to do here. We keep this method so callers can mirror the host setup.
}

bool TeensyMidiInput::poll(core::MidiMessage& out) {
    if (!usbMIDI.read()) return false;

    const uint8_t  rawType = usbMIDI.getType();
    const uint8_t  channel = usbMIDI.getChannel();   // 1..16
    const uint8_t  d1      = static_cast<uint8_t>(usbMIDI.getData1() & 0x7F);
    const uint8_t  d2      = static_cast<uint8_t>(usbMIDI.getData2() & 0x7F);

    core::MidiType type = core::MidiType::Unknown;
    switch (rawType) {
        case usbMIDI.NoteOff:           type = core::MidiType::NoteOff;           break;
        case usbMIDI.NoteOn:            type = core::MidiType::NoteOn;            break;
        case usbMIDI.AfterTouchPoly:    type = core::MidiType::PolyAftertouch;    break;
        case usbMIDI.ControlChange:     type = core::MidiType::ControlChange;     break;
        case usbMIDI.ProgramChange:     type = core::MidiType::ProgramChange;     break;
        case usbMIDI.AfterTouchChannel: type = core::MidiType::ChannelAftertouch; break;
        case usbMIDI.PitchBend:         type = core::MidiType::PitchBend;         break;
        case usbMIDI.Clock:             type = core::MidiType::Clock;             break;
        case usbMIDI.Start:             type = core::MidiType::Start;             break;
        case usbMIDI.Continue:          type = core::MidiType::Continue;          break;
        case usbMIDI.Stop:              type = core::MidiType::Stop;              break;
        default: return false;
    }

    out.type    = type;
    out.channel = channel;
    out.data1   = d1;
    out.data2   = d2;
    return true;
}
