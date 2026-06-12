#pragma once

#include <cstdint>

#include "core/app/Mode.h"

namespace core {

// What a mode must provide to host a PresetScreen. The screen owns the whole
// Save/Load/Delete slot-picker flow; the mode only (de)serializes its state.
class PresetOps {
public:
    virtual ~PresetOps() = default;
    virtual bool presetUsed(int slot) = 0;
    virtual bool savePreset(int slot) = 0;        // overwrites a used slot
    virtual bool loadPreset(int slot) = 0;        // false = empty/corrupt
    virtual bool deletePresetSlot(int slot) = 0;
};

// Generic "presets" screen, shared by Arp and Berlin.
//
// Idle: Enc1 press = Save, Enc2 = Load, Enc3 = Delete — each opens the slot
// picker (a 5x4 grid of slots 01..20; used slots bright, empty dim, the
// selection framed). In the picker: rotating Enc1-4 moves the selection
// (wraps), pressing the SAME encoder confirms, pressing a different one
// cancels, 5 s without input cancels, leaving the screen cancels. The slot
// index is remembered across openings so save -> load round-trips stay put.
// Save/Load close the picker on success; Delete stays open for bulk cleanup;
// Load/Delete on an empty slot only flash EMPTY. Feedback shows ~1.5 s.
class PresetScreen : public Screen {
public:
    static constexpr int      kSlots     = 20;
    static constexpr uint32_t kTimeoutMs = 5000;
    static constexpr uint32_t kMsgMs     = 1500;

    enum class Action : uint8_t { None = 0, Save, Load, Del };

    explicit PresetScreen(PresetOps& ops) : ops_(ops) {}

    const char* name() const override { return "presets"; }
    void onExit() override { action_ = Action::None; }
    void onEncoder(int index, int delta) override;
    void onEncoderSw(int index) override;
    void update(uint32_t nowMs) override;
    void render(Display& d) const override;

    // Test inspectors.
    bool pickerOpen() const { return action_ != Action::None; }
    Action action() const { return action_; }
    int  slot() const { return slot_; }

private:
    void refreshUsed();
    void execute();
    void setMsg(const char* verb, int slotShown);

    PresetOps& ops_;
    Action     action_      = Action::None;
    int        invokingEnc_ = 0;
    int        slot_        = 0;          // 0..19, remembered across openings
    uint32_t   usedMask_    = 0;
    uint32_t   nowMs_       = 0;
    uint32_t   lastInputMs_ = 0;
    char       msg_[24]     = "";
    uint32_t   msgMs_       = 0;
};

} // namespace core
