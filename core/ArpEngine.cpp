#include "core/ArpEngine.h"

#include <cstdint>

#include "core/ArpGenerator.h"
#include "core/MidiOutput.h"
#include "core/Scale.h"

namespace core {

// ---------------------------------------------------------------------------
// Timing
// ---------------------------------------------------------------------------

uint32_t ArpEngine::msPerStep() const {
    // ticks * (60000 ms/min) / (bpm * 24 ticks/beat)
    // Example: Quarter(24 ticks) at 120 BPM → 24*60000/(120*24) = 500 ms
    return static_cast<uint32_t>(arpRateTicks(params_.rate)) * 60000u / (bpm_ * 24u);
}

// ---------------------------------------------------------------------------
// Queue helpers
// ---------------------------------------------------------------------------

void ArpEngine::qPush(uint8_t note, uint8_t velocity) {
    if (qCount_ >= kQueueCap) return;  // drop new note if full
    int tail = (qHead_ + qCount_) % kQueueCap;
    queue_[tail] = { note, velocity };
    ++qCount_;
}

void ArpEngine::qPop() {
    if (qCount_ <= 0) return;
    qHead_ = (qHead_ + 1) % kQueueCap;
    --qCount_;
}

// ---------------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------------

void ArpEngine::noteOn(uint8_t note, uint8_t velocity, uint32_t nowMs) {
    if (!scale_) return;

    if (params_.latch) {
        if (!active_) {
            // Nothing playing — start fresh
            qCount_ = 0;
            qHead_  = 0;
            latchHasPending_ = false;
            cyclePending_    = false;
            qPush(note, velocity);
            initSeqFromHead();
            active_ = true;
            beginStep(nowMs);
        } else {
            // Active note is looping — store as pending replacement
            latchHasPending_  = true;
            latchPendingNote_ = note;
            latchPendingVel_  = velocity;
        }
        return;
    }

    // Normal (non-latch) mode
    if (!active_) {
        qCount_ = 0;
        qHead_  = 0;
        cyclePending_ = false;
        qPush(note, velocity);
        initSeqFromHead();
        active_ = true;
        beginStep(nowMs);
    } else {
        // Append to FIFO — never interrupt the active cycle
        qPush(note, velocity);
    }
}

void ArpEngine::noteOff(uint8_t /*note*/, uint32_t /*nowMs*/) {
    // One-shot model: noteOff is a no-op in both latch and non-latch modes.
    // Non-latch: each note plays exactly one cycle then auto-advances.
    // Latch: loops forever; replacement is triggered by a new noteOn.
}

void ArpEngine::tick(uint32_t nowMs) {
    // Fire NoteOff if gate has elapsed
    if (noteSounding_ && nowMs >= noteOffMs_) {
        emit(false, soundingNote_, 0);
        noteSounding_ = false;
    }

    // Advance to next step if scheduled
    if (active_ && nowMs >= nextStepMs_) {
        beginStep(nowMs);
    }
}

void ArpEngine::stop() {
    if (noteSounding_) {
        emit(false, soundingNote_, 0);
        noteSounding_ = false;
    }
    active_           = false;
    qCount_           = 0;
    qHead_            = 0;
    latchHasPending_  = false;
    activeCycleSteps_ = 0;
    cyclePending_     = false;
}

void ArpEngine::reset() {
    stepCount_        = 0;
    activeCycleSteps_ = 0;
    cyclePending_     = false;
    udDir_ = +1;
    if (params_.direction == ArpDirection::Down) {
        seqPos_ = seqLen_ - 1;
    } else if (params_.direction == ArpDirection::DownUp) {
        seqPos_ = seqLen_ - 1;
        udDir_  = -1;
    } else {
        seqPos_ = 0;
    }
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void ArpEngine::emit(bool isOn, uint8_t note, uint8_t velocity) {
    if (out_) {
        if (isOn) { if (!muted_) out_->sendNoteOn(outChannel_, note, velocity); }
        else      out_->sendNoteOff(outChannel_, note);
    }
    if (echo_) echo_(echoUser_, isOn, outChannel_, note, velocity);
}

void ArpEngine::initSeqFromHead() {
    if (qCount_ <= 0 || !scale_) return;

    uint8_t note = queue_[qHead_].note;
    rootVel_  = queue_[qHead_].velocity;
    seqLen_   = ArpGenerator::build(note, *scale_, params_, seq_, 16);
    if (seqLen_ <= 0) { seqLen_ = 0; return; }  // caller's beginStep guard sets active_=false

    stepCount_        = 0;
    activeCycleSteps_ = 0;
    udDir_ = +1;

    if (params_.direction == ArpDirection::Down) {
        seqPos_ = seqLen_ - 1;
    } else if (params_.direction == ArpDirection::DownUp) {
        seqPos_ = seqLen_ - 1;
        udDir_  = -1;
    } else {
        seqPos_ = 0;
    }
}


void ArpEngine::beginStep(uint32_t nowMs) {
    // --- 1. Kill the previous sounding note (its step has ended). ---
    if (noteSounding_) { emit(false, soundingNote_, 0); noteSounding_ = false; }

    // --- 2. If the previous step completed a cycle, resolve the boundary now —
    //        AFTER the last note has had its full step slot. ---
    if (cyclePending_) {
        cyclePending_ = false;
        if (!params_.latch) {
            qPop();                                         // remove the finished note
            if (qCount_ == 0) { active_ = false; return; } // queue empty → idle
            initSeqFromHead();                              // next queued note → step 0
        } else if (latchHasPending_) {
            latchHasPending_ = false;
            qCount_ = 0; qHead_ = 0;
            queue_[0] = { latchPendingNote_, latchPendingVel_ };
            qCount_   = 1;
            initSeqFromHead();
        }
        // latch with no pending → loop: seqPos_ already wrapped; fall through.
    }

    // --- 3. Guard. ---
    if (seqLen_ <= 0 || qCount_ == 0) { active_ = false; return; }

    // --- 4. Emit current step. ---
    uint8_t note = seq_[seqPos_];
    uint8_t vel  = velocityForStep(seqPos_);
    emit(true, note, vel);
    soundingNote_ = note;
    noteSounding_ = true;

    uint32_t stepMs = msPerStep();
    noteOffMs_  = nowMs + stepMs * params_.gatePercent / 100u;
    nextStepMs_ = nowMs + stepMs;

    // Swing: on odd steps shift the next-step boundary forward.
    if (params_.swingPercent != 50 && (stepCount_ & 1)) {
        int swing = (static_cast<int>(params_.swingPercent) - 50)
                    * static_cast<int>(stepMs) / 100;
        nextStepMs_ = static_cast<uint32_t>(static_cast<int>(nextStepMs_) + swing);
    }
    ++stepCount_;

    // --- 5. Advance seqPos_ for the next step. ---
    seqPos_ = nextSeqIndex();

    // --- 6. Track cycle completion — DEFER the boundary action to the next step. ---
    ++activeCycleSteps_;
    if (activeCycleSteps_ % seqLen_ == 0) {
        activeCycleSteps_ = 0;
        cyclePending_ = true;
    }
}

int ArpEngine::nextSeqIndex() {
    if (seqLen_ <= 0) return 0;

    switch (params_.direction) {
        case ArpDirection::Up:
            return (seqPos_ + 1) % seqLen_;

        case ArpDirection::Down:
            return (seqPos_ - 1 + seqLen_) % seqLen_;

        case ArpDirection::UpDown: {
            if (seqLen_ <= 1) return 0;
            // At top: flip to descending (don't repeat top endpoint)
            if (seqPos_ == seqLen_ - 1 && udDir_ == +1) {
                udDir_ = -1;
            }
            // At bottom: flip to ascending (don't repeat bottom endpoint)
            else if (seqPos_ == 0 && udDir_ == -1) {
                udDir_ = +1;
            }
            return seqPos_ + udDir_;
        }

        case ArpDirection::DownUp: {
            if (seqLen_ <= 1) return 0;
            // At bottom: flip to ascending
            if (seqPos_ == 0 && udDir_ == -1) {
                udDir_ = +1;
            }
            // At top: flip to descending
            else if (seqPos_ == seqLen_ - 1 && udDir_ == +1) {
                udDir_ = -1;
            }
            return seqPos_ + udDir_;
        }

        case ArpDirection::Random: {
            // LCG: no immediate repeat
            randState_ = randState_ * 1664525u + 1013904223u;
            int next = static_cast<int>(randState_ % static_cast<uint32_t>(seqLen_));
            if (seqLen_ > 1 && next == seqPos_) {
                next = (next + 1) % seqLen_;
            }
            return next;
        }

        default:
            return (seqPos_ + 1) % seqLen_;
    }
}

uint8_t ArpEngine::velocityForStep(int seqPos) const {
    switch (params_.velocityMode) {
        case ArpVelocityMode::Fixed:
            return params_.fixedVelocity;

        case ArpVelocityMode::FollowInput:
            return rootVel_;

        case ArpVelocityMode::Accent: {
            uint8_t base = params_.fixedVelocity;
            if (seqPos == 0) {
                int accented = static_cast<int>(base) + 30;
                return static_cast<uint8_t>(accented > 127 ? 127 : accented);
            }
            return base;
        }

        default:
            return params_.fixedVelocity;
    }
}

} // namespace core
