#include "core/BerlinEngine.h"

#include "core/MidiOutput.h"
#include "core/SequenceGenerator.h"

namespace core {

void BerlinEngine::emit(bool isOn, uint8_t note, uint8_t velocity) {
    if (!out_) return;
    if (isOn) out_->sendNoteOn(outChannel_, note, velocity);
    else      out_->sendNoteOff(outChannel_, note);
}

void BerlinEngine::emitStep(int i) {
    const BerlinStep& s = seq_.step(i);
    stepTicks_ = 0;
    if (!s.active) return;
    emit(true, s.note, s.velocity);
    noteSounding_ = true;
    soundingNote_ = s.note;
    gateTicks_    = s.gateTicks < 1 ? 1 : s.gateTicks;
    noteAge_      = 0;
}

void BerlinEngine::play() {
    if (playing_) return;
    playing_ = true;
    if (!noteSounding_) emitStep(playhead_);   // start promptly from silence
}

void BerlinEngine::pause() {
    playing_ = false;   // hold playhead; leave a sounding note to ring out
}

void BerlinEngine::stop() {
    playing_ = false;
    if (noteSounding_) { emit(false, soundingNote_, 0); noteSounding_ = false; }
    playhead_  = 0;
    stepTicks_ = 0;
    noteAge_   = 0;
}

void BerlinEngine::onClockTick() {
    if (noteSounding_) {
        ++noteAge_;
        if (noteAge_ >= gateTicks_) { emit(false, soundingNote_, 0); noteSounding_ = false; }
    }
    if (!playing_) return;
    ++stepTicks_;
    if (stepTicks_ >= stepLenTicks()) {
        playhead_ = (playhead_ + 1) % (seq_.length() < 1 ? 1 : seq_.length());
        emitStep(playhead_);
    }
}

void BerlinEngine::generate() {
    if (generator_ && scale_) {
        BerlinSequence cand;
        generator_->generate(cand, params_, *scale_, rng_);
        if (params_.morph >= 100) {
            seq_ = cand;                       // full regeneration
        } else {
            const int n = cand.length();
            for (int i = 0; i < n; ++i) {
                const bool replace = (i >= seq_.length()) || rng_.chance(params_.morph);
                if (replace) seq_.step(i) = cand.step(i);   // else keep the base step
            }
            seq_.setLength(n);
        }
    }
    if (noteSounding_) { emit(false, soundingNote_, 0); noteSounding_ = false; }
    playhead_  = 0;
    stepTicks_ = 0;
    noteAge_   = 0;
}

} // namespace core
