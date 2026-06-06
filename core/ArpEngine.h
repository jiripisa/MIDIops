#pragma once

#include <cstdint>

#include "core/ArpTypes.h"

namespace core {

class MidiOutput;
class Scale;

class ArpEngine {
public:
    using EchoFn = void(*)(void* user, bool isOn, uint8_t channel, uint8_t note, uint8_t velocity);

    void setOutput(MidiOutput* o) { out_ = o; }
    void setEcho(EchoFn fn, void* user) { echo_ = fn; echoUser_ = user; }
    void setBpm(uint16_t bpm) { bpm_ = bpm ? bpm : 120; }
    void setScale(const Scale* s) { scale_ = s; }
    void setParams(const ArpParams& p) { params_ = p; }
    void setOutChannel(uint8_t ch) { outChannel_ = ch; }

    void noteOn(uint8_t note, uint8_t velocity, uint32_t nowMs);
    void noteOff(uint8_t note, uint32_t nowMs);
    void tick(uint32_t nowMs);
    void stop();
    void reset();

    bool isPlaying() const { return active_; }

private:
    void     emit(bool isOn, uint8_t note, uint8_t velocity);
    uint32_t msPerStep() const;
    void     beginStep(uint32_t nowMs);   // emit current step note-on + schedule its note-off + next step time
    int      nextSeqIndex();              // advance per direction (no immediate repeat for Random)
    uint8_t  velocityForStep(int seqPos) const;

    MidiOutput*  out_ = nullptr;
    EchoFn       echo_ = nullptr; void* echoUser_ = nullptr;
    const Scale* scale_ = nullptr;
    ArpParams    params_{};
    uint16_t     bpm_ = 120;
    uint8_t      outChannel_ = 1;

    bool     active_ = false;
    uint8_t  rootVel_ = 100;
    uint8_t  seq_[16] = {};
    int      seqLen_ = 0;
    int      seqPos_ = 0;          // index into seq_ for the current step
    int      stepCount_ = 0;       // steps emitted since (re)start, for direction walk
    int8_t   udDir_ = +1;          // for UpDown/DownUp traversal
    uint32_t nextStepMs_ = 0;
    bool     noteSounding_ = false;
    uint8_t  soundingNote_ = 0;
    uint32_t noteOffMs_ = 0;
    uint32_t randState_ = 0x12345;
};

} // namespace core
