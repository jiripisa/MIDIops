#include "core/app/AppShell.h"

#include <cstdio>

#include "core/Display.h"
#include "core/MidiOutput.h"

namespace core {

void AppShell::addMode(Mode* mode) {
    if (modeCount_ < kMaxModes) modes_[modeCount_++] = mode;
}

void AppShell::setMidiOutput(MidiOutput* o) { out_ = o; }

void AppShell::begin(int startMode) {
    activeMode_ = (startMode >= 0 && startMode < modeCount_) ? startMode : 0;
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
        if (index == 5 && modeCount_ > 0) {        // Enc5 moves selection
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
    if (index < 1 || index > 3) return;
    // On hardware the latch LEVEL is delivered every main-loop frame, so the
    // shell must (a) track the level FIRST and act second, (b) keep its shadow
    // in sync even when it does not act, and (c) absorb the first delivery for
    // an index so a physically-ON switch at boot — or held ON while leaving a
    // capturing mode — does not look like an edge and fire a phantom transport.
    const bool firstDelivery = !latchSeen_[index];
    const bool changed = on != lastLatchOn_[index];
    lastLatchOn_[index] = on;                 // shadow tracked unconditionally
    latchSeen_[index] = true;
    if (firstDelivery) return;                // absorb: record level only
    if (!changed) return;                     // act on a genuine level change
    if (overlayOpen_) return;                 // transport suppressed in overlay
    // Modes that capture transport (e.g. Arp uses the latches for hold/mute)
    // handle the latch via onRawInput; skip the shell's global transport.
    if (modeCount_ > 0 && modes_[activeMode_]->capturesTransport()) return;
    switch (index) {
        case 1:  // Play / Pause toggles
            applyTransport(transportState_ == TransportState::Playing
                               ? Transport::Pause : Transport::Play);
            break;
        case 2: applyTransport(Transport::Stop);  break;
        case 3: applyTransport(Transport::Reset); break;
    }
}

void AppShell::notifyLocalTransport(Transport t) {
    const bool send = out_ != nullptr && transportMode_ == TransportMode::Send;
    switch (t) {
        case Transport::Play:
            if (send) {
                if (transportState_ == TransportState::Paused) out_->sendContinue();
                else out_->sendStart();
            }
            transportState_ = TransportState::Playing;
            break;
        case Transport::Pause:
            if (send) out_->sendStop();
            transportState_ = TransportState::Paused;
            break;
        case Transport::Stop:
        case Transport::Reset:
            if (send) out_->sendStop();
            transportState_ = TransportState::Stopped;
            break;
    }
    transport_ = t;
}

void AppShell::applyTransport(Transport t) {
    notifyLocalTransport(t);
    if (modeCount_ > 0) modes_[activeMode_]->onTransport(t);
}

void AppShell::onMidiIn(const MidiMessage& msg) {
    // Realtime transport/clock handled first. Clock is followed under an
    // external source; transport (Start/Continue/Stop) is handled per the
    // Transport setting and is CONSUMED (never re-emitted). Realtime never
    // reaches a mode's note handler, so always return after this block.
    const bool isRealtime = msg.type == MidiType::Clock ||
                            msg.type == MidiType::Start ||
                            msg.type == MidiType::Continue ||
                            msg.type == MidiType::Stop;
    if (isRealtime) {
        if (msg.type == MidiType::Clock) {
            if (clockSource_ == ClockSource::External) {
                // Follow the external clock: derive BPM for display and advance
                // the active mode one tick per pulse.
                //
                // We deliberately do NOT echo the pulse back out. This device
                // has a single MIDI interface (usbMIDI), and when following an
                // external clock the host on that interface IS the master — so
                // forwarding its own clock straight back is useless, and at
                // 24 PPQN with a send_now() flush per pulse it floods usbMIDI TX
                // (nothing drains the device's MIDI-out), which starves usbMIDI
                // RX and stalls incoming note handling. So: follow, don't echo.
                //
                // nowMs_ is the last tick() timestamp (refreshed once per
                // main-loop iteration), used as the pulse's approximate arrival
                // time — fine for tempo follow over a 24-pulse window.
                clockFollower_.onPulse(nowMs_);
                uint16_t b = clockFollower_.bpm();
                if (b) bpm_ = b;                     // followed tempo updates display BPM
                if (modeCount_ > 0) modes_[activeMode_]->onClockTick();
            }
            return;
        }
        // Transport (Start/Continue/Stop). Receive drives playback per the
        // MIDI standard and CONSUMES the message (no re-emit — the same
        // no-echo rule as the clock). Otherwise incoming transport is
        // ignored, except the external-clock Stop safety below.
        if (transportMode_ == TransportMode::Receive) {
            if (modeCount_ > 0) {
                if (msg.type == MidiType::Start) {
                    modes_[activeMode_]->onTransport(Transport::Reset);   // rewind
                    modes_[activeMode_]->onTransport(Transport::Play);    // from the top
                } else if (msg.type == MidiType::Continue) {
                    modes_[activeMode_]->onTransport(Transport::Play);    // from position
                } else {
                    modes_[activeMode_]->onTransport(Transport::Pause);   // halt, keep position
                }
            }
            transportState_ = (msg.type == MidiType::Stop)
                                  ? TransportState::Paused
                                  : TransportState::Playing;
        } else if (msg.type == MidiType::Stop && clockSource_ == ClockSource::External) {
            // Safety (audit N6): a stopping master stops its clock too, so a
            // tick-scheduled gate-off would never fire. Silence immediately;
            // playback state is otherwise untouched — including transportState_
            // (the top bar): under Off/Send the device does not track received
            // transport, so the bar deliberately keeps showing the local state.
            if (modeCount_ > 0) modes_[activeMode_]->onTransport(Transport::Pause);
        }
        return;
    }
    // Global MIDI-in channel filter: when not OMNI, drop channel-voice
    // messages on other channels. Non-channel-voice (e.g. system) passes.
    if (midiInChannel_ != 0 && msg.isChannelVoice() &&
        msg.channel != midiInChannel_) {
        return;
    }
    if (modeCount_ > 0) modes_[activeMode_]->onMidiIn(msg);
}

void AppShell::setBpm(uint16_t bpm) {
    bpm_ = bpm;
    // Only drive the internal clock master while on the internal source;
    // following an external clock must not fight the derived tempo.
    if (out_ && clockSource_ == ClockSource::Internal) out_->setClockBpm(bpm_);
}

void AppShell::setClockSource(ClockSource s) {
    clockSource_ = s;
    if (out_) {
        if (s == ClockSource::External) {
            out_->setClockBpm(0);     // stop the internal clock master
            // When External is selected and no external clock is arriving, the
            // arp advances only on incoming pulses — a sounding note's gate-off
            // (fired in onClockTick) is deferred until clock resumes. Intended for v1.
            clockFollower_.reset();
        } else {
            out_->setClockBpm(bpm_);  // resume internal generation
        }
    }
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
    // Route internal clock ticks to the active mode (e.g. ArpMode → ArpEngine).
    if (clockSource_ == ClockSource::Internal && out_ && modeCount_ > 0) {
        uint32_t n = out_->consumeClockTicks();
        for (uint32_t i = 0; i < n; ++i) modes_[activeMode_]->onClockTick();
    }
}

void AppShell::drawTopBar(Display& d) const {
    d.fillRect(0, 0, d.width(), 10, color::DarkGray);
    if (modeCount_ == 0) return;
    char line[56];
    const char* mode   = modes_[activeMode_]->name();
    const char* screen = modes_[activeMode_]->screen(screenIndex_).name();
    const int   count  = modes_[activeMode_]->screenCount();
    std::snprintf(line, sizeof(line), "%s  -  %s (%d/%d)",
                  mode, screen, screenIndex_ + 1, count);
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
