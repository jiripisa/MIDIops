#include "core/BerlinEngine.h"

#include "core/MidiOutput.h"

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
    // Filled in Task 4. For now, leave the sequence as-is and rewind.
    playhead_  = 0;
    stepTicks_ = 0;
}

} // namespace core
