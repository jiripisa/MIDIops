#pragma once

#include "core/app/AppServices.h"
#include "core/app/Mode.h"
#include "core/BerlinEngine.h"
#include "core/BerlinTypes.h"
#include "core/DegreeWeightedGenerator.h"
#include "core/DrunkardWalkGenerator.h"
#include "core/GatePitchPhasingGenerator.h"
#include "core/Scale.h"

namespace core {

class MidiOutput;

// BerlinMode — single-voice generative Berlin-School sequencer.
// Screens: Structure / Character / Dynamics / Behavior. Each draws its top
// parameter row plus the shared bottom piano-roll (drawn every screen so the
// visualization persists across screen switches).
class BerlinMode : public Mode {
public:
    explicit BerlinMode(AppServices& svc);

    const char* name() const override { return "Berlin"; }
    int     screenCount() const override { return 4; }
    Screen& screen(int i) override;

    void onEnter() override;
    void onExit()  override { engine_.stop(); }   // silence any sounding note on leave
    void onClockTick() override { engine_.onClockTick(); }
    // Drives engine transport from the shell (Receive mapping + external-clock
    // Stop safety): Play → run, Pause → silence + hold position, Reset/Stop →
    // rewind + silence. Under an external clock the gate-off normally fired in
    // onClockTick() never arrives once the upstream clock stops, so Pause/Stop
    // silence the sounding note immediately.
    void onTransport(Transport t) override;
    void onRawInput(const RawInput& in) override;
    bool capturesTransport() const override { return true; }
    void update(uint32_t nowMs) override;

    void setMidiOutput(MidiOutput* o) { engine_.setOutput(o); }

    void liveRegen();   // regenerate immediately if Behavior == Live (structural edit)

    // Test inspectors.
    BerlinParams&         params()        { return params_; }
    const BerlinEngine&   engine() const  { return engine_; }

private:
    AppServices&          svc_;
    DrunkardWalkGenerator     walkGen_;
    DegreeWeightedGenerator   degreeGen_;
    GatePitchPhasingGenerator phasingGen_;
    BerlinEngine          engine_;
    BerlinParams          params_{};
    Scale                 scale_{};
    bool                  lastLatch_[4]   = {false, false, false, false};  // index 1..3 edge-detect
    bool                  latchSynced_[4] = {false, false, false, false};  // first-frame absorb after onEnter

    void applyGenerator();   // point the engine at the generator for params_.algorithm

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
    DynamicsScreen   dynamicsScreen_{*this};
    BehaviorScreen   behaviorScreen_{*this};
};

} // namespace core
