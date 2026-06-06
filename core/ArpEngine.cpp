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
// Public interface
// ---------------------------------------------------------------------------

void ArpEngine::noteOn(uint8_t note, uint8_t velocity, uint32_t nowMs) {
    if (!scale_) return;

    // Build ascending note sequence
    seqLen_ = ArpGenerator::build(note, *scale_, params_, seq_, 16);
    if (seqLen_ <= 0) return;

    active_    = true;
    rootVel_   = velocity;
    stepCount_ = 0;
    udDir_     = +1;

    // Direction=Down starts at the highest note
    if (params_.direction == ArpDirection::Down) {
        seqPos_ = seqLen_ - 1;
    } else if (params_.direction == ArpDirection::DownUp) {
        seqPos_ = seqLen_ - 1;
        udDir_  = -1;
    } else {
        seqPos_ = 0;
    }

    beginStep(nowMs);
}

void ArpEngine::noteOff(uint8_t /*note*/, uint32_t /*nowMs*/) {
    // Task 5 will handle latch/hold logic; for now single-note, just stop
    stop();
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
    active_ = false;
}

void ArpEngine::reset() {
    stepCount_ = 0;
    udDir_     = +1;
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
        if (isOn) out_->sendNoteOn(outChannel_, note, velocity);
        else      out_->sendNoteOff(outChannel_, note);
    }
    if (echo_) {
        echo_(echoUser_, isOn, outChannel_, note, velocity);
    }
}

void ArpEngine::beginStep(uint32_t nowMs) {
    // Kill any previous sounding note first
    if (noteSounding_) {
        emit(false, soundingNote_, 0);
        noteSounding_ = false;
    }

    if (seqLen_ <= 0) return;

    uint8_t note = seq_[seqPos_];
    uint8_t vel  = velocityForStep(seqPos_);

    emit(true, note, vel);
    soundingNote_  = note;
    noteSounding_  = true;

    uint32_t stepMs = msPerStep();
    noteOffMs_  = nowMs + stepMs * params_.gatePercent / 100u;
    nextStepMs_ = nowMs + stepMs;

    // Swing: on odd steps (stepCount_ is 0-based, so step 1, 3, 5...) shift
    // the next-step boundary forward.  swingPercent=50 means no swing;
    // values above 50 delay odd beats.
    if (params_.swingPercent != 50 && (stepCount_ & 1)) {
        int swing = (static_cast<int>(params_.swingPercent) - 50)
                    * static_cast<int>(stepMs) / 100;
        nextStepMs_ = static_cast<uint32_t>(static_cast<int>(nextStepMs_) + swing);
    }

    ++stepCount_;

    // Advance seqPos_ to the NEXT step
    seqPos_ = nextSeqIndex();
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
