#pragma once

#include "core/app/AppServices.h"
#include "core/app/Mode.h"
#include "core/app/PresetScreen.h"
#include "core/render/ModeIcons.h"
#include "core/BassAnchorGenerator.h"
#include "core/BerlinEngine.h"
#include "core/BerlinTypes.h"
#include "core/DegreeWeightedGenerator.h"
#include "core/DrunkardWalkGenerator.h"
#include "core/GatePitchPhasingGenerator.h"
#include "core/Scale.h"

namespace core {

class MidiOutput;

// BerlinMode — multi-voice generative Berlin-School sequencer (Bass/Mid/High,
// each = engine + params + MIDI channel; the screens edit one voice at a time).
// Screens: Structure / Character / Voices / Dynamics / Behavior / Presets. The
// parameter screens draw their top parameter row plus the shared bottom
// piano-roll (drawn every screen so the visualization persists across screen
// switches); Voices is the mixer (per-voice channel + mute); Presets is the
// Save/Load/Delete slot picker.
class BerlinMode : public Mode, public PresetOps {
public:
    explicit BerlinMode(AppServices& svc);

    const char* name() const override { return "Berlin"; }
    const uint16_t* icon() const override { return icons::kBerlin; }
    int     screenCount() const override { return 6; }
    Screen& screen(int i) override;

    // PresetOps — BerlinParams + the realized sequence per slot, keys
    // "berlin.s01".."berlin.s20". A load mid-play swaps seamlessly: the
    // playhead keeps running, wrapped into the new length.
    bool presetUsed(int slot) override;
    bool savePreset(int slot) override;
    bool loadPreset(int slot) override;
    bool deletePresetSlot(int slot) override;

    void onEnter() override;
    void onExit()  override {              // silence every voice on leave
        for (int v = 0; v < kVoices; ++v) voices_[v].engine.stop();
    }
    void onClockTick() override {
        for (int v = 0; v < kVoices; ++v) voices_[v].engine.onClockTick();
    }
    // Drives engine transport from the shell (Receive mapping + external-clock
    // Stop safety): Play → run, Pause → silence + hold position, Reset/Stop →
    // rewind + silence. Under an external clock the gate-off normally fired in
    // onClockTick() never arrives once the upstream clock stops, so Pause/Stop
    // silence the sounding note immediately.
    void onTransport(Transport t) override;
    void onRawInput(const RawInput& in) override;
    bool capturesTransport() const override { return true; }
    void update(uint32_t nowMs) override;

    void setMidiOutput(MidiOutput* o) {
        for (int v = 0; v < kVoices; ++v) voices_[v].engine.setOutput(o);
    }

    static constexpr int kVoices = 3;
    enum VoiceId { kBass = 0, kMid = 1, kHigh = 2 };

    struct Voice {
        BerlinEngine engine;
        BerlinParams params;
        uint8_t      channel = 1;
    };

    // Live sculpting: when Behavior == Live, apply the edited parameter to the
    // existing sequence in place (no regeneration, playhead untouched).
    bool live() const { return voices_[editVoice_].params.behavior == BerlinBehavior::Live; }

    // Edit-voice accessors (no-arg = the voice the screens currently edit).
    int  editVoice() const { return editVoice_; }
    void setEditVoice(int v) { if (v >= 0 && v < kVoices) editVoice_ = v; }
    BerlinParams&         params()        { return voices_[editVoice_].params; }
    BerlinParams&         params(int v)   { return voices_[v].params; }
    const BerlinEngine&   engine() const  { return voices_[editVoice_].engine; }
    const BerlinEngine&   engine(int v) const { return voices_[v].engine; }
    uint8_t               voiceChannel(int v) const { return voices_[v].channel; }

private:
    AppServices&          svc_;
    DrunkardWalkGenerator     walkGen_;
    DegreeWeightedGenerator   degreeGen_;
    GatePitchPhasingGenerator phasingGen_;
    Voice                 voices_[kVoices];
    int                   editVoice_ = kHigh;
    BassAnchorGenerator   bassGen_;
    Scale                 scale_{};
    bool                  lastLatch_[4]   = {false, false, false, false};  // index 1..3 edge-detect
    bool                  latchSynced_[4] = {false, false, false, false};  // first-frame absorb after onEnter

    // Convenience for the screens: the voice being edited.
    BerlinParams& editParams() { return voices_[editVoice_].params; }
    BerlinEngine& editEngine() { return voices_[editVoice_].engine; }

    void applyGenerator(int v);   // point voice v's engine at its generator

    class StructureScreen : public Screen {
    public:
        explicit StructureScreen(BerlinMode& m) : mode_(m) {}
        const char* name() const override { return "structure"; }
        void onEncoder(int index, int delta) override;
        void render(Display& d) const override;
    private:
        BerlinMode& mode_;
    };

    class CharacterScreen : public Screen {
    public:
        explicit CharacterScreen(BerlinMode& m) : mode_(m) {}
        const char* name() const override { return "character"; }
        void onEncoder(int index, int delta) override;
        void render(Display& d) const override;
    private:
        BerlinMode& mode_;
    };

    // Mixer: one cell per voice — rotate sets the voice's MIDI channel,
    // press toggles its mute (the engine keeps running so unmuting re-enters
    // in phase). Enc4 unused.
    class VoicesScreen : public Screen {
    public:
        explicit VoicesScreen(BerlinMode& m) : mode_(m) {}
        const char* name() const override { return "voices"; }
        void onEncoder(int index, int delta) override;
        void onEncoderSw(int index) override;
        void render(Display& d) const override;
    private:
        BerlinMode& mode_;
    };

    class DynamicsScreen : public Screen {
    public:
        explicit DynamicsScreen(BerlinMode& m) : mode_(m) {}
        const char* name() const override { return "dynamics"; }
        void onEncoder(int index, int delta) override;
        void render(Display& d) const override;
    private:
        BerlinMode& mode_;
    };

    class BehaviorScreen : public Screen {
    public:
        explicit BehaviorScreen(BerlinMode& m) : mode_(m) {}
        const char* name() const override { return "behavior"; }
        void onEncoder(int index, int delta) override;
        void render(Display& d) const override;
    private:
        BerlinMode& mode_;
    };

    StructureScreen  structureScreen_{*this};
    CharacterScreen  characterScreen_{*this};
    VoicesScreen     voicesScreen_{*this};
    DynamicsScreen   dynamicsScreen_{*this};
    BehaviorScreen   behaviorScreen_{*this};
    PresetScreen     presetScreen_{*this};
};

} // namespace core
