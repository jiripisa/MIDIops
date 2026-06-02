#include "core/render/NotationRenderer.h"

#include <cstdio>
#include <cstring>
#include <string>

#include "core/Display.h"
#include "core/MidiMessage.h"
#include "core/NoteWormModel.h"
#include "core/notation_glyphs.h"
#include "core/render/Color.h"
#include "core/render/Glyph.h"
#include "core/render/KeyLayout.h"

namespace core {

// ---- Grand-staff layout constants ----------------------------------------
// One natural-pitch "step" (line <-> adjacent space) is kStepH pixels.
// Lines therefore sit at every other step, kStepH * 2 = 8 px apart.
static constexpr int kStepH    = 4;
static constexpr int kStaffX   = 26;
static constexpr int kStaffW   = 290;
static constexpr int kF5Y      = 60;                        // top treble line
static constexpr int kE4Y      = kF5Y + 4 * 2 * kStepH;   // 92,  bottom treble
static constexpr int kC4Y      = kE4Y + 2 * kStepH;        // 100, middle C
static constexpr int kA3Y      = kE4Y + 4 * kStepH;        // 108, top bass
static constexpr int kG2Y      = kA3Y + 4 * 2 * kStepH;   // 140, bottom bass
static constexpr int kG4Y      = kE4Y - 2 * 2 * kStepH;   // 76,  G4 line (2nd treble)
static constexpr int kF3Y      = kA3Y + 2 * 2 * kStepH;   // 124, F3 line (4th bass)

// Natural-pitch step indices (every odd number = a space, every even = a line).
// MIDI 0 = C-1; one octave = 7 steps.
static constexpr int kF5Step = 38;
static constexpr int kC4Step = 28;
static constexpr int kG2Step = 18;

// Notes that are above the grand staff's vertical middle get a stem pointing
// DOWN; notes below it get a stem pointing UP.
static constexpr int kStemBoundaryY = (kF5Y + kG2Y) / 2;

static constexpr int kScrollPxPerSec = 40;
static constexpr int kRightX = kStaffX + kStaffW - notation::kNoteHeadW - 4;

// Held-note name display constants
static constexpr int      kCharW      = 12;      // size-2 font glyph stride
static constexpr int      kSep        = 6;
static constexpr int      kNamesY     = kG2Y + 14; // baseline while held
static constexpr int      kFallPxPerS = 60;
static constexpr uint32_t kFadeMs     = 1500;

static constexpr int kScreenH = 240;

// Maps pitch class to the natural-note index within an octave (sharps share
// their lower neighbour's natural).
static const int8_t kPcToNatural[12] = {
    0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6
};

// True if the pitch class is a sharp (black key).
static const bool kPcIsSharp[12] = {
    false, true, false, true, false, false, true, false, true, false, true, false
};

static inline int stepY(int step) {
    return kF5Y + (kF5Step - step) * kStepH;
}

// ---- update ---------------------------------------------------------------
// Mutates nameDisplays_ state: builds the held-note set, reconciles slots,
// stamps releasedMs when notes are released, expires stale slots.

void NotationRenderer::update(const NoteWormModel& model, uint32_t nowMs) {
    nowMs_ = nowMs;

    // 1) Collect currently held notes and compute their centred X positions.
    struct Held {
        uint8_t note;
        uint8_t channel;
        int     x;
        int     width;
    };
    Held held[kMaxNameDisplays];
    int  heldCount = 0;
    for (int n = 0; n < 128 && heldCount < kMaxNameDisplays; ++n) {
        const uint8_t ch = model.pressedChannelFor(static_cast<uint8_t>(n));
        if (ch == 0) continue;
        held[heldCount].note    = static_cast<uint8_t>(n);
        held[heldCount].channel = ch;
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
        if (model.pressedChannelFor(sl.note) == 0) {
            sl.releasedMs = nowMs_;
        }
    }

    // 3) For each currently held note, find an existing *still-held* slot
    //    for it (so we keep updating its X / channel), or allocate a fresh
    //    slot. Any falling slot with the same note is intentionally NOT
    //    reused — it keeps falling on its own while the freshly-pressed
    //    note appears at the baseline.
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

    // 4) Expire slots that have faded out or drifted off-screen.
    for (auto& sl : nameDisplays_) {
        if (!sl.live) continue;
        if (sl.releasedMs == 0) continue;
        const uint32_t age = nowMs_ - sl.releasedMs;
        const int y = kNamesY + static_cast<int>((age * kFallPxPerS) / 1000u);
        if (age >= kFadeMs || y >= kScreenH) {
            sl.live = false;
        }
    }
}

// ---- render ---------------------------------------------------------------
// Pure drawing: staff lines, clefs, rolling note-heads, and held-note names.
// Does not mutate any state.

void NotationRenderer::render(const NoteWormModel& model, Display& d) const {
    // ---- Staff lines -------------------------------------------------------
    for (int i = 0; i < 5; ++i) {
        d.fillRect(kStaffX, kF5Y + i * 2 * kStepH, kStaffW, 1, color::White);
        d.fillRect(kStaffX, kA3Y + i * 2 * kStepH, kStaffW, 1, color::White);
    }
    d.fillRect(kStaffX - 1, kF5Y, 1, kG2Y - kF5Y + 1, color::White);

    // ---- Bitmap clefs ------------------------------------------------------
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

    // ---- Rolling note-heads ------------------------------------------------
    // Notes spawn at the right edge ("now") when NoteOn arrives and walk
    // left at a constant scroll speed. Each note is a discrete musical
    // glyph (head + stem); duration is *not* drawn as a bar.
    const uint32_t now = model.lastTickMs();

    for (int i = 0; i < model.maxWorms(); ++i) {
        const NoteWormModel::Worm& w = model.worms()[i];
        if (!w.live) continue;
        if (!noteVisible(w.note)) continue;

        const uint32_t elapsed = now - w.startMs;
        const int headX = kRightX -
            static_cast<int>((elapsed * kScrollPxPerSec) / 1000u);
        if (headX < kStaffX) continue;  // scrolled off the left

        const int pc      = w.note % 12;
        const int octave  = w.note / 12 - 1;
        const int step    = octave * 7 + kPcToNatural[pc];
        const int y       = stepY(step);
        const bool sharp  = kPcIsSharp[pc];
        const uint16_t col = channelColor(w.channel);

        // Ledger lines for notes outside the staff. Drawn in white so they
        // read as part of the staff rather than the note.
        constexpr int kLedgerW = notation::kNoteHeadW + 4;
        if (step > kF5Step) {
            const int last = (step % 2 == 0) ? step : (step - 1);
            for (int s = kF5Step + 2; s <= last; s += 2) {
                d.fillRect(headX - 2, stepY(s), kLedgerW, 1, color::White);
            }
        } else if (step < kG2Step) {
            const int last = (step % 2 == 0) ? step : (step + 1);
            for (int s = kG2Step - 2; s >= last; s -= 2) {
                d.fillRect(headX - 2, stepY(s), kLedgerW, 1, color::White);
            }
        } else if (step == kC4Step) {
            d.fillRect(headX - 2, kC4Y, kLedgerW, 1, color::White);
        }

        // Stem.
        constexpr int kStemH = 14;
        const bool stemUp = (y > kStemBoundaryY);
        if (stemUp) {
            const int stemX = headX + notation::kNoteHeadW - 1;
            const int stemY = y - notation::kNoteHeadH / 2 - kStemH;
            d.fillRect(stemX, stemY, 1, kStemH, col);
        } else {
            const int stemX = headX;
            const int stemY = y + notation::kNoteHeadH / 2 + 1;
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
        drawGlyph(d, headX, y - notation::kNoteHeadH / 2,
                  notation::kNoteHead,
                  notation::kNoteHeadW, notation::kNoteHeadH,
                  col);
    }

    // ---- Held-note names (below the staff, with fall-away animation) -------
    //
    // Each held note gets a name slot; when the note is released the
    // slot starts drifting straight down while its colour fades to
    // black, until either time or the screen bottom retires it.
    for (const auto& sl : nameDisplays_) {
        if (!sl.live) continue;
        uint16_t col = channelColor(sl.channel);
        int y = kNamesY;
        if (sl.releasedMs != 0) {
            const uint32_t age = nowMs_ - sl.releasedMs;
            y += static_cast<int>((age * kFallPxPerS) / 1000u);
            if (age >= kFadeMs || y >= kScreenH) {
                continue;  // slot expired (will be freed next update)
            }
            const uint32_t factor = ((kFadeMs - age) * 256u) / kFadeMs;
            col = scaleRgb565(col, static_cast<uint16_t>(factor));
        }
        const std::string name = MidiMessage::noteName(sl.note);
        d.drawText(sl.x, y, name.c_str(), col, color::Black, 2);
    }
}

} // namespace core
