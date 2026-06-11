#include "RtMidiInput.h"

#include <RtMidi.h>

#include <cstddef>
#include <cstdio>
#include <utility>

namespace {
// Upper bound on the input queue depth (see callback notes).
constexpr std::size_t kMaxQueue = 1024;
}  // namespace

RtMidiInput::RtMidiInput(std::string portName)
    : portName_(std::move(portName)) {}

RtMidiInput::~RtMidiInput() {
    if (midi_) {
        // Cancel the callback before closing the port so an in-flight CoreMIDI
        // callback cannot race the dying queue_/mu_.
        try { midi_->cancelCallback(); } catch (...) {}
        try { midi_->closePort(); } catch (...) {}
    }
}

void RtMidiInput::begin() {
    try {
        midi_ = std::make_unique<RtMidiIn>(RtMidi::MACOSX_CORE,
                                           "MIDIops simulator");
        midi_->openVirtualPort(portName_);
        midi_->setCallback(&RtMidiInput::rtMidiCallback, this);
        // Pass through everything; we don't generate sysex/timing yet but a
        // future feature might want them.
        midi_->ignoreTypes(false, false, false);
        std::fprintf(stderr,
                     "[RtMidi] virtual input port opened: \"%s\"\n",
                     portName_.c_str());
    } catch (RtMidiError& e) {
        std::fprintf(stderr,
                     "[RtMidi] failed to open virtual port: %s\n",
                     e.getMessage().c_str());
        midi_.reset();
    }
}

bool RtMidiInput::poll(core::MidiMessage& out) {
    std::lock_guard<std::mutex> lock(mu_);
    if (queue_.empty()) return false;
    out = queue_.front();
    queue_.pop_front();
    return true;
}

void RtMidiInput::inject(const core::MidiMessage& msg) {
    std::lock_guard<std::mutex> lock(mu_);
    queue_.push_back(msg);
}

void RtMidiInput::rtMidiCallback(double /*timestamp*/,
                                 std::vector<unsigned char>* bytes,
                                 void* userdata) {
    auto* self = static_cast<RtMidiInput*>(userdata);
    if (!self || !bytes || bytes->empty()) return;

    const uint8_t status = (*bytes)[0];

    // Deliver realtime clock/transport messages before the channel-voice guard.
    {
        core::MidiType rtType = core::MidiType::Unknown;
        switch (status) {
            case 0xF8: rtType = core::MidiType::Clock;    break;
            case 0xFA: rtType = core::MidiType::Start;    break;
            case 0xFB: rtType = core::MidiType::Continue; break;
            case 0xFC: rtType = core::MidiType::Stop;     break;
            default: break;
        }
        if (rtType != core::MidiType::Unknown) {
            core::MidiMessage rtMsg;
            rtMsg.type = rtType;
            std::lock_guard<std::mutex> lock(self->mu_);
            // Bound the queue: only grows unbounded if the UI loop is blocked and
            // stops draining; dropping the oldest entry bounds memory (may drop
            // messages in that degenerate case) instead of growing without limit.
            if (self->queue_.size() >= kMaxQueue) self->queue_.pop_front();
            self->queue_.push_back(rtMsg);
            return;
        }
    }

    if (status < 0x80 || status >= 0xF0) return;  // ignore other system/sysex

    core::MidiMessage msg;
    msg.channel = static_cast<uint8_t>((status & 0x0F) + 1);

    switch (status & 0xF0) {
        case 0x80: msg.type = core::MidiType::NoteOff;           break;
        case 0x90: msg.type = core::MidiType::NoteOn;            break;
        case 0xA0: msg.type = core::MidiType::PolyAftertouch;    break;
        case 0xB0: msg.type = core::MidiType::ControlChange;     break;
        case 0xC0: msg.type = core::MidiType::ProgramChange;     break;
        case 0xD0: msg.type = core::MidiType::ChannelAftertouch; break;
        case 0xE0: msg.type = core::MidiType::PitchBend;         break;
        default:   return;
    }

    if (bytes->size() > 1) msg.data1 = static_cast<uint8_t>((*bytes)[1] & 0x7F);
    if (bytes->size() > 2) msg.data2 = static_cast<uint8_t>((*bytes)[2] & 0x7F);

    // A NoteOn with velocity 0 is, by MIDI spec, a NoteOff.
    if (msg.type == core::MidiType::NoteOn && msg.data2 == 0) {
        msg.type = core::MidiType::NoteOff;
    }

    {
        std::lock_guard<std::mutex> lock(self->mu_);
        // See the realtime branch above: bound the queue by dropping the oldest.
        if (self->queue_.size() >= kMaxQueue) self->queue_.pop_front();
        self->queue_.push_back(msg);
    }
}
