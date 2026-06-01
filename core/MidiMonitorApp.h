#pragma once

#include <cstddef>
#include <cstdint>

#include "ChordEngine.h"
#include "MidiMessage.h"

namespace core {

class Display;
class MidiOutput;

// Note-focused MIDI monitor: a piano keyboard at the bottom of the screen
// lights up under pressed notes, and "worms" (piano-roll bars) scroll upward
// from the keyboard while notes are held. Non-note channel-voice messages
// are accepted but not rendered.
class MidiMonitorApp {
public:
    // ============================================================
    //  Change this to listen on a different MIDI channel.
    //   0      = OMNI (accept all channels)
    //   1..16  = a single channel
    // ============================================================
    static constexpr uint8_t kDefaultChannel = 0;

    MidiMonitorApp();

    void    setChannel(uint8_t channel);
    uint8_t channel() const { return channel_; }

    // Increments / decrements the listened channel by `delta` detents.
    // Range is [0..16] where 0 = OMNI; values are clamped at both ends so
    // turning past the limits is a no-op. Clears the visible note/worm
    // state on actual change.
    void onChannelKnob(int delta);

    // Where to send outgoing real-time MIDI messages. The output owns its
    // own clock timing source — `setMidiOutput` immediately reconfigures
    // it for the current BPM so the MIDI Clock master starts running as
    // soon as the output is attached. Pass nullptr to detach.
    void setMidiOutput(MidiOutput* out);

    // Tempo in BPM for the MIDI Clock master. Clamped to [kBpmMin..kBpmMax].
    void     setBpm(uint16_t bpm);
    uint16_t bpm() const { return bpm_; }
    void     onBpmKnob(int delta);

    // Master monitoring switch. When OFF, incoming MIDI is ignored and the
    // held-note + worm state is cleared so the screen reflects "muted".
    void setMonitoring(bool on);
    void toggleMonitoring() { setMonitoring(!monitoring_); }
    bool monitoring() const { return monitoring_; }

    // Re-shows the boot splash and wipes the volatile MIDI state. Lets us
    // test the splash + first-render path without power-cycling the
    // device. Triggered by the encoder SW button on hardware and by F5
    // in the simulator.
    void restart();

    // "Panic" — releases every note that is currently shown as held,
    // including the still-growing worms. Useful when a sender (e.g.
    // Ableton editing a playing clip) sends a NoteOn but never the
    // matching NoteOff, leaving a note visually stuck. CC 120 / CC 123
    // from a DAW trigger this automatically; in the simulator the key
    // is Backspace.
    void panic();

    // Cycles between three views: monitor (header + worms + keyboard)
    // -> big-BPM focus -> notation (grand staff with held notes) ->
    // back to monitor. The BPM encoder's SW button drives this on
    // hardware; Tab in the simulator. BPM rotation still adjusts
    // tempo in every view.
    void toggleView();

    // ---- Mapping mode ------------------------------------------------
    //
    // The latching front-panel switch (DFR0789) drives this. While the
    // switch is ON we're in mapping mode: the display swaps to the
    // mapping editor, channel + BPM encoders re-purpose to edit chord
    // type and gate, channel-SW cycles direction, BPM-SW cycles through
    // existing mappings, and any incoming NoteOn captures the trigger
    // for the mapping being edited. All edits are auto-saved into the
    // chord engine as they happen. Flipping the switch OFF returns to
    // the previously selected view.
    void setMappingMode(bool on);
    bool mappingMode() const { return mappingMode_; }

    // Encoder-rotation entry points. In normal mode these update the
    // listened channel / BPM. In mapping mode they edit the current
    // mapping's parameters (chord type, gate ticks).
    // (The rotation methods are the same as before; the dispatch sits
    // inside the existing onChannelKnob / onBpmKnob.)

    // SW-button entry points.
    //   Channel SW — normal mode: restart the app.
    //                mapping mode: cycle the edit's chord direction.
    //   BPM SW     — normal mode: reserved (currently a no-op since
    //                the dedicated view encoder took over view
    //                cycling).
    //                mapping mode: browse to the next saved mapping.
    //   View SW    — currently a no-op in both modes; reserved for a
    //                future "home" / reset action.
    void onChannelSwPress();
    void onBpmSwPress();
    void onViewSwPress();

    // View-encoder rotation. Cycles views forward (+) or backward (-)
    // through Monitor → BigBpm → Notation. No-op in mapping mode (the
    // editor doesn't have a notion of "next view").
    void onViewKnob(int delta);
    uint8_t view() const { return static_cast<uint8_t>(view_); }

    void onMessage(const MidiMessage& msg);

    // Advances the scrolling animation. Caller passes a monotonic millisecond
    // clock (millis() on Teensy, SDL_GetTicks() on host). Safe to call as
    // often as you like — internal accumulator keeps a constant scroll speed.
    void tick(uint32_t nowMs);

    void render(Display& display);

private:
    // ---- Layout (320x240 logical pixels) -----------------------------
    static constexpr int kScreenW     = 320;
    static constexpr int kScreenH     = 240;
    static constexpr int kHeaderH     = 20;
    static constexpr int kRollTop     = 22;       // inclusive upper edge
    static constexpr int kRollBottom  = 180;      // exclusive lower edge
    static constexpr int kKeyboardTop = 180;
    static constexpr int kKeyboardBot = 240;
    static constexpr int kBlackKeyH   = 32;

    // Keyboard window. Must start on a C and end on a B.
    static constexpr uint8_t kLowestNote  = 36;   // C2
    static constexpr uint8_t kHighestNote = 83;   // B5
    static constexpr int     kWhiteKeysVisible = 28;          // 4 octaves
    static constexpr int     kWhiteKeyW = 11;                 // 28 * 11 = 308
    static constexpr int     kBlackKeyW = 7;
    static constexpr int     kKeyboardX0 =
        (kScreenW - kWhiteKeysVisible * kWhiteKeyW) / 2;

    // Scroll speed for the worm roll.
    static constexpr uint32_t kScrollPxPerSec = 50;

    // MIDI Clock tempo bounds and default.
    static constexpr uint16_t kBpmMin     = 30;
    static constexpr uint16_t kBpmMax     = 300;
    static constexpr uint16_t kBpmDefault = 120;

    // Boot splash: how long the "MIDIops" title sits on the screen before
    // the monitor view takes over.
    static constexpr uint32_t kSplashDurationMs = 3000;

    // ---- State -------------------------------------------------------
    struct Worm {
        bool     live     = false;
        bool     growing  = false;
        // True when the worm represents a note the chord engine is
        // playing (rather than a note arriving on the MIDI input).
        // Rendered in gray so it visually sits "behind" the input
        // worms.
        bool     isOutput = false;
        uint8_t  note     = 0;
        uint8_t  channel  = 0;
        int16_t  topY     = 0;   // smaller y == higher on screen
        int16_t  bottomY  = 0;
        // Wall-clock timestamps in milliseconds. Used by the notation
        // view to scroll note-heads horizontally and freeze the duration
        // bar's right edge when the note is released. `endMs` is only
        // meaningful when `growing` is false.
        uint32_t startMs  = 0;
        uint32_t endMs    = 0;
    };
    static constexpr int kMaxWorms = 64;

    struct KeyRect { int x; int w; bool isBlack; };

    uint8_t  channel_                = kDefaultChannel;
    bool     monitoring_             = true;

    // Per-note bitmap of which channels are currently holding it. Bit (ch-1)
    // is set when channel `ch` (1..16) is sustaining MIDI note `n`. This
    // tracks all 16 channels independently — required for per-channel chord
    // detection and for correct keyboard highlighting when more than one
    // channel plays the same note simultaneously.
    uint16_t notePressedBy_[128]     = {};

    // Same shape as notePressedBy_, but tracks notes the chord engine
    // itself is currently playing back through the MIDI output. Lets
    // the monitor view show "the device is playing X" alongside the
    // incoming notes, rendered in gray.
    uint16_t outNotePressedBy_[128]  = {};

    Worm     worms_[kMaxWorms]       {};
    uint32_t lastTickMs_             = 0;
    uint32_t scrollAccumMs_          = 0;
    uint32_t splashStartMs_          = 0;
    bool     splashActive_           = true;

    // ---- Notation-view note-name overlays --------------------------
    //
    // Each currently-pressed note in the notation view has its name
    // (e.g. "C#4") drawn just below the staff. When the note is
    // released the slot stays alive, its X freezes at wherever it was
    // last shown, and the name drifts straight down while its colour
    // fades to black. Animation is computed each render() from the
    // wall-clock time stored on the slot — no state is mutated in any
    // const path, so the field is marked `mutable` to keep the existing
    // drawNotation() signature.
    struct NameDisplay {
        bool     live       = false;
        uint8_t  note       = 0;
        uint8_t  channel    = 0;
        int16_t  x          = 0;       // X anchored at release time
        uint32_t releasedMs = 0;       // 0 = still held
    };
    static constexpr int kMaxNameDisplays = 32;
    mutable NameDisplay nameDisplays_[kMaxNameDisplays]{};

public:
    enum class View : uint8_t {
        Monitor  = 0,
        BigBpm   = 1,
        Notation = 2,
        Debug    = 3,
        kCount   = 4,
    };
private:
    View view_ = View::Monitor;

    MidiOutput* midiOut_             = nullptr;
    uint16_t    bpm_                 = kBpmDefault;

    // Chord engine: trigger-driven chord / arpeggio player. Listens to
    // every incoming NoteOn and emits scheduled note events through the
    // attached MidiOutput.
    ChordEngine chordEngine_;

    // ---- Debug view state -------------------------------------------
    //
    // Per-control activity counters surfaced by the debug view. Updated
    // at the top of each on*Knob() / on*SwPress() / setMappingMode()
    // entry point so the view shows real input even when the dispatched
    // action is a no-op in the current mode.
    struct DebugKnob {
        int32_t  total        = 0;   // running sum of detents since boot
        int8_t   lastDelta    = 0;   // sign of the most recent rotation
        uint32_t lastChangeMs = 0;   // 0 = "never moved"
    };
    struct DebugButton {
        uint16_t pressCount   = 0;   // monotonic per-boot
        uint32_t lastChangeMs = 0;   // 0 = "never pressed"
    };
    DebugKnob   dbgChannelKnob_{};
    DebugKnob   dbgBpmKnob_{};
    DebugKnob   dbgViewKnob_{};
    DebugButton dbgPanelSwitch_{};
    DebugButton dbgChannelSw_{};
    DebugButton dbgBpmSw_{};
    DebugButton dbgViewSw_{};

    // ---- Mapping mode state -----------------------------------------
    //
    // While `mappingMode_` is true the chord engine's normal trigger
    // path is suspended (NoteOns capture the trigger instead of firing
    // a chord). `editMapping_` is the live working copy of the mapping
    // currently being edited; `editIndex_` is its index inside the
    // chord engine (-1 means "no mapping captured yet — waiting for a
    // trigger note"). All knob/SW changes mutate editMapping_ and call
    // chordEngine_.updateMapping() immediately, so there is no
    // explicit save action — flipping the panel switch OFF just exits
    // the editor.
    bool                 mappingMode_ = false;
    ChordEngine::Mapping editMapping_{};
    int                  editIndex_   = -1;

    // ---- Helpers -----------------------------------------------------
    void onNoteOn (const MidiMessage& msg);
    void onNoteOff(const MidiMessage& msg);
    void releaseAllNotesOnChannel(uint8_t ch);
    void advanceWorms(int dy);

    void drawHeader(Display& d) const;
    void drawWorms(Display& d)  const;
    void drawKeyboard(Display& d) const;
    void drawChordQueue(Display& d) const;
    void drawDebug(Display& d) const;
    void drawSplash(Display& d) const;
    void drawBigBpm(Display& d) const;
    void drawNotation(Display& d) const;
    void drawMappingMode(Display& d) const;
    void drawChordNames(Display& d, int rightEdge) const;

    // Mapping-mode helpers.
    void captureTriggerForEditing(const MidiMessage& msg);
    void commitEditToEngine();
    void cycleEditChordType(int delta);
    void cycleEditDirection();
    void adjustEditGate(int delta);
    void browseNextMapping();

    static bool     isBlackPc(int pc);
    static int      whiteKeyIdx(uint8_t note);   // white-key index from kLowestNote
    static uint8_t  whiteKeyAt(int idx);
    static KeyRect  keyRectFor(uint8_t note);
    static uint16_t channelColor(uint8_t channel);
    static bool     noteVisible(uint8_t note) {
        return note >= kLowestNote && note <= kHighestNote;
    }

    // Lowest-numbered channel currently holding `note`, or 0 if none.
    // Used by the keyboard-highlight code to pick a fill colour when more
    // than one channel is sustaining the same note.
    uint8_t pressedChannelFor(uint8_t note) const;

    // Same for engine-output notes.
    uint8_t outPressedChannelFor(uint8_t note) const;

    // Plumbing for the chord engine's echo. Static dispatcher is set on
    // the engine at construction time; the per-instance handler updates
    // outNotePressedBy_ and spawns / stops output worms.
    static void engineEchoStatic(void* user, const MidiMessage& msg);
    void onEngineEcho(const MidiMessage& msg);

    // Writes a chord name like "Cmaj7", "Am", "G/B" to `out` when channel
    // `ch` is sustaining 3+ notes that match a known triad/seventh.
    // Writes an empty string otherwise.
    void detectChordOnChannel(uint8_t ch,
                              char* out, std::size_t outSize) const;
};

} // namespace core
