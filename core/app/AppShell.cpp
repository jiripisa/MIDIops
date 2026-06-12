#include "core/app/AppShell.h"

#include <cstdio>
#include <cstring>

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
        overlayAnimPos256_ = overlayChoice_ * 256;   // tape parked on the choice
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
    // Frame delta for animations, clamped so a large jump between ticks
    // (startup, tests) advances at most one 50 ms animation step.
    uint32_t dt = nowMs - nowMs_;
    if (dt > 50) dt = 50;
    nowMs_ = nowMs;
    if (overlayOpen_ && (nowMs_ - overlayLastInputMs_) >= kOverlayTimeoutMs) {
        overlayOpen_ = false;              // revert: active mode never changed
    }
    if (overlayOpen_ && modeCount_ > 0 && dt > 0) {
        // Slide the carousel tape toward the choice along the shortest
        // wrapped path, exponentially eased (fast start, soft landing).
        const int span = modeCount_ * 256;
        int diff = ((overlayChoice_ * 256 - overlayAnimPos256_) % span + span) % span;
        if (diff > span / 2) diff -= span;
        if (diff != 0) {
            int step = diff * static_cast<int>(dt) / kOverlayAnimTauMs;
            if (step == 0) step = diff > 0 ? 1 : -1;   // integer-math floor: keep converging
            overlayAnimPos256_ = ((overlayAnimPos256_ + step) % span + span) % span;
        }
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
    // Horizontal mode carousel: the mode names sit on one animated row (a
    // "tape") across the middle of the screen and the centred frame is the
    // selection window the tape slides through. Text size steps down with
    // distance from the centre (3 / 2 / 1) and the colour darkens with it.
    if (modeCount_ == 0) return;
    constexpr int kPitch = 130;     // px between neighbouring tape positions
    const int n    = modeCount_;
    const int span = n * 256;
    const int cx   = d.width() / 2;
    const int cy   = d.height() / 2;

    d.drawText(cx - 33, cy - 44, "SELECT MODE", color::Gray, color::Black, 1);

    // The tape. Distances are in 1/256 index units around overlayAnimPos256_;
    // both backends clip, so names may slide partially off either edge.
    for (int i = 0; i < n; ++i) {
        int dist = ((i * 256 - overlayAnimPos256_) % span + span) % span;
        if (dist >= span / 2) dist -= span;
        const int adist = dist < 0 ? -dist : dist;
        const int size  = adist < 128 ? 3 : (adist < 384 ? 2 : 1);
        const uint16_t fg = adist < 128 ? color::White
                                        : (adist < 384 ? color::Gray
                                                       : color::DarkGray);
        const char* nm = modes_[i]->name();
        const int w = static_cast<int>(std::strlen(nm)) * 6 * size;
        const int x = cx + dist * kPitch / 256 - w / 2;
        if (x + w < 0 || x > d.width()) continue;
        d.drawText(x, cy - 4 * size, nm, fg, color::Black, size);
    }

    // Selection window, drawn over the tape. Its width follows the animation
    // by interpolating between the two names flanking the centre.
    const int i0   = (overlayAnimPos256_ / 256) % n;
    const int i1   = (i0 + 1) % n;
    const int frac = overlayAnimPos256_ % 256;
    const int w0   = static_cast<int>(std::strlen(modes_[i0]->name())) * 18 + 20;
    const int w1   = static_cast<int>(std::strlen(modes_[i1]->name())) * 18 + 20;
    const int fw   = w0 + (w1 - w0) * frac / 256;
    const int fx   = cx - fw / 2;
    const int fy   = cy - 17;
    const int fh   = 34;
    d.fillRect(fx, fy, fw, 1, color::Yellow);
    d.fillRect(fx, fy + fh - 1, fw, 1, color::Yellow);
    d.fillRect(fx, fy, 1, fh, color::Yellow);
    d.fillRect(fx + fw - 1, fy, 1, fh, color::Yellow);
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
