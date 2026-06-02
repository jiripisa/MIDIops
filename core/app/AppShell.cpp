#include "core/app/AppShell.h"

#include <cstdio>

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
    if (overlayOpen_) return;                 // transport suppressed in overlay
    if (index < 1 || index > 3) return;
    if (on == lastLatchOn_[index]) return;    // act on any state change
    lastLatchOn_[index] = on;
    switch (index) {
        case 1:  // Play / Pause toggles
            applyTransport(transportState_ == TransportState::Playing
                               ? Transport::Pause : Transport::Play);
            break;
        case 2: applyTransport(Transport::Stop);  break;
        case 3: applyTransport(Transport::Reset); break;
    }
}

void AppShell::applyTransport(Transport t) {
    switch (t) {
        case Transport::Play:
            if (out_) {
                if (transportState_ == TransportState::Paused) out_->sendContinue();
                else out_->sendStart();
            }
            transportState_ = TransportState::Playing;
            break;
        case Transport::Pause:
            if (out_) out_->sendStop();
            transportState_ = TransportState::Paused;
            break;
        case Transport::Stop:
        case Transport::Reset:
            if (out_) out_->sendStop();
            transportState_ = TransportState::Stopped;
            break;
    }
    transport_ = t;
    if (modeCount_ > 0) modes_[activeMode_]->onTransport(t);
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

void AppShell::drawTopBar(Display& d) const {
    d.fillRect(0, 0, d.width(), 10, color::DarkGray);
    if (modeCount_ == 0) return;
    char line[48];
    const char* mode   = modes_[activeMode_]->name();
    const char* screen = modes_[activeMode_]->screen(screenIndex_).name();
    std::snprintf(line, sizeof(line), "%s  -  %s", mode, screen);
    d.drawText(2, 2, line, color::White, color::DarkGray, 1);
}

void AppShell::drawOverlay(Display& d) const {
    const int w = 200, h = 16 * modeCount_ + 8;
    const int x = (d.width() - w) / 2;
    const int y = (d.height() - h) / 2;
    d.fillRect(x, y, w, h, color::Black);
    d.fillRect(x, y, w, 12, color::Blue);
    d.drawText(x + 4, y + 2, "SELECT MODE", color::White, color::Blue, 1);
    for (int i = 0; i < modeCount_; ++i) {
        const bool sel = (i == overlayChoice_);
        const uint16_t bg = sel ? color::Yellow : color::Black;
        const uint16_t fg = sel ? color::Black : color::White;
        d.drawText(x + 8, y + 14 + i * 16, modes_[i]->name(), fg, bg, 1);
    }
}

void AppShell::render(Display& d) {
    d.clear(color::Black);
    if (overlayOpen_) {
        drawOverlay(d);
    } else if (modeCount_ > 0) {
        activeScreen().render(d);
    }
    drawTopBar(d);
    d.present();
}

} // namespace core
