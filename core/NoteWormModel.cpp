#include "core/NoteWormModel.h"
#include "core/render/KeyLayout.h"  // noteVisible, kRollTop, kRollBottom

namespace core {

// ---------- bitmask helpers -----------------------------------------------

uint8_t NoteWormModel::pressedChannelFor(uint8_t note) const {
    if (note >= 128) return 0;
    uint16_t bits = notePressedBy_[note];
    if (bits == 0) return 0;
    uint8_t ch = 1;
    while ((bits & 1u) == 0) { bits >>= 1; ++ch; }
    return ch;
}

uint8_t NoteWormModel::outPressedChannelFor(uint8_t note) const {
    if (note >= 128) return 0;
    uint16_t bits = outNotePressedBy_[note];
    if (bits == 0) return 0;
    uint8_t ch = 1;
    while ((bits & 1u) == 0) { bits >>= 1; ++ch; }
    return ch;
}

// ---------- worm pool helpers ---------------------------------------------

int NoteWormModel::spawnWorm(uint8_t channel, uint8_t note,
                              bool isOutput, uint32_t nowMs) {
    for (int i = 0; i < kMaxWorms; ++i) {
        if (worms_[i].live) continue;
        Worm& w    = worms_[i];
        w.live     = true;
        w.growing  = true;
        w.isOutput = isOutput;
        w.note     = note;
        w.channel  = channel;
        w.topY     = static_cast<int16_t>(kRollBottom - 1);
        w.bottomY  = static_cast<int16_t>(kRollBottom - 1);
        w.startMs  = nowMs;
        w.endMs    = 0;
        return i;
    }
    return -1;  // pool exhausted
}

void NoteWormModel::stopWorm(uint8_t channel, uint8_t note,
                              bool isOutput, uint32_t nowMs) {
    for (int i = 0; i < kMaxWorms; ++i) {
        Worm& w = worms_[i];
        if (w.live && w.growing && w.isOutput == isOutput
                && w.note == note && w.channel == channel) {
            w.growing = false;
            w.endMs   = nowMs;
        }
    }
}

// ---------- public note events --------------------------------------------

void NoteWormModel::onNoteOn(uint8_t channel, uint8_t note) {
    if (!noteVisible(note)) return;
    if (channel < 1 || channel > 16) return;

    notePressedBy_[note] |= static_cast<uint16_t>(1u << (channel - 1));
    spawnWorm(channel, note, false, lastTickMs_);
}

void NoteWormModel::onNoteOff(uint8_t channel, uint8_t note) {
    if (!noteVisible(note)) return;
    if (channel < 1 || channel > 16) return;

    notePressedBy_[note] &= static_cast<uint16_t>(~(1u << (channel - 1)));
    stopWorm(channel, note, false, lastTickMs_);
}

void NoteWormModel::onEngineNoteOn(uint8_t channel, uint8_t note) {
    if (!noteVisible(note)) return;
    if (channel < 1 || channel > 16) return;

    outNotePressedBy_[note] |= static_cast<uint16_t>(1u << (channel - 1));
    spawnWorm(channel, note, true, lastTickMs_);
}

void NoteWormModel::onEngineNoteOff(uint8_t channel, uint8_t note) {
    if (!noteVisible(note)) return;
    if (channel < 1 || channel > 16) return;

    outNotePressedBy_[note] &= static_cast<uint16_t>(~(1u << (channel - 1)));
    stopWorm(channel, note, true, lastTickMs_);
}

void NoteWormModel::releaseAllOnChannel(uint8_t ch) {
    if (ch < 1 || ch > 16) return;
    const uint16_t chBit     = static_cast<uint16_t>(1u << (ch - 1));
    const uint16_t clearMask = static_cast<uint16_t>(~chBit);
    for (int n = 0; n < 128; ++n) {
        notePressedBy_[n] &= clearMask;
    }
    for (int i = 0; i < kMaxWorms; ++i) {
        Worm& w = worms_[i];
        if (w.live && w.growing && !w.isOutput && w.channel == ch) {
            w.growing = false;
            w.endMs   = lastTickMs_;
        }
    }
}

void NoteWormModel::clearInput() {
    for (int n = 0; n < 128; ++n) notePressedBy_[n] = 0;
    for (int i = 0; i < kMaxWorms; ++i) {
        Worm& w = worms_[i];
        if (w.live && w.growing && !w.isOutput) {
            w.growing = false;
            w.endMs   = lastTickMs_;
        }
    }
}

// ---------- animation -----------------------------------------------------

void NoteWormModel::advanceWorms(int dy) {
    for (int i = 0; i < kMaxWorms; ++i) {
        Worm& w = worms_[i];
        if (!w.live) continue;
        w.topY = static_cast<int16_t>(w.topY - dy);
        if (w.growing) {
            // Growing worm stays anchored at the keyboard; clip top so it
            // doesn't extend past the roll area while held forever.
            if (w.topY < kRollTop) w.topY = static_cast<int16_t>(kRollTop);
        } else {
            w.bottomY = static_cast<int16_t>(w.bottomY - dy);
            if (w.bottomY < kRollTop - kPostRollMargin) {
                w.live = false;
            }
        }
    }
}

void NoteWormModel::tick(uint32_t nowMs) {
    if (!started_) {
        lastTickMs_ = nowMs;
        started_    = true;
        return;
    }

    const uint32_t elapsed = nowMs - lastTickMs_;
    lastTickMs_ = nowMs;
    scrollAccumMs_ += elapsed;

    constexpr uint32_t kMsPerPixel = 1000u / kScrollPxPerSec;  // 20 ms/px
    int dy = 0;
    while (scrollAccumMs_ >= kMsPerPixel) {
        scrollAccumMs_ -= kMsPerPixel;
        if (++dy >= kRollBottom - kRollTop) {
            // Big stall — don't try to catch up.
            scrollAccumMs_ = 0;
            break;
        }
    }
    if (dy > 0) advanceWorms(dy);
}

} // namespace core
