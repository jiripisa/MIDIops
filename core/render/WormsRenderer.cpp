#include "core/render/WormsRenderer.h"
#include "core/render/KeyLayout.h"
#include "core/render/Color.h"
#include "core/NoteWormModel.h"
#include "core/Display.h"

namespace core {
namespace WormsRenderer {

void drawWorms(const NoteWormModel& model, Display& d) {
    // Two passes:
    //   1. Input worms — per-channel-coloured fill with the existing
    //      split-on-overlap logic.
    //   2. Output (ghost) worms — gray rectangular OUTLINE only, drawn
    //      on top of the input fill so the engine's notes always read
    //      even when they share a column with an incoming note.
    constexpr uint16_t kBottomFactor = 256;
    constexpr uint16_t kTopFactor    = 160;

    // ---- Pass 1: input worms (per-channel split) -----------------------
    for (int i = 0; i < model.maxWorms(); ++i) {
        const NoteWormModel::Worm& w = model.worms()[i];
        if (!w.live || w.isOutput) continue;
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
            // Only count *input* worms in the slot split — output worms
            // are a separate ghost layer and shouldn't carve up the column.
            uint16_t chMask = 0;
            for (int j = 0; j < model.maxWorms(); ++j) {
                const NoteWormModel::Worm& o = model.worms()[j];
                if (!o.live || o.isOutput) continue;
                if (o.note != w.note) continue;
                if (y < o.topY || y > o.bottomY) continue;
                chMask |= static_cast<uint16_t>(1u << (o.channel - 1));
            }

            int slot = 0, total = 0;
            for (uint16_t m = chMask; m; m &= (m - 1)) ++total;
            for (uint16_t m = chMask & (myBit - 1); m; m &= (m - 1)) ++slot;
            if (total == 0) continue;

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

    // ---- Pass 2: output (ghost) worms — outline only -------------------
    for (int i = 0; i < model.maxWorms(); ++i) {
        const NoteWormModel::Worm& w = model.worms()[i];
        if (!w.live || !w.isOutput) continue;
        const KeyRect kr = keyRectFor(w.note);
        if (kr.x < 0) continue;

        int y0 = w.topY;
        int y1 = w.bottomY;
        if (y0 < kRollTop)     y0 = kRollTop;
        if (y1 >= kRollBottom) y1 = kRollBottom - 1;
        if (y1 < y0) continue;

        const uint16_t col   = color::Gray;
        const int      visH  = y1 - y0 + 1;

        // Two vertical sides — always visible while any of the worm is.
        d.fillRect(kr.x,               y0, 1, visH, col);
        d.fillRect(kr.x + kr.w - 1,    y0, 1, visH, col);

        // Top / bottom caps drawn only when the actual edge of the worm
        // sits inside the roll area; otherwise the ghost reads as an
        // open-ended box that has been clipped, which is correct.
        if (w.topY >= kRollTop && w.topY < kRollBottom) {
            d.fillRect(kr.x, w.topY, kr.w, 1, col);
        }
        if (w.bottomY >= kRollTop && w.bottomY < kRollBottom) {
            d.fillRect(kr.x, w.bottomY, kr.w, 1, col);
        }
    }
}

void drawKeyboard(const NoteWormModel& model, Display& d) {
    const int kbW = kWhiteKeysVisible * kWhiteKeyW;
    const int kbH = kKeyboardBot - kKeyboardTop;

    // 1) White-key surface.
    d.fillRect(kKeyboardX0, kKeyboardTop, kbW, kbH, color::White);

    // 2) Pressed white-key highlights (inside the separator lines).
    //    Output (engine-played) keys colour gray first; input keys then
    //    overdraw with their channel colour. When the same key is both
    //    received and played, the input colour wins.
    for (int wi = 0; wi < kWhiteKeysVisible; ++wi) {
        const uint8_t note    = whiteKeyAt(wi);
        const uint8_t inCh    = model.pressedChannelFor(note);
        const uint8_t outCh   = model.outPressedChannelFor(note);
        if (inCh == 0 && outCh == 0) continue;
        const int x = kKeyboardX0 + wi * kWhiteKeyW;
        const uint16_t fill = (inCh != 0) ? channelColor(inCh) : color::Gray;
        d.fillRect(x + 1, kKeyboardTop + 1,
                   kWhiteKeyW - 2, kbH - 2, fill);
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

        const uint8_t  inCh    = model.pressedChannelFor(note);
        const uint8_t  outCh   = model.outPressedChannelFor(note);
        const bool     pressed = (inCh != 0) || (outCh != 0);
        const uint16_t fill =
            (inCh != 0) ? channelColor(inCh) :
            (outCh != 0) ? color::Gray :
                           color::Black;
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

void render(const NoteWormModel& model, Display& d) {
    drawWorms(model, d);
    drawKeyboard(model, d);
}

} // namespace WormsRenderer
} // namespace core
