#include "core/app/AppShell.h"

#include "core/Display.h"
#include "core/MidiOutput.h"

namespace core {

void AppShell::addMode(Mode* mode) {
    if (modeCount_ < kMaxModes) modes_[modeCount_++] = mode;
}

void AppShell::setMidiOutput(MidiOutput* o) { out_ = o; }

void AppShell::begin() {
    activeMode_ = 0;
    screenIndex_ = 0;
    if (modeCount_ > 0) {
        modes_[activeMode_]->onEnter();
        activeScreen().onEnter();
    }
}

Screen& AppShell::activeScreen() {
    return modes_[activeMode_]->screen(screenIndex_);
}

void AppShell::fireRaw(const RawInput& in) {
    if (modeCount_ > 0) modes_[activeMode_]->onRawInput(in);
}

void AppShell::switchScreen(int delta) {
    if (modeCount_ == 0) return;
    const int n = modes_[activeMode_]->screenCount();
    if (n <= 1) return;
    activeScreen().onExit();
    screenIndex_ = ((screenIndex_ + delta) % n + n) % n;
    activeScreen().onEnter();
}

void AppShell::enterMode(int index) {
    if (index == activeMode_ || index < 0 || index >= modeCount_) return;
    activeScreen().onExit();
    modes_[activeMode_]->onExit();
    activeMode_ = index;
    screenIndex_ = 0;
    modes_[activeMode_]->onEnter();
    activeScreen().onEnter();
}

void AppShell::onEncoderKnob(int index, int delta) {
    fireRaw({RawInput::Kind::EncoderKnob, index, delta, false});
    if (overlayOpen_) {
        if (index == 2 && modeCount_ > 0) {        // Enc2 moves selection
            const int n = modeCount_;
            overlayChoice_ = ((overlayChoice_ + delta) % n + n) % n;
            overlayLastInputMs_ = nowMs_;
        }
        return;
    }
    if (index == 5) { switchScreen(delta); return; }
    if (index >= 1 && index <= 4) activeScreen().onEncoder(index, delta);
}

void AppShell::onEncoderSw(int index) {
    fireRaw({RawInput::Kind::EncoderSw, index, 0, false});
    if (overlayOpen_) {
        if (index == 5) {                 // confirm
            const int chosen = overlayChoice_;
            overlayOpen_ = false;
            enterMode(chosen);
        }
        return;                            // other SW ignored while overlay open
    }
    if (index == 5) {                      // open overlay
        overlayOpen_ = true;
        overlayChoice_ = activeMode_;
        overlayLastInputMs_ = nowMs_;
        return;
    }
    if (index >= 1 && index <= 4) activeScreen().onEncoderSw(index);
}

void AppShell::onLatch(int index, bool on) {
    fireRaw({RawInput::Kind::Latch, index, 0, on});
    // transport handling added in Task 5
}

void AppShell::onMidiIn(const MidiMessage& msg) {
    if (modeCount_ > 0) modes_[activeMode_]->onMidiIn(msg);
}

void AppShell::setBpm(uint16_t bpm) {
    bpm_ = bpm;
    if (out_) out_->setClockBpm(bpm_);
}

void AppShell::tick(uint32_t nowMs) {
    nowMs_ = nowMs;
    if (overlayOpen_ && (nowMs_ - overlayLastInputMs_) >= kOverlayTimeoutMs) {
        overlayOpen_ = false;              // revert: active mode never changed
    }
    if (modeCount_ > 0) {
        modes_[activeMode_]->update(nowMs);
        activeScreen().update(nowMs);
    }
}

// Stub — full implementation in a later task.
void AppShell::render(Display&) {}

// drawTopBar / drawOverlay / applyTransport implemented in later tasks.

} // namespace core
