#include "MidiMonitorApp.h"

#include <cstdio>
#include <cstring>

#include "Display.h"
#include "MidiOutput.h"
#include "notation_glyphs.h"

#ifndef JP4MIDI_VERSION
#define JP4MIDI_VERSION "dev"
#endif
#ifndef JP4MIDI_BUILD
#define JP4MIDI_BUILD ""
#endif

namespace core {

namespace {

// Pitch-class (0..11) -> index of the white key inside its octave, or -1 for
// black keys. C, D, E, F, G, A, B.
constexpr int8_t kWhiteIdxInOctave[12] = {
     0, -1,  1, -1,  2,
     3, -1,  4, -1,  5,
    -1,  6
};

// Pitch-class of each white key within an octave, indexed 0..6.
constexpr uint8_t kWhitePc[7] = {0, 2, 4, 5, 7, 9, 11};

// Multiplies an RGB565 colour by `factor256/256`. Used for vertical worm
// gradients (256 = full color, 160 ≈ 63 %).
uint16_t scaleRgb565(uint16_t c, uint16_t factor256) {
    uint32_t r = (c >> 11) & 0x1F;
    uint32_t g = (c >>  5) & 0x3F;
    uint32_t b =  c        & 0x1F;
    r = (r * factor256) >> 8;
    g = (g * factor256) >> 8;
    b = (b * factor256) >> 8;
    if (r > 0x1F) r = 0x1F;
    if (g > 0x3F) g = 0x3F;
    if (b > 0x1F) b = 0x1F;
    return static_cast<uint16_t>((r << 11) | (g << 5) | b);
}

// Pitch-class names. # uses the same glyph as in the font5x7 table.
constexpr const char* kPitchNames[12] = {
    "C", "C#", "D", "D#", "E", "F",
    "F#", "G", "G#", "A", "A#", "B"
};

// Chord patterns expressed as a 12-bit pitch-class set with the root at
// bit 0. The detector rotates the live pitch-class mask so the candidate
// root sits at bit 0, then looks for an exact match here.
struct ChordPattern { uint16_t mask; const char* suffix; };
constexpr ChordPattern kChordPatterns[] = {
    // ---- 4+ note chords (more specific, listed first) ----
    {0x895, "maj9"},   // 0,2,4,7,11
    {0x495, "9"},      // 0,2,4,7,10
    {0x48D, "m9"},     // 0,2,3,7,10
    {0x891, "maj7"},   // 0,4,7,11
    {0x489, "m7"},     // 0,3,7,10
    {0x491, "7"},      // 0,4,7,10
    {0x4A1, "7sus4"},  // 0,5,7,10
    {0x449, "m7b5"},   // 0,3,6,10
    {0x249, "dim7"},   // 0,3,6,9
    {0x291, "6"},      // 0,4,7,9
    {0x289, "m6"},     // 0,3,7,9
    // ---- 3 note triads ----
    {0x091, ""},       // 0,4,7    major
    {0x089, "m"},      // 0,3,7    minor
    {0x049, "dim"},    // 0,3,6
    {0x111, "aug"},    // 0,4,8
    {0x085, "sus2"},   // 0,2,7
    {0x0A1, "sus4"},   // 0,5,7
    {0x095, "add9"},   // 0,2,4,7
};

// Blit a 1-bpp glyph. Width up to 16 px; rows are packed into one uint16_t
// each, most-significant bit = leftmost pixel. Runs of consecutive set
// bits in a row are coalesced into a single fillRect call to keep the
// per-pixel rate down — important on Teensy where each fillRect dispatches
// through the framebuffer.
void drawGlyph(Display& d, int x, int y,
               const uint16_t* rows, int width, int height,
               uint16_t color) {
    for (int row = 0; row < height; ++row) {
        const uint16_t bits = rows[row];
        int col = 0;
        while (col < width) {
            const uint16_t mask =
                static_cast<uint16_t>(1u << (width - 1 - col));
            if (bits & mask) {
                int runEnd = col + 1;
                while (runEnd < width) {
                    const uint16_t m2 =
                        static_cast<uint16_t>(1u << (width - 1 - runEnd));
                    if (!(bits & m2)) break;
                    ++runEnd;
                }
                d.fillRect(x + col, y + row, runEnd - col, 1, color);
                col = runEnd;
            } else {
                ++col;
            }
        }
    }
}

int popCount12(uint16_t x) {
    int n = 0;
    while (x) { n += (x & 1); x >>= 1; }
    return n;
}

// Right-rotate the lower 12 bits of `m` by `by`.
uint16_t rotatePc(uint16_t m, int by) {
    by %= 12;
    if (by < 0) by += 12;
    return static_cast<uint16_t>(((m >> by) | (m << (12 - by))) & 0x0FFF);
}

// Bright per-channel palette (channels 1..16).
constexpr uint16_t kChannelPalette[16] = {
    rgb565( 80, 255,  80),   // 1  green
    rgb565(255, 200,  60),   // 2  yellow
    rgb565( 80, 200, 255),   // 3  sky
    rgb565(255, 100, 200),   // 4  magenta
    rgb565(255, 140,  60),   // 5  orange
    rgb565(180, 100, 255),   // 6  violet
    rgb565( 80, 255, 200),   // 7  teal
    rgb565(255,  80,  80),   // 8  red
    rgb565(200, 255,  80),   // 9  lime
    rgb565( 80, 140, 255),   // 10 blue
    rgb565(255, 180, 220),   // 11 pink
    rgb565(180, 255, 180),   // 12 pale green
    rgb565(220, 220,  80),   // 13 olive
    rgb565(180, 180, 255),   // 14 lavender
    rgb565(255, 220, 140),   // 15 peach
    rgb565(140, 220, 200),   // 16 aqua
};

} // namespace

// ---------- Static layout helpers ----------------------------------------

bool MidiMonitorApp::isBlackPc(int pc) {
    return kWhiteIdxInOctave[pc] < 0;
}

int MidiMonitorApp::whiteKeyIdx(uint8_t note) {
    const int octaveDiff = (note / 12) - (kLowestNote / 12);
    const int wi         = kWhiteIdxInOctave[note % 12];
    return wi + octaveDiff * 7;
}

uint8_t MidiMonitorApp::whiteKeyAt(int idx) {
    const int oct = idx / 7;
    const int pc  = kWhitePc[idx % 7];
    return static_cast<uint8_t>(kLowestNote + oct * 12 + pc);
}

MidiMonitorApp::KeyRect MidiMonitorApp::keyRectFor(uint8_t note) {
    if (!noteVisible(note)) return {-1, 0, false};
    const int pc = note % 12;
    if (isBlackPc(pc)) {
        const int leftWhiteIdx = whiteKeyIdx(static_cast<uint8_t>(note - 1));
        const int whiteRightEdge =
            kKeyboardX0 + (leftWhiteIdx + 1) * kWhiteKeyW;
        return { whiteRightEdge - kBlackKeyW / 2, kBlackKeyW, true };
    }
    const int wi = whiteKeyIdx(note);
    return { kKeyboardX0 + wi * kWhiteKeyW, kWhiteKeyW, false };
}

uint16_t MidiMonitorApp::channelColor(uint8_t channel) {
    if (channel < 1 || channel > 16) return color::White;
    return kChannelPalette[channel - 1];
}

// ---------- Lifecycle / input --------------------------------------------

MidiMonitorApp::MidiMonitorApp() = default;

void MidiMonitorApp::setChannel(uint8_t channel) {
    if (channel > 16) channel = 0;
    if (channel == channel_) return;
    channel_ = channel;
    // Keyboard highlights and chord names reflect "what's happening right
    // now" on the listened channel — clear them so the new channel starts
    // with a fresh view.
    for (int i = 0; i < 128; ++i) notePressedBy_[i] = 0;
    // Worms that were already on screen keep their natural trajectory.
    // Released worms continue scrolling toward the top until they leave
    // the roll area. Growing worms (whose source is still holding the
    // note) are flipped to released — their NoteOff would now be filtered
    // out, so we let them scroll away instead of growing forever.
    for (int i = 0; i < kMaxWorms; ++i) {
        if (worms_[i].live && worms_[i].growing) {
            worms_[i].growing = false;
            worms_[i].endMs   = lastTickMs_;
        }
    }
}

void MidiMonitorApp::onChannelKnob(int delta) {
    int next = static_cast<int>(channel_) + delta;
    if (next < 0)  next = 0;
    if (next > 16) next = 16;
    setChannel(static_cast<uint8_t>(next));
}

void MidiMonitorApp::setMidiOutput(MidiOutput* out) {
    midiOut_ = out;
    if (midiOut_) midiOut_->setClockBpm(bpm_);
}

void MidiMonitorApp::setBpm(uint16_t bpm) {
    if (bpm < kBpmMin) bpm = kBpmMin;
    if (bpm > kBpmMax) bpm = kBpmMax;
    if (bpm == bpm_) return;
    bpm_ = bpm;
    if (midiOut_) midiOut_->setClockBpm(bpm_);
}

void MidiMonitorApp::onBpmKnob(int delta) {
    int next = static_cast<int>(bpm_) + delta;
    if (next < kBpmMin) next = kBpmMin;
    if (next > kBpmMax) next = kBpmMax;
    setBpm(static_cast<uint16_t>(next));
}

void MidiMonitorApp::restart() {
    splashActive_  = true;
    splashStartMs_ = lastTickMs_;   // lastTickMs_ already holds "now"
    channel_       = kDefaultChannel;
    monitoring_    = true;
    for (int i = 0; i < 128; ++i) notePressedBy_[i] = 0;
    for (int i = 0; i < kMaxWorms; ++i) worms_[i].live = false;
    for (auto& sl : nameDisplays_) sl.live = false;
}

void MidiMonitorApp::toggleView() {
    const auto next = (static_cast<uint8_t>(view_) + 1) %
                      static_cast<uint8_t>(View::kCount);
    view_ = static_cast<View>(next);
}

void MidiMonitorApp::panic() {
    // Release every held note (all channels at once) and stop every
    // still-growing worm so they drift away naturally. Doesn't touch
    // monitoring / channel filter / BPM — this is just a "drop stuck
    // notes" recovery, not a full reset.
    for (int i = 0; i < 128; ++i) notePressedBy_[i] = 0;
    for (int i = 0; i < kMaxWorms; ++i) {
        if (worms_[i].live && worms_[i].growing) {
            worms_[i].growing = false;
            worms_[i].endMs   = lastTickMs_;
        }
    }
}

void MidiMonitorApp::releaseAllNotesOnChannel(uint8_t ch) {
    if (ch < 1 || ch > 16) return;
    const uint16_t chBit = static_cast<uint16_t>(1u << (ch - 1));
    const uint16_t clearMask = static_cast<uint16_t>(~chBit);
    for (int n = 0; n < 128; ++n) {
        notePressedBy_[n] &= clearMask;
    }
    for (int i = 0; i < kMaxWorms; ++i) {
        Worm& w = worms_[i];
        if (w.live && w.growing && w.channel == ch) {
            w.growing = false;
            w.endMs   = lastTickMs_;
        }
    }
}

void MidiMonitorApp::setMonitoring(bool on) {
    if (on == monitoring_) return;
    monitoring_ = on;
    if (!on) {
        // Clear all visible MIDI state so the panel reflects "muted" rather
        // than a frozen snapshot. Existing worms vanish, every key on the
        // keyboard goes back to unpressed.
        for (int i = 0; i < 128; ++i) notePressedBy_[i] = 0;
        for (int i = 0; i < kMaxWorms; ++i) worms_[i].live = false;
        for (auto& sl : nameDisplays_) sl.live = false;
    }
}

void MidiMonitorApp::onMessage(const MidiMessage& msg) {
    if (!monitoring_) return;
    if (msg.isChannelVoice() && channel_ != 0 && msg.channel != channel_) {
        return;
    }
    // NoteOn with velocity 0 is the running-status convention for NoteOff.
    if (msg.type == MidiType::NoteOn && msg.data2 == 0) {
        MidiMessage off = msg;
        off.type = MidiType::NoteOff;
        onNoteOff(off);
        return;
    }
    if (msg.type == MidiType::NoteOn)  { onNoteOn(msg);  return; }
    if (msg.type == MidiType::NoteOff) { onNoteOff(msg); return; }
    // Recognise the standard MIDI panic CCs. Ableton sends these on stop
    // and on some clip edits — releasing notes here cleans up after any
    // NoteOn whose NoteOff went missing.
    //   CC 120  All Sound Off
    //   CC 123  All Notes Off
    if (msg.type == MidiType::ControlChange &&
        (msg.data1 == 120 || msg.data1 == 123)) {
        releaseAllNotesOnChannel(msg.channel);
        return;
    }
    // Everything else (PC, PB, aftertouch, other CCs) is intentionally
    // ignored — this view is note-only.
}

void MidiMonitorApp::onNoteOn(const MidiMessage& msg) {
    if (!noteVisible(msg.data1)) return;
    if (msg.channel < 1 || msg.channel > 16) return;

    notePressedBy_[msg.data1] |=
        static_cast<uint16_t>(1u << (msg.channel - 1));

    // Spawn a worm. Reuse the first free slot; if the pool is full, the new
    // note simply doesn't get a worm (the keyboard still highlights).
    for (int i = 0; i < kMaxWorms; ++i) {
        if (worms_[i].live) continue;
        Worm& w   = worms_[i];
        w.live    = true;
        w.growing = true;
        w.note    = msg.data1;
        w.channel = msg.channel;
        w.topY    = static_cast<int16_t>(kRollBottom - 1);
        w.bottomY = static_cast<int16_t>(kRollBottom - 1);
        w.startMs = lastTickMs_;
        w.endMs   = 0;
        return;
    }
}

void MidiMonitorApp::onNoteOff(const MidiMessage& msg) {
    if (!noteVisible(msg.data1)) return;
    if (msg.channel < 1 || msg.channel > 16) return;

    notePressedBy_[msg.data1] &=
        static_cast<uint16_t>(~(1u << (msg.channel - 1)));

    // Stop growth on every matching worm. Normally one, but multi-channel
    // overlaps could leave more than one growing.
    for (int i = 0; i < kMaxWorms; ++i) {
        Worm& w = worms_[i];
        if (w.live && w.growing
            && w.note == msg.data1 && w.channel == msg.channel) {
            w.growing = false;
            w.endMs   = lastTickMs_;
        }
    }
}

uint8_t MidiMonitorApp::pressedChannelFor(uint8_t note) const {
    if (note >= 128) return 0;
    uint16_t bits = notePressedBy_[note];
    if (bits == 0) return 0;
    uint8_t ch = 1;
    while ((bits & 1u) == 0) {
        bits >>= 1;
        ++ch;
    }
    return ch;
}

// ---------- Animation ----------------------------------------------------

void MidiMonitorApp::advanceWorms(int dy) {
    // Keep released worms alive a while after they leave the worm-view
    // roll area so the notation view can keep scrolling their note-heads
    // toward the left edge of the staff. With the worm scroll at
    // 50 px/s and the notation scroll at 40 px/s, ~200 px of extra
    // post-roll travel ≈ 4 more seconds of life, putting the note's
    // total visible time around ~7 s — enough to reach the left side
    // of the staff in notation view before the slot is reused.
    constexpr int kPostRollMargin = 200;
    for (int i = 0; i < kMaxWorms; ++i) {
        Worm& w = worms_[i];
        if (!w.live) continue;
        w.topY = static_cast<int16_t>(w.topY - dy);
        if (w.growing) {
            // Growing worm stays anchored at the keyboard; clip its top so
            // it doesn't extend past the roll area while held forever.
            if (w.topY < kRollTop) w.topY = static_cast<int16_t>(kRollTop);
        } else {
            w.bottomY = static_cast<int16_t>(w.bottomY - dy);
            if (w.bottomY < kRollTop - kPostRollMargin) {
                w.live = false;
            }
        }
    }
}

void MidiMonitorApp::tick(uint32_t nowMs) {
    if (lastTickMs_ == 0) {
        lastTickMs_    = nowMs;
        splashStartMs_ = nowMs;
        return;
    }
    if (splashActive_ && (nowMs - splashStartMs_) >= kSplashDurationMs) {
        splashActive_ = false;
    }
    const uint32_t elapsed = nowMs - lastTickMs_;
    lastTickMs_ = nowMs;
    scrollAccumMs_ += elapsed;

    constexpr uint32_t kMsPerPixel = 1000u / kScrollPxPerSec;  // 20 ms / px
    int dy = 0;
    while (scrollAccumMs_ >= kMsPerPixel) {
        scrollAccumMs_ -= kMsPerPixel;
        if (++dy >= kRollBottom - kRollTop) {
            // Big stall (e.g. process backgrounded). Don't try to catch up.
            scrollAccumMs_ = 0;
            break;
        }
    }
    if (dy > 0) advanceWorms(dy);

    // MIDI Clock pulses are emitted by the MidiOutput's own timing source
    // (hardware timer on Teensy, dedicated thread on host) — see
    // setBpm() / setMidiOutput().
}

// ---------- Drawing ------------------------------------------------------

void MidiMonitorApp::drawHeader(Display& d) const {
    if (!monitoring_) {
        // Red bar makes the muted state unmistakable.
        d.fillRect(0, 0, kScreenW, kHeaderH, color::Red);
        d.drawText(4, 4, "MONITOR OFF", color::White, color::Red, 2);
        return;
    }

    d.fillRect(0, 0, kScreenW, kHeaderH, color::DarkGray);

    // ---- Channel filter (left) -----------------------------------------
    char chBuf[16];
    if (channel_ == 0) std::snprintf(chBuf, sizeof(chBuf), "CH:OMNI");
    else               std::snprintf(chBuf, sizeof(chBuf), "CH:%u", channel_);
    d.drawText(4, 4, chBuf, color::White, color::DarkGray, 2);

    // ---- BPM tempo (right) ---------------------------------------------
    constexpr int kCharW = 12;       // size-2 font glyph stride
    char bpmBuf[16];
    std::snprintf(bpmBuf, sizeof(bpmBuf), "%u BPM", bpm_);
    const int bpmTextW = static_cast<int>(std::strlen(bpmBuf)) * kCharW;
    const int bpmX     = kScreenW - bpmTextW - 4;
    d.drawText(bpmX, 4, bpmBuf, color::White, color::DarkGray, 2);

    // ---- Chord names occupy the middle, right-aligned to just before BPM
    drawChordNames(d, bpmX - 8);
}

void MidiMonitorApp::drawChordNames(Display& d, int rightEdge) const {
    // Size 2 text: 6 px per glyph stride * 2 = 12 px per char on screen.
    constexpr int kCharW = 12;
    constexpr int kSep   = 12;    // one blank char between adjacent names

    struct Entry {
        char    name[12];
        uint8_t channel;
    };
    Entry entries[16];
    int count = 0;

    for (uint8_t ch = 1; ch <= 16 && count < 16; ++ch) {
        char name[12];
        detectChordOnChannel(ch, name, sizeof(name));
        if (name[0] == '\0') continue;
        std::strncpy(entries[count].name, name, sizeof(entries[count].name));
        entries[count].name[sizeof(entries[count].name) - 1] = '\0';
        entries[count].channel = ch;
        ++count;
    }
    if (count == 0) return;

    // Right-align all names ending at `rightEdge`. Drop the leftmost
    // entries if they would collide with the "CH:OMNI" / "CH:N" text on
    // the left (which ends around x = 4 + 7 chars * 12 = 88).
    constexpr int kLeftReserved = 92;
    int totalW = 0;
    for (int i = 0; i < count; ++i) {
        totalW += static_cast<int>(std::strlen(entries[i].name)) * kCharW;
    }
    totalW += (count - 1) * kSep;

    int firstShown = 0;
    while (firstShown < count - 1 &&
           rightEdge - totalW < kLeftReserved) {
        // Drop the oldest (lowest-channel) entry to make room.
        const int w = static_cast<int>(std::strlen(entries[firstShown].name))
                      * kCharW + kSep;
        totalW -= w;
        ++firstShown;
    }

    int x = rightEdge - totalW;
    for (int i = firstShown; i < count; ++i) {
        d.drawText(x, 4, entries[i].name,
                   channelColor(entries[i].channel), color::DarkGray, 2);
        x += static_cast<int>(std::strlen(entries[i].name)) * kCharW;
        if (i < count - 1) x += kSep;
    }
}

void MidiMonitorApp::drawWorms(Display& d) const {
    // Per-row rendering. At each y we look up which channels currently have
    // a live worm covering (w.note, y) and split the key column into equal
    // sub-rects ordered left-to-right by channel number. With only one
    // channel active the row fills the full key width — identical to the
    // pre-split behaviour.
    constexpr uint16_t kBottomFactor = 256;
    constexpr uint16_t kTopFactor    = 160;

    for (int i = 0; i < kMaxWorms; ++i) {
        const Worm& w = worms_[i];
        if (!w.live) continue;
        const KeyRect kr = keyRectFor(w.note);
        if (kr.x < 0) continue;

        int y0 = w.topY;
        int y1 = w.bottomY;
        if (y0 < kRollTop)     y0 = kRollTop;
        if (y1 >= kRollBottom) y1 = kRollBottom - 1;
        if (y1 < y0) continue;

        const uint16_t base  = channelColor(w.channel);
        const int      fullH = w.bottomY - w.topY + 1;
        const int      denom = (fullH > 1) ? (fullH - 1) : 1;
        const uint16_t myBit = static_cast<uint16_t>(1u << (w.channel - 1));

        for (int y = y0; y <= y1; ++y) {
            // Bitmask of distinct channels with a live worm covering this
            // row on the same note. Same-channel duplicates collapse to one
            // bit so re-triggers don't split the column further.
            uint16_t chMask = 0;
            for (int j = 0; j < kMaxWorms; ++j) {
                const Worm& o = worms_[j];
                if (!o.live || o.note != w.note) continue;
                if (y < o.topY || y > o.bottomY) continue;
                chMask |= static_cast<uint16_t>(1u << (o.channel - 1));
            }

            // Total channels and this worm's slot (count of lower-numbered
            // channels also active in this row).
            int slot = 0, total = 0;
            for (uint16_t m = chMask; m; m &= (m - 1)) ++total;
            for (uint16_t m = chMask & (myBit - 1); m; m &= (m - 1)) ++slot;
            if (total == 0) continue;

            // Proportional slot split: each slot gets ceil(kr.w / total)-ish
            // pixels, with rounding distributed so the last slot fills the
            // remainder. Avoids 1 px gaps and keeps widths balanced.
            const int slotStart = (kr.w * slot) / total;
            const int slotEnd   = (kr.w * (slot + 1)) / total;
            const int subX      = kr.x + slotStart;
            const int subW      = slotEnd - slotStart;
            if (subW <= 0) continue;

            const int rowsFromBottom = w.bottomY - y;
            const uint16_t factor = static_cast<uint16_t>(
                kBottomFactor -
                ((kBottomFactor - kTopFactor) * rowsFromBottom) / denom);
            d.fillRect(subX, y, subW, 1, scaleRgb565(base, factor));
        }
    }
}

void MidiMonitorApp::drawKeyboard(Display& d) const {
    const int kbW = kWhiteKeysVisible * kWhiteKeyW;
    const int kbH = kKeyboardBot - kKeyboardTop;

    // 1) White-key surface.
    d.fillRect(kKeyboardX0, kKeyboardTop, kbW, kbH, color::White);

    // 2) Pressed white-key highlights (inside the separator lines). When
    //    multiple channels hold the same note, the lowest-numbered channel
    //    wins the colour — see pressedChannelFor().
    for (int wi = 0; wi < kWhiteKeysVisible; ++wi) {
        const uint8_t note = whiteKeyAt(wi);
        const uint8_t ch   = pressedChannelFor(note);
        if (ch) {
            const int x = kKeyboardX0 + wi * kWhiteKeyW;
            d.fillRect(x + 1, kKeyboardTop + 1,
                       kWhiteKeyW - 2, kbH - 2,
                       channelColor(ch));
        }
    }

    // 3) White-key separators and the keyboard's top edge.
    for (int wi = 0; wi <= kWhiteKeysVisible; ++wi) {
        int x = kKeyboardX0 + wi * kWhiteKeyW;
        if (wi == kWhiteKeysVisible) x -= 1;  // keep the rightmost line on-screen
        d.fillRect(x, kKeyboardTop, 1, kbH, color::Black);
    }
    d.fillRect(kKeyboardX0, kKeyboardTop, kbW, 1, color::Black);

    // 4) Black keys (drawn last so they sit on top of the white keys).
    for (uint8_t note = kLowestNote; note <= kHighestNote; ++note) {
        if (!isBlackPc(note % 12)) continue;
        const KeyRect kr = keyRectFor(note);
        if (kr.x < 0) continue;

        const uint8_t ch    = pressedChannelFor(note);
        const bool    pressed = ch != 0;
        const uint16_t fill = pressed ? channelColor(ch) : color::Black;
        d.fillRect(kr.x, kKeyboardTop, kr.w, kBlackKeyH, fill);

        if (pressed) {
            // Outline so the bright fill reads cleanly against neighbouring
            // unpressed black keys.
            d.fillRect(kr.x, kKeyboardTop, kr.w, 1, color::Black);
            d.fillRect(kr.x, kKeyboardTop + kBlackKeyH - 1, kr.w, 1, color::Black);
            d.fillRect(kr.x, kKeyboardTop, 1, kBlackKeyH, color::Black);
            d.fillRect(kr.x + kr.w - 1, kKeyboardTop, 1, kBlackKeyH, color::Black);
        }
    }
}

void MidiMonitorApp::render(Display& d) {
    d.clear(color::Black);
    if (splashActive_) {
        drawSplash(d);
    } else if (view_ == View::BigBpm) {
        drawBigBpm(d);
    } else if (view_ == View::Notation) {
        drawNotation(d);
    } else {
        drawWorms(d);     // worms first; header draws on top in the y=0..20 strip
        drawHeader(d);
        drawKeyboard(d);
    }
    d.present();
}

void MidiMonitorApp::drawSplash(Display& d) const {
    // Font: 5 px glyph + 1 px spacing = 6 px stride, 7 px tall.
    constexpr int kStride = 6;
    constexpr int kGlyphH = 7;

    // ---- Big "JP4Midi" title ---------------------------------------
    constexpr const char* kTitle = "JP4Midi";
    constexpr int kTitleSize = 4;
    const int titleLen = static_cast<int>(std::strlen(kTitle));
    const int titleW   = titleLen * kStride * kTitleSize;
    const int titleX   = (kScreenW - titleW) / 2;
    constexpr int kTitleY = 80;
    d.drawText(titleX, kTitleY, kTitle, color::White, color::Black, kTitleSize);

    // ---- Version + build timestamp on one line at size 2 -----------
    char meta[40];
    std::snprintf(meta, sizeof(meta), "%s %s",
                  JP4MIDI_VERSION, JP4MIDI_BUILD);
    constexpr int kMetaSize = 2;
    const int metaLen = static_cast<int>(std::strlen(meta));
    const int metaW   = metaLen * kStride * kMetaSize;
    const int metaX   = (kScreenW - metaW) / 2;
    constexpr int kMetaY = 150;
    d.drawText(metaX, kMetaY, meta, color::Gray, color::Black, kMetaSize);
    (void)kGlyphH;  // height not needed, layout is constant
}

void MidiMonitorApp::drawBigBpm(Display& d) const {
    // Font: 5 px glyph + 1 px spacing = 6 px stride, 7 px tall.
    constexpr int kStride = 6;
    constexpr int kGlyphH = 7;

    // ---- BPM number, centered, very large ---------------------------
    char num[8];
    std::snprintf(num, sizeof(num), "%u", bpm_);
    constexpr int kNumSize = 14;
    const int numLen = static_cast<int>(std::strlen(num));
    const int numW   = numLen * kStride * kNumSize;
    const int numH   = kGlyphH * kNumSize;
    const int numX   = (kScreenW - numW) / 2;
    constexpr int kNumY = 50;
    d.drawText(numX, kNumY, num, color::White, color::Black, kNumSize);

    // ---- "BPM" label below, smaller, gray ---------------------------
    constexpr const char* kLabel = "BPM";
    constexpr int kLabelSize = 3;
    const int labelLen = static_cast<int>(std::strlen(kLabel));
    const int labelW   = labelLen * kStride * kLabelSize;
    const int labelX   = (kScreenW - labelW) / 2;
    const int labelY   = kNumY + numH + 20;
    d.drawText(labelX, labelY, kLabel, color::Gray, color::Black, kLabelSize);
}

void MidiMonitorApp::drawNotation(Display& d) const {
    // ---- Grand-staff layout -------------------------------------------
    // One natural-pitch "step" (line ↔ adjacent space) is kStepH pixels.
    // Lines therefore sit at every other step, kStepH * 2 = 8 px apart.
    constexpr int kStepH    = 4;
    constexpr int kStaffX   = 26;
    constexpr int kStaffW   = 290;
    constexpr int kF5Y      = 60;    // top treble line
    constexpr int kE4Y      = kF5Y + 4 * 2 * kStepH;  // 92,  bottom treble
    constexpr int kC4Y      = kE4Y + 2 * kStepH;      // 100, middle C
    constexpr int kA3Y      = kE4Y + 4 * kStepH;      // 108, top bass
    constexpr int kG2Y      = kA3Y + 4 * 2 * kStepH;  // 140, bottom bass
    constexpr int kG4Y      = kE4Y - 2 * 2 * kStepH;  // 76,  G4 line (2nd treble)
    constexpr int kF3Y      = kA3Y + 2 * 2 * kStepH;  // 124, F3 line (4th bass)

    // Natural-pitch step indices (every odd number = a space, every even
    // = a line). MIDI 0 = C-1; one octave = 7 steps.
    constexpr int kF5Step = 38;
    constexpr int kC4Step = 28;
    constexpr int kG2Step = 18;

    auto stepY = [&](int step) {
        return kF5Y + (kF5Step - step) * kStepH;
    };

    // ---- Staff lines ---------------------------------------------------
    for (int i = 0; i < 5; ++i) {
        d.fillRect(kStaffX, kF5Y + i * 2 * kStepH, kStaffW, 1, color::White);
        d.fillRect(kStaffX, kA3Y + i * 2 * kStepH, kStaffW, 1, color::White);
    }
    d.fillRect(kStaffX - 1, kF5Y, 1, kG2Y - kF5Y + 1, color::White);

    // ---- Bitmap clefs --------------------------------------------------
    drawGlyph(d, 8,
              kG4Y - notation::kTrebleClefG4Row,
              notation::kTrebleClef,
              notation::kTrebleClefW, notation::kTrebleClefH,
              color::White);
    drawGlyph(d, 8,
              kF3Y - notation::kBassClefF3Row,
              notation::kBassClef,
              notation::kBassClefW, notation::kBassClefH,
              color::White);

    // ---- Rolling layout ------------------------------------------------
    // Notes spawn at the right edge ("now") when NoteOn arrives and walk
    // left at a constant scroll speed. Each note is a discrete musical
    // glyph (head + stem); duration is *not* drawn as a bar — that's
    // what the worm view is for. The result reads as "moving notes"
    // sliding across the staff.
    static const int8_t kPcToNatural[12] = {
        0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6
    };
    static const bool kPcIsSharp[12] = {
        false, true, false, true, false, false, true, false, true, false, true, false
    };

    constexpr int kNoteHeadW = notation::kNoteHeadW;
    constexpr int kNoteHeadH = notation::kNoteHeadH;
    constexpr int kLedgerW   = kNoteHeadW + 4;
    constexpr int kStemH     = 14;

    // Notes that are above the grand staff's vertical middle get a
    // stem pointing DOWN; notes below it get a stem pointing UP — the
    // standard convention so the stem always reaches toward the
    // centre of the staff system.
    constexpr int kStemBoundaryY = (kF5Y + kG2Y) / 2;

    constexpr int kScrollPxPerSec = 40;
    constexpr int kRightX = kStaffX + kStaffW - kNoteHeadW - 4;
    constexpr int kLeftX  = kStaffX + 4;

    const uint32_t now = lastTickMs_;

    for (int i = 0; i < kMaxWorms; ++i) {
        const Worm& w = worms_[i];
        if (!w.live) continue;
        if (!noteVisible(w.note)) continue;

        const uint32_t elapsed = now - w.startMs;
        const int headX = kRightX -
            static_cast<int>((elapsed * kScrollPxPerSec) / 1000u);
        // Hide the head a little before it would visually crowd the clef
        // glyph. kStaffX is the staff's left edge; cutting off there leaves
        // a small gap of staff width before the clef.
        if (headX < kStaffX) continue;  // scrolled off the left

        const int pc      = w.note % 12;
        const int octave  = w.note / 12 - 1;
        const int step    = octave * 7 + kPcToNatural[pc];
        const int y       = stepY(step);
        const bool sharp  = kPcIsSharp[pc];
        const uint16_t col = channelColor(w.channel);

        // Ledger lines for notes outside the staff. Drawn in white so they
        // read as part of the staff rather than the note.
        if (step > kF5Step) {
            const int last = (step % 2 == 0) ? step : (step - 1);
            for (int s = kF5Step + 2; s <= last; s += 2) {
                d.fillRect(headX - 2, stepY(s),
                           kLedgerW, 1, color::White);
            }
        } else if (step < kG2Step) {
            const int last = (step % 2 == 0) ? step : (step + 1);
            for (int s = kG2Step - 2; s >= last; s -= 2) {
                d.fillRect(headX - 2, stepY(s),
                           kLedgerW, 1, color::White);
            }
        } else if (step == kC4Step) {
            d.fillRect(headX - 2, kC4Y, kLedgerW, 1, color::White);
        }

        // Stem.
        const bool stemUp = (y > kStemBoundaryY);
        if (stemUp) {
            const int stemX = headX + kNoteHeadW - 1;
            const int stemY = y - kNoteHeadH / 2 - kStemH;
            d.fillRect(stemX, stemY, 1, kStemH, col);
        } else {
            const int stemX = headX;
            const int stemY = y + kNoteHeadH / 2 + 1;
            d.fillRect(stemX, stemY, 1, kStemH, col);
        }

        // Sharp accidental (drawn to the left of the head, channel-coloured).
        if (sharp) {
            const int sharpX = headX - notation::kSharpW - 1;
            const int sharpY = y - notation::kSharpH / 2;
            drawGlyph(d, sharpX, sharpY,
                      notation::kSharp,
                      notation::kSharpW, notation::kSharpH,
                      col);
        }

        // Oval note head.
        drawGlyph(d, headX, y - kNoteHeadH / 2,
                  notation::kNoteHead,
                  notation::kNoteHeadW, notation::kNoteHeadH,
                  col);
    }

    // ---- Channel filter label (top-left) ------------------------------
    char chBuf[16];
    if (channel_ == 0) std::snprintf(chBuf, sizeof(chBuf), "CH:OMNI");
    else               std::snprintf(chBuf, sizeof(chBuf), "CH:%u", channel_);
    d.drawText(4, 4, chBuf, color::White, color::Black, 2);

    // ---- Per-channel chord names (top, right-aligned) -----------------
    // Same shape as drawChordNames() for the monitor view, but freshly
    // laid out so the y-position and right-edge are independent of the
    // header bar.
    {
        constexpr int kCharW = 12;       // size-2 font glyph stride
        constexpr int kSep   = 12;
        constexpr int kRightEdge   = kScreenW - 4;
        constexpr int kLeftReserved = 100;  // keep clear of the "CH:" label

        struct Entry { char name[12]; uint8_t channel; };
        Entry entries[16];
        int count = 0;
        for (uint8_t ch = 1; ch <= 16 && count < 16; ++ch) {
            char name[12];
            detectChordOnChannel(ch, name, sizeof(name));
            if (name[0] == '\0') continue;
            std::strncpy(entries[count].name, name,
                         sizeof(entries[count].name));
            entries[count].name[sizeof(entries[count].name) - 1] = '\0';
            entries[count].channel = ch;
            ++count;
        }
        if (count > 0) {
            int totalW = 0;
            for (int i = 0; i < count; ++i)
                totalW += static_cast<int>(std::strlen(entries[i].name)) * kCharW;
            totalW += (count - 1) * kSep;

            int firstShown = 0;
            while (firstShown < count - 1 &&
                   kRightEdge - totalW < kLeftReserved) {
                const int w =
                    static_cast<int>(std::strlen(entries[firstShown].name))
                    * kCharW + kSep;
                totalW -= w;
                ++firstShown;
            }
            int x = kRightEdge - totalW;
            for (int i = firstShown; i < count; ++i) {
                d.drawText(x, 4, entries[i].name,
                           channelColor(entries[i].channel),
                           color::Black, 2);
                x += static_cast<int>(std::strlen(entries[i].name)) * kCharW;
                if (i < count - 1) x += kSep;
            }
        }
    }

    // ---- Held-note names (below the staff, with fall-away animation) -
    //
    // Each held note gets a name slot; when the note is released the
    // slot starts drifting straight down while its colour fades to
    // black, until either time or the screen bottom retires it.
    {
        constexpr int kCharW       = 12;        // size-2 font stride
        constexpr int kSep         = 6;
        constexpr int kNamesY      = kG2Y + 14; // baseline while held
        // Tuned so the fall reaches the screen bottom (Y=240) at roughly
        // the same moment the colour fades to black — names disappear
        // quickly enough that rapid re-presses don't pile up.
        constexpr int kFallPxPerS  = 60;
        constexpr uint32_t kFadeMs = 1500;

        // 1) Collect currently held notes and compute their centred X
        //    positions (the same layout the previous version produced).
        struct Held {
            uint8_t note;
            uint8_t channel;
            int     x;
            int     width;
        };
        Held held[kMaxNameDisplays];
        int  heldCount = 0;
        for (int n = 0; n < 128 && heldCount < kMaxNameDisplays; ++n) {
            if (notePressedBy_[n] == 0) continue;
            held[heldCount].note    = static_cast<uint8_t>(n);
            held[heldCount].channel = pressedChannelFor(static_cast<uint8_t>(n));
            const std::string s = MidiMessage::noteName(static_cast<uint8_t>(n));
            held[heldCount].width   = static_cast<int>(s.size()) * kCharW;
            ++heldCount;
        }
        if (heldCount > 0) {
            int totalW = 0;
            for (int i = 0; i < heldCount; ++i) totalW += held[i].width;
            totalW += (heldCount - 1) * kSep;
            int x = (kScreenW - totalW) / 2;
            if (x < 4) x = 4;
            for (int i = 0; i < heldCount; ++i) {
                held[i].x = x;
                x += held[i].width + kSep;
            }
        }

        // 2) Mark slots whose note is no longer pressed as released.
        //    They keep their last X and start their drift.
        for (auto& sl : nameDisplays_) {
            if (!sl.live) continue;
            if (sl.releasedMs != 0) continue;
            if (notePressedBy_[sl.note] == 0) {
                sl.releasedMs = lastTickMs_;
            }
        }

        // 3) For each currently held note, find an existing *still-held*
        //    slot for it (so we keep updating its X / channel), or
        //    allocate a fresh slot. Any falling slot with the same note
        //    is intentionally NOT reused — it keeps falling on its own
        //    while the freshly-pressed note appears at the baseline.
        for (int i = 0; i < heldCount; ++i) {
            int found = -1;
            for (int s = 0; s < kMaxNameDisplays; ++s) {
                if (nameDisplays_[s].live &&
                    nameDisplays_[s].releasedMs == 0 &&
                    nameDisplays_[s].note == held[i].note) {
                    found = s;
                    break;
                }
            }
            if (found < 0) {
                for (int s = 0; s < kMaxNameDisplays; ++s) {
                    if (!nameDisplays_[s].live) {
                        nameDisplays_[s].live = true;
                        nameDisplays_[s].note = held[i].note;
                        found = s;
                        break;
                    }
                }
            }
            if (found >= 0) {
                nameDisplays_[found].channel    = held[i].channel;
                nameDisplays_[found].x          = static_cast<int16_t>(held[i].x);
                nameDisplays_[found].releasedMs = 0;
            }
        }

        // 4) Render every live slot. Held slots stay at the baseline,
        //    released slots drift down with a linear fade.
        for (auto& sl : nameDisplays_) {
            if (!sl.live) continue;
            uint16_t col = channelColor(sl.channel);
            int y = kNamesY;
            if (sl.releasedMs != 0) {
                const uint32_t age = lastTickMs_ - sl.releasedMs;
                y += static_cast<int>((age * kFallPxPerS) / 1000u);
                if (age >= kFadeMs || y >= kScreenH) {
                    sl.live = false;
                    continue;
                }
                const uint32_t factor = ((kFadeMs - age) * 256u) / kFadeMs;
                col = scaleRgb565(col, static_cast<uint16_t>(factor));
            }
            const std::string name = MidiMessage::noteName(sl.note);
            d.drawText(sl.x, y, name.c_str(), col, color::Black, 2);
        }
    }
}

// ---------- Chord detection ---------------------------------------------

void MidiMonitorApp::detectChordOnChannel(uint8_t ch,
                                          char* out,
                                          std::size_t outSize) const {
    if (outSize == 0) return;
    out[0] = '\0';
    if (ch < 1 || ch > 16) return;

    const uint16_t chBit = static_cast<uint16_t>(1u << (ch - 1));
    uint16_t pcMask = 0;
    int      bassPc = -1;
    for (int n = 0; n < 128; ++n) {
        if (notePressedBy_[n] & chBit) {
            if (bassPc < 0) bassPc = n % 12;
            pcMask |= static_cast<uint16_t>(1u << (n % 12));
        }
    }
    if (popCount12(pcMask) < 3 || bassPc < 0) return;

    // 1) Prefer the bass note as the chord root — covers root-position
    //    voicings, which is what we'll match the vast majority of the time.
    {
        const uint16_t rotated = rotatePc(pcMask, bassPc);
        for (const auto& p : kChordPatterns) {
            if (rotated == p.mask) {
                std::snprintf(out, outSize, "%s%s",
                              kPitchNames[bassPc], p.suffix);
                return;
            }
        }
    }

    // 2) Otherwise look for an inversion: some other pitch class is the
    //    "real" root and the bass is just the lowest member.
    for (int root = 0; root < 12; ++root) {
        if (root == bassPc) continue;
        const uint16_t rotated = rotatePc(pcMask, root);
        for (const auto& p : kChordPatterns) {
            if (rotated == p.mask) {
                std::snprintf(out, outSize, "%s%s/%s",
                              kPitchNames[root], p.suffix,
                              kPitchNames[bassPc]);
                return;
            }
        }
    }

    // No match — leave `out` empty so the header doesn't print anything.
}

} // namespace core
