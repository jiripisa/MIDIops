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
    if (gate_.sounding()) {
        emit(false, gate_.note(), 0);
        gate_.clear();
    }
    const BerlinStep& s = seq_.step(i);
    stepTicks_ = 0;
    if (!s.active) return;
    emit(true, s.note, s.velocity);
    // Gate is a LIVE performance parameter: derive it from the current params
    // (like ArpEngine does) instead of the gateTicks baked at generation time.
    // Turning the Gate knob is audible immediately in every behavior, and the
    // gate always matches the current resolution (no stale baked values). The
    // baked step gateTicks remain in the sequence for the piano-roll widths.
    const int gateTicks = stepLenTicks() * params_.gatePercent / 100;
    gate_.arm(s.note, gateTicks);   // arm() clamps to >= 1
}

void BerlinEngine::play() {
    if (playing_) return;
    playing_ = true;
    if (!gate_.sounding()) emitStep(playhead_);   // start promptly from silence
}

void BerlinEngine::pause() {
    playing_ = false;   // hold playhead; leave a sounding note to ring out
}

void BerlinEngine::silence() {
    if (!gate_.sounding()) return;
    emit(false, gate_.note(), 0);
    gate_.clear();
}

void BerlinEngine::stop() {
    playing_ = false;
    if (gate_.sounding()) { emit(false, gate_.note(), 0); gate_.clear(); }
    playhead_  = 0;
    stepTicks_ = 0;
    loopCount_ = 0;
}

void BerlinEngine::onClockTick() {
    if (gate_.sounding()) {
        const uint8_t n = gate_.note();
        if (gate_.tick()) emit(false, n, 0);
    }
    if (!playing_) return;
    ++stepTicks_;
    if (stepTicks_ >= stepLenTicks()) {
        const int len = seq_.length() < 1 ? 1 : seq_.length();
        int next = playhead_ + 1;
        if (next >= len) {
            next = 0;
            ++loopCount_;
            if (params_.behavior == BerlinBehavior::Evolve && generator_ && scale_
                && params_.evolveRate > 0 && (loopCount_ % params_.evolveRate) == 0) {
                evolve();
            }
        }
        playhead_ = next;
        emitStep(playhead_);
    }
}

void BerlinEngine::generate()     { regenerate(false); }   // Reset/Generate: honours Morph
void BerlinEngine::generateFull() { regenerate(true);  }   // Live: always a full fresh roll

void BerlinEngine::regenerate(bool full) {
    if (generator_ && scale_) {
        BerlinSequence cand;
        generator_->generate(cand, params_, *scale_, rng_);
        if (full || params_.morph >= 100) {
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
    if (gate_.sounding()) { emit(false, gate_.note(), 0); gate_.clear(); }
    playhead_  = 0;
    stepTicks_ = 0;
    loopCount_ = 0;
}

void BerlinEngine::evolve() {
    BerlinSequence cand;
    generator_->generate(cand, params_, *scale_, rng_);
    const int len = seq_.length();
    const int changes = 1 + rng_.range(0, 1);          // 1 or 2 steps
    for (int c = 0; c < changes; ++c) {
        const int idx = rng_.range(0, len - 1);
        if (idx >= 0 && idx < cand.length()) seq_.step(idx) = cand.step(idx);
    }
}

} // namespace core
