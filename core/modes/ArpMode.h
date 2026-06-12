#pragma once

#include "core/app/AppServices.h"
#include "core/app/Mode.h"
#include "core/render/ModeIcons.h"
#include "core/ArpEngine.h"
#include "core/ArpTypes.h"
#include "core/NoteWormModel.h"
#include "core/render/NotationRenderer.h"

namespace core {

class MidiOutput;

// ArpMode — arpeggiator with 4 screens:
//   0 "params1" — steps, rate, gate, direction
//   1 "params2" — octave, swing, velocity mode, latch
//   2 "worms"   — outgoing note visualisation
//   3 "notes"   — notation view of outgoing notes
class ArpMode : public Mode {
public:
    explicit ArpMode(AppServices& svc);

    const char* name() const override { return "Arp"; }
    const uint16_t* icon() const override { return icons::kArp; }
    int     screenCount() const override { return 4; }
    Screen& screen(int i) override;

    void onEnter() override;
    void onExit() override;
    void onMidiIn(const MidiMessage& msg) override;
    void onClockTick() override { engine_.onClockTick(); }
    // Stop and Pause silence the engine: under an external clock the gate-off
    // normally fired in onClockTick() never arrives once the upstream clock
    // stops. An arp has no meaningful Start/Continue, so Play is ignored.
    void onTransport(Transport t) override {
        if (t == Transport::Stop || t == Transport::Pause) engine_.stop();
    }
    void onRawInput(const RawInput& in) override;
    bool capturesTransport() const override { return true; }
    void update(uint32_t nowMs) override;

    // Attach the real MIDI output (called from Task 7 / tests).
    void setMidiOutput(MidiOutput* o) { engine_.setOutput(o); }

    // Test inspectors.
    const ArpParams& params() const { return params_; }
    bool hold()  const { return params_.latch; }
    bool muted() const { return engine_.muted(); }

private:
    // Echo thunk: forwards ArpEngine output events to model_.
    static void echoThunk(void* user, bool isOn,
                          uint8_t channel, uint8_t note, uint8_t velocity);

    // -----------------------------------------------------------------------
    // State — members must be declared before screen objects because the
    // screen constructors take references to them.
    // -----------------------------------------------------------------------
    AppServices&  svc_;
    ArpEngine     engine_;
    NoteWormModel model_;
    ArpParams     params_;

    // Latch edge-detection. The shell delivers the latch LEVEL every main-loop
    // frame, so Latch3 (Reset) must fire on the RISING edge only — otherwise it
    // zeroes step timing every frame and freezes the arp. latchSynced_ absorbs
    // the first delivery per index after onEnter() so entering with a switch ON
    // does not fire a phantom action.
    bool lastLatch_[4]   = {false, false, false, false};  // index 1..3
    bool latchSynced_[4] = {false, false, false, false};  // first-frame absorb

    // -----------------------------------------------------------------------
    // Screens (nested classes — declared after the members they reference).
    // -----------------------------------------------------------------------

    // Params screen A: steps / rate / gate / direction.
    class ParamScreenA : public Screen {
    public:
        explicit ParamScreenA(ArpMode& m) : mode_(m) {}
        const char* name() const override { return "params1"; }
        void onEncoder(int index, int delta) override;
        void render(Display& d) const override;
    private:
        ArpMode& mode_;
    };

    // Params screen B: octave / swing / velocity mode / latch.
    class ParamScreenB : public Screen {
    public:
        explicit ParamScreenB(ArpMode& m) : mode_(m) {}
        const char* name() const override { return "params2"; }
        void onEncoder(int index, int delta) override;
        void render(Display& d) const override;
    private:
        ArpMode& mode_;
    };

    // Worms visualisation of outgoing notes.
    class WormsScreen : public Screen {
    public:
        explicit WormsScreen(ArpMode& m) : mode_(m) {}
        const char* name() const override { return "worms"; }
        void render(Display& d) const override;
    private:
        ArpMode& mode_;
    };

    // Notation view of outgoing notes.
    class NotesScreen : public Screen {
    public:
        explicit NotesScreen(ArpMode& m) : mode_(m) {}
        const char* name() const override { return "notes"; }
        void update(uint32_t nowMs) override { notation_.update(mode_.model_, nowMs); }
        void render(Display& d) const override { notation_.render(mode_.model_, d); }
    private:
        ArpMode&         mode_;
        NotationRenderer notation_;
    };

    // Screen objects — constructed last (after svc_, engine_, model_, params_).
    ParamScreenA paramScreenA_{*this};
    ParamScreenB paramScreenB_{*this};
    WormsScreen  wormsScreen_{*this};
    NotesScreen  notesScreen_{*this};
};

} // namespace core
