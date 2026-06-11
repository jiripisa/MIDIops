#include "core/BerlinEngine.h"

#include "core/BerlinGen.h"
#include "core/MidiOutput.h"
#include "core/Scale.h"
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

BerlinStep BerlinEngine::freshLiveStep(uint8_t baseRoot) {
    BerlinStep s;
    s.active    = true;
    s.note      = berlinDegreeWeightedNote(*scale_, baseRoot, params_, rng_);
    s.accent    = (s.note % 12 == scale_->root());
    s.velocity  = berlinFinalizeVelocity(params_, s.accent, rng_);
    s.gateTicks = static_cast<uint16_t>(berlinGateTicks(params_));
    return s;
}

void BerlinEngine::applyLiveDensity() {
    const int len = seq_.length();
    int target = (static_cast<int>(params_.density) * len + 50) / 100;
    if (target < 1) target = 1;               // step 0 anchor always survives
    if (target > len) target = len;

    int active = 0;
    for (int i = 0; i < len; ++i) if (seq_.step(i).active) ++active;

    if (active < target) {
        if (!scale_) return;                  // additions need the scale
        const uint8_t baseRoot = berlinBaseRoot(*scale_, params_);
        int cand[BerlinSequence::kMaxSteps];
        int n = 0;
        for (int i = 0; i < len; ++i) if (!seq_.step(i).active) cand[n++] = i;
        while (active < target && n > 0) {
            const int k = rng_.range(0, n - 1);
            const int idx = cand[k];
            cand[k] = cand[--n];              // remove the picked candidate
            seq_.step(idx) = freshLiveStep(baseRoot);
            ++active;
        }
    } else if (active > target) {
        int cand[BerlinSequence::kMaxSteps];
        int n = 0;
        for (int i = 1; i < len; ++i) if (seq_.step(i).active) cand[n++] = i;  // never step 0
        while (active > target && n > 0) {
            const int k = rng_.range(0, n - 1);
            const int idx = cand[k];
            cand[k] = cand[--n];
            seq_.step(idx).active = false;
            --active;
        }
    }
}

void BerlinEngine::applyLiveOctaveBase(int deltaSemis) {
    int lo = 0, hi = 0;
    berlinRegister(params_, lo, hi);
    const int len = seq_.length();
    for (int i = 0; i < len; ++i) {
        BerlinStep& s = seq_.step(i);
        if (!s.active) continue;
        int note = static_cast<int>(s.note) + deltaSemis;
        note = berlinFoldIntoRegister(note, lo, hi);
        if (note < 0)   note = 0;
        if (note > 127) note = 127;
        s.note = static_cast<uint8_t>(note);
    }
}

void BerlinEngine::applyLiveOctaveRange() {
    int lo = 0, hi = 0;
    berlinRegister(params_, lo, hi);
    const int len = seq_.length();
    for (int i = 0; i < len; ++i) {
        BerlinStep& s = seq_.step(i);
        if (!s.active) continue;
        int note = berlinFoldIntoRegister(static_cast<int>(s.note), lo, hi);
        if (note < 0)   note = 0;
        if (note > 127) note = 127;
        s.note = static_cast<uint8_t>(note);
    }
}

void BerlinEngine::applyLiveLength() {
    int target = params_.length;
    if (target < 1) target = 1;
    if (target > BerlinSequence::kMaxSteps) target = BerlinSequence::kMaxSteps;
    const int old = seq_.length();

    if (target < old) {
        seq_.setLength(target);
        if (playhead_ >= target) playhead_ %= target;   // wrap, do NOT reset
    } else if (target > old) {
        const uint8_t baseRoot = scale_ ? berlinBaseRoot(*scale_, params_) : 0;
        seq_.setLength(target);
        for (int i = old; i < target; ++i) {
            if (scale_ && rng_.chance(params_.density)) {
                seq_.step(i) = freshLiveStep(baseRoot);
            } else {
                seq_.step(i) = BerlinStep{};
            }
        }
    }
}

void BerlinEngine::applyLiveTension() {
    if (!scale_) return;
    const uint8_t baseRoot = berlinBaseRoot(*scale_, params_);
    const int len = seq_.length();
    for (int i = 1; i < len; ++i) {           // step 0 anchor untouched
        BerlinStep& s = seq_.step(i);
        if (!s.active) continue;
        s.note   = berlinDegreeWeightedNote(*scale_, baseRoot, params_, rng_);
        s.accent = (s.note % 12 == scale_->root());
        // velocity and gateTicks intentionally kept.
    }
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
