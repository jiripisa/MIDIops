#include "core/app/PresetScreen.h"

#include <cstdio>

#include "core/Display.h"

namespace core {

void PresetScreen::refreshUsed() {
    usedMask_ = 0;
    for (int i = 0; i < kSlots; ++i)
        if (ops_.presetUsed(i)) usedMask_ |= (1u << i);
}

void PresetScreen::setMsg(const char* verb, int slotShown) {
    if (slotShown >= 0)
        snprintf(msg_, sizeof msg_, "%s %02d", verb, slotShown + 1);
    else
        snprintf(msg_, sizeof msg_, "%s", verb);
    msgMs_ = nowMs_;
}

void PresetScreen::onEncoder(int index, int delta) {
    if (action_ == Action::None || index < 1 || index > 4 || delta == 0) return;
    slot_ = ((slot_ + delta) % kSlots + kSlots) % kSlots;
    lastInputMs_ = nowMs_;
}

void PresetScreen::onEncoderSw(int index) {
    if (action_ == Action::None) {
        if (index < 1 || index > 3) return;
        action_ = index == 1 ? Action::Save
                : index == 2 ? Action::Load
                             : Action::Del;
        invokingEnc_ = index;
        refreshUsed();
        lastInputMs_ = nowMs_;
        return;
    }
    if (index == invokingEnc_) {
        execute();
        lastInputMs_ = nowMs_;
        return;
    }
    if (index >= 1 && index <= 4) action_ = Action::None;   // cancel
}

void PresetScreen::execute() {
    const bool used = (usedMask_ >> slot_) & 1u;
    switch (action_) {
        case Action::Save:
            if (ops_.savePreset(slot_)) {
                setMsg("SAVED", slot_);
                action_ = Action::None;
            } else {
                setMsg("ERROR", -1);
            }
            break;
        case Action::Load:
            if (!used) {
                setMsg("EMPTY", slot_);            // stay open, pick another
            } else if (ops_.loadPreset(slot_)) {
                setMsg("LOADED", slot_);
                action_ = Action::None;
            } else {
                setMsg("ERROR", -1);
            }
            break;
        case Action::Del:
            if (!used) {
                setMsg("EMPTY", slot_);
            } else if (ops_.deletePresetSlot(slot_)) {
                setMsg("DELETED", slot_);
                refreshUsed();                     // stay open: bulk cleanup
            } else {
                setMsg("ERROR", -1);
            }
            break;
        case Action::None:
            break;
    }
}

void PresetScreen::update(uint32_t nowMs) {
    nowMs_ = nowMs;
    if (action_ != Action::None && nowMs_ - lastInputMs_ >= kTimeoutMs)
        action_ = Action::None;
    if (msg_[0] != '\0' && nowMs_ - msgMs_ >= kMsgMs)
        msg_[0] = '\0';
}

void PresetScreen::render(Display& d) const {
    if (action_ == Action::None) {
        // Idle: the three actions and which encoder press fires each.
        static const char* kNames[3]  = {"E1", "E2", "E3"};
        static const char* kValues[3] = {"SAVE", "LOAD", "DEL"};
        for (int i = 0; i < 3; ++i) {
            const int x = i * 106;
            d.drawText(x + 8, 40, kNames[i],  color::Gray,  color::Black, 1);
            d.drawText(x + 8, 58, kValues[i], color::White, color::Black, 2);
        }
        for (int i = 1; i < 3; ++i)
            d.fillRect(i * 106, 32, 1, 56, color::DarkGray);
        d.drawText(8, 110, "press an encoder, pick a slot,", color::Gray, color::Black, 1);
        d.drawText(8, 122, "press the same encoder to confirm", color::Gray, color::Black, 1);
    } else {
        const char* title = action_ == Action::Save ? "SAVE TO SLOT"
                          : action_ == Action::Load ? "LOAD SLOT"
                                                    : "DELETE SLOT";
        d.drawText(8, 16, title, color::Yellow, color::Black, 1);
        // 5x4 slot grid, slots 01..20.
        const int gx = 0, gy = 32, cw = 64, ch = 42;
        for (int i = 0; i < kSlots; ++i) {
            const int col = i % 5;
            const int row = i / 5;
            const int x = gx + col * cw;
            const int y = gy + row * ch;
            const bool used = (usedMask_ >> i) & 1u;
            char label[4];
            snprintf(label, sizeof label, "%02d", i + 1);
            d.drawText(x + 20, y + 14, label,
                       used ? color::White : rgb565(70, 70, 70),
                       color::Black, 2);
            if (i == slot_) {                      // selection frame
                d.fillRect(x + 2, y + 2, cw - 4, 1, color::Yellow);
                d.fillRect(x + 2, y + ch - 3, cw - 4, 1, color::Yellow);
                d.fillRect(x + 2, y + 2, 1, ch - 4, color::Yellow);
                d.fillRect(x + cw - 3, y + 2, 1, ch - 4, color::Yellow);
            }
        }
    }
    if (msg_[0] != '\0')
        d.drawText(8, 224, msg_, color::Yellow, color::Black, 1);
}

} // namespace core
