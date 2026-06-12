#pragma once

#include <cstdint>

#include "core/BerlinSequence.h"
#include "core/BerlinTypes.h"
#include "core/BerlinRng.h"
#include "core/GateTimer.h"

namespace core {

class MidiOutput;
class Scale;
class SequenceGenerator;

// Single-voice Berlin sequencer. Holds a generated BerlinSequence, advances
// it on the 24-PPQN tick stream, and plays it out via MidiOutput. Transport
// (play/pause/stop) and (re)generation are driven by BerlinMode.
class BerlinEngine {
public:
    void setOutput(MidiOutput* o) { out_ = o; }
    void setScale(const Scale* s) { scale_ = s; }
    void setParams(const BerlinParams& p) { params_ = p; }
    void setOutChannel(uint8_t ch) { outChannel_ = ch; }
    void setGenerator(SequenceGenerator* g) { generator_ = g; }   // Task 4
    void seed(uint32_t v) { rng_.seed(v); }                        // tests

    // Mute suppresses NoteOns only; the engine keeps ticking so unmuting
    // re-enters in phase ("build up, then take away"). NoteOffs always pass,
    // and muting silences the currently sounding note immediately.
    void setMuted(bool m) {
        if (m && !muted_) silence();
        muted_ = m;
    }
    bool muted() const { return muted_; }

    // Transport.
    void play();    // run (emits the current step if starting from silence)
    void pause();   // hold playhead, stop advancing
    void silence(); // NoteOff a sounding note; playhead + playing state untouched
    void stop();    // rewind to step 0 + all-notes-off
    void generate();    // (re)generate using current params + Morph (Reset/Generate)

    // Live in-place sculpting. Each operation reads the already-pushed params_
    // and edits seq_ in place — only the touched parameter's effect is applied,
    // every untouched step stays byte-identical, and the playhead is NEVER reset
    // (length-shorten only wraps it). These never touch playing_, stepTicks_, or
    // the sounding gate. Used by BerlinMode when behavior == Live.
    void applyLiveDensity();                 // add/remove active steps to hit the new density target
    void applyLiveOctaveBase(int deltaSemis);// transpose active notes, fold into the current register
    void applyLiveOctaveRange(int oldRangeOctaves);  // proportionally stretch/squeeze the melody into the new register
    void applyLiveLength();                  // truncate (wrap playhead) or extend (fill the new tail)
    void applyLiveTension();                 // re-pitch active steps (except step 0), keep rhythm/velocity/gate

    // Replaces the whole sequence (preset load). The playhead is wrapped into
    // the new length; playing state and the sounding gate are untouched, so a
    // load mid-play swaps the pattern seamlessly.
    void setSequence(const BerlinSequence& s) {
        seq_ = s;
        if (playhead_ >= seq_.length()) playhead_ %= seq_.length();
    }

    void onClockTick();      // advance one 24-PPQN tick

    bool isPlaying()    const { return playing_; }
    int  playhead()     const { return playhead_; }
    int  loopCount()    const { return loopCount_; }
    int  soundingNote() const { return gate_.sounding() ? static_cast<int>(gate_.note()) : -1; }
    const BerlinSequence& sequence() const { return seq_; }
    BerlinSequence&       sequenceMut()    { return seq_; }   // tests set steps directly

private:
    void emit(bool isOn, uint8_t note, uint8_t velocity);
    void emitStep(int i);    // fire step i's NoteOn (if active), arm gate
    void evolve();           // splice 1-2 steps from a fresh candidate (Evolve behavior)
    BerlinStep freshLiveStep(uint8_t baseRoot);  // a freshly-rolled active step from current params_
    int  stepLenTicks() const { return berlinResolutionTicks(params_.resolution); }

    MidiOutput*        out_       = nullptr;
    const Scale*       scale_     = nullptr;
    SequenceGenerator* generator_ = nullptr;
    BerlinParams       params_{};
    BerlinRng          rng_{};
    uint8_t            outChannel_ = 1;

    BerlinSequence seq_{};
    bool  playing_   = false;
    bool  muted_     = false;
    int   playhead_  = 0;
    int   stepTicks_ = 0;
    int   loopCount_ = 0;    // completed loops since the last generate()/stop()

    GateTimer gate_{};
};

} // namespace core
