#include "MidiMonitorApp.h"

#include <cstdio>
#include <cstring>

#include "Display.h"

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
        }
    }
}

void MidiMonitorApp::onChannelKnob(int delta) {
    int next = static_cast<int>(channel_) + delta;
    if (next < 0)  next = 0;
    if (next > 16) next = 16;
    setChannel(static_cast<uint8_t>(next));
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
    // Everything else (CC, PC, PB, aftertouch) is intentionally ignored —
    // this view is note-only.
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
            if (w.bottomY < kRollTop) {
                w.live = false;
            }
        }
    }
}

void MidiMonitorApp::tick(uint32_t nowMs) {
    if (lastTickMs_ == 0) {
        lastTickMs_ = nowMs;
        return;
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

    char buf[16];
    if (channel_ == 0) std::snprintf(buf, sizeof(buf), "CH:OMNI");
    else               std::snprintf(buf, sizeof(buf), "CH:%u", channel_);
    d.drawText(4, 4, buf, color::White, color::DarkGray, 2);

    drawChordNames(d);
}

void MidiMonitorApp::drawChordNames(Display& d) const {
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

    // Right-align all names with a 4 px gutter. Drop the leftmost entries
    // if they would collide with the "CH:OMNI" / "CH:N" text on the left
    // (which ends around x = 4 + 7 chars * 12 = 88).
    constexpr int kLeftReserved = 92;
    int totalW = 0;
    for (int i = 0; i < count; ++i) {
        totalW += static_cast<int>(std::strlen(entries[i].name)) * kCharW;
    }
    totalW += (count - 1) * kSep;

    int firstShown = 0;
    while (firstShown < count - 1 &&
           kScreenW - totalW - 4 < kLeftReserved) {
        // Drop the oldest (lowest-channel) entry to make room.
        const int w = static_cast<int>(std::strlen(entries[firstShown].name))
                      * kCharW + kSep;
        totalW -= w;
        ++firstShown;
    }

    int x = kScreenW - totalW - 4;
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
    drawWorms(d);     // worms first; header draws on top in the y=0..20 strip
    drawHeader(d);
    drawKeyboard(d);
    d.present();
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
