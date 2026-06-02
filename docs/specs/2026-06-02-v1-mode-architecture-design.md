# MIDIops v1 — mode-based firmware architecture (design)

Status: draft for review · Date: 2026-06-02 · Supersedes: the PoC (`poc` tag)

## 1. Why this exists

The PoC proved the approach (portable `core/` + `platform/{teensy,host}`
split, the SDL/RtMidi simulator, on-device flashing, and a low-jitter MIDI
Clock master). It also accumulated a monolith: `core/MidiMonitorApp`
(~1650 lines) holds every view, the worm/keyboard/notation rendering, a
chord-mapping editor, and a flat `View` enum, all in one class.

v1 is the first *real* firmware. It introduces a **mode-based**
architecture sized for ongoing growth (many modes, each with several
screens), and re-homes the PoC's proven pieces — the MIDI Clock master,
the worms renderer, the notation renderer, and `ChordEngine` — as
reusable components rather than monolith internals. **Nothing valuable is
discarded; it is extracted.**

## 2. Terminology

- **Mode** (CZ "režim") — a top-level unit of behaviour. Exactly one is
  active at a time. Examples: Monitoring, Arp, Berlin, BPM, Settings,
  Debug. The set grows over time.
- **Screen** — one renderable + interactive page within a Mode. A Mode has
  one or more Screens.
- **AppShell** — the runtime that owns the active Mode, global state
  (settings, transport, the shared note model, the clock), draws the top
  bar, runs the mode-change overlay, and routes hardware input to the
  active Mode/Screen.

The top bar always shows the **Mode name + Screen name** in a smaller
font.

## 3. Hardware control map

Hardware identities are fixed (Enc1–5, Latch1–3); their *function* lives in
the app layer and can differ per mode/screen.

| Control | Role |
|---|---|
| **Enc1–Enc4** rotate | Forwarded to the active Screen (e.g. Arp param edit). |
| **Enc1–Enc4** SW (press) | Forwarded to the active Screen. |
| **Enc5** rotate | Switch Screen within the active Mode (wraps). |
| **Enc5** press | Open the **mode-change overlay** (see §4). |
| **Latch1** toggle | Transport: **Play / Pause**. |
| **Latch2** toggle | Transport: **Stop**. |
| **Latch3** toggle | Transport: **Reset / Back**. |

Latches are physically latching switches; the firmware reads **each state
change as one event** (a toggle/trigger). Play/Pause maps naturally to a
latch; Stop/Reset are treated as "fire on each transition". No hardware
change is planned for v1.

## 4. Mode-change overlay

- **Enc5 press** opens an overlay panel listing the available modes.
- **Enc2 rotate** moves the selection through the list.
- **Enc5 press** confirms → switch to the selected mode.
- If **3 s** elapse since the last Enc2 rotation without a confirm, the
  overlay closes and the **previous mode is restored** (no change).
- While the overlay is open, Enc1/Enc3/Enc4 and the transport latches are
  suppressed (only Enc2 + Enc5 act). MIDI input continues to be processed
  by the active mode underneath.

## 5. Core abstractions

Sketch (final signatures settled during planning):

```cpp
// One interactive page inside a Mode.
class Screen {
public:
    virtual ~Screen() = default;
    virtual const char* name() const = 0;
    virtual void onEnter() {}
    virtual void onExit() {}
    virtual void onEncoder(int idx, int delta) {}  // idx 1..4
    virtual void onEncoderSw(int idx) {}           // idx 1..4
    virtual void update(uint32_t nowMs) {}
    virtual void render(Display& d) const = 0;
};

enum class Transport { Play, Pause, Stop, Reset };

// A top-level unit of behaviour. Owns its screens + mode-local state.
class Mode {
public:
    virtual ~Mode() = default;
    virtual const char* name() const = 0;
    virtual int     screenCount() const = 0;
    virtual Screen& screen(int i) = 0;
    virtual void onEnter() {}
    virtual void onExit() {}
    virtual void onMidiIn(const MidiMessage& msg) {}
    virtual void onTransport(Transport t) {}
    virtual void update(uint32_t nowMs) {}
};
```

`AppShell` holds `Mode* modes[]`, the active mode index, the per-mode
active screen index, the mode-change overlay state, and the global
services below. It:

- routes Enc1–4 (+SW) to the active screen;
- handles Enc5 rotate (screen switch) and press (overlay) itself;
- turns Latch1–3 toggles into `Transport` events → active mode +
  emits MIDI realtime Start/Stop/Continue as appropriate;
- feeds MIDI input to the shared `NoteState` and to `activeMode.onMidiIn`;
- draws the top bar, then `activeScreen.render`.

## 6. Shared model & global services

Owned by `AppShell`, visible to modes:

- **`NoteState`** — the set of currently-held notes arriving on MIDI in
  (per channel), with timestamps. One source of truth fed by the shell.
- **`WormField`** — the piano-roll "worm" model, **parameterised by a note
  source**. Monitoring feeds it input notes; Arp feeds it the engine's
  outgoing notes. Extracted from today's `MidiMonitorApp` worm code.
- **`Settings`** — see §8.
- **Clock & transport** — the MIDI Clock master (existing
  `TeensyMidiOutput`) plus a transport state machine
  (`Stopped`/`Playing`/`Paused`). The active mode observes transport; the
  shell emits Start/Continue/Stop.

## 7. v1 modes

### 7.1 Monitoring — *2 screens: Worms, Notes*
Watches notes on MIDI in. Worms screen = the extracted `WormsRenderer`
over `NoteState`; Notes screen = the extracted `NotationRenderer` over the
same. Pure visualisation; transport has no effect.

### 7.2 Arp — *4 screens: Params 1–4, Params 5–8, Worms, Notes*
Receives notes on the input channel; emits an arpeggiated chord on the
output channel via `ChordEngine`. Params screens map **Enc1–Enc4 to four
parameters each** (8 total; exact parameter list settled in planning —
candidates: chord type, direction, gate, octave range, rate, swing,
velocity, hold). Worms/Notes screens visualise the **outgoing** notes
(WormField/Notation fed by the engine's output). Honours `Settings`
scale + root. Transport Play/Stop gates whether the arp runs.

### 7.3 Berlin — *screens TBD*
Receives input notes, emits a generated **sequence** on the output
channel. **Algorithm to be specified separately** before implementation.
v1 registers the mode with a placeholder screen so the framework is
exercised; the generator is its own mini-spec.

### 7.4 BPM — *1 screen*
Large current-tempo readout (reuse the PoC big-BPM rendering). Reflects
the clock source (internal tempo, or — later — followed external clock).
Transport state shown here too.

### 7.5 Settings — *screen(s) TBD by field count*
Edits global settings (§8) with Enc1–Enc4. Values applied live.

### 7.6 Debug — *1 screen*
The existing Debug view (Enc1–5 + Latch1–3 telemetry), ported as a mode.

## 8. Settings (global)

| Setting | Range / values |
|---|---|
| MIDI out channel | 1–16 |
| MIDI in channel | OMNI, 1–16 |
| Clock source | Internal (we are master) / External (follow incoming clock) |
| Scale type | maj, min, aug, dim, pentatonic-maj, pentatonic-min |
| Root key | C…B |

`Scale` (type + root → permitted note set / quantiser) is consumed by Arp
and Berlin. **External clock follow** is designed-for but may be stubbed
in v1 (internal master is the default and already precise); the follower
is a later increment.

Persistence of settings to flash is **out of scope for v1** (Teensy has
room; added later).

## 9. Keep / extract / retire

| PoC asset | v1 disposition |
|---|---|
| `TeensyMidiOutput` clock master (send_now + float period) | **Keep 1:1** |
| Platform split + `Display`/`Midi*`/`Encoder`/`Button` interfaces | **Keep** |
| `ChordEngine` (FIFO, BLOCK/UP/DOWN, chord types, mappings) | **Keep + grow** (Arp core; mapping capability retained) |
| Worm model + `drawWorms` + `drawKeyboard` | **Extract** → `WormField` + `WormsRenderer`/`KeyboardRenderer` |
| `drawNotation` + `notation_glyphs.h` | **Extract** → `NotationRenderer` |
| Big-BPM drawing | **Extract** → BPM mode |
| Debug view | **Port** → Debug mode |
| Splash, channel colours, chord detection | **Extract** as utilities |
| Chord-mapping **editor UI** | **Park** — not shown in v1; returns later as its own mode (logic in `ChordEngine` stays) |
| Flat `View` enum + `MidiMonitorApp` god-class | **Retire** once modes cover its functionality |

## 10. Migration plan (strangler, in-place)

Each step keeps both builds (native + teensy41) green.

1. **Framework**: add `AppShell` + `Mode`/`Screen` + the mode-change
   overlay + top bar + transport routing, alongside `MidiMonitorApp`.
   Point both `main.cpp`s at the shell hosting a single **Debug** mode
   (port the existing debug view first) to validate routing.
2. **Extract renderers**: pull `WormsRenderer`/`KeyboardRenderer` and
   `NotationRenderer` out of the monolith → build **Monitoring** mode
   (Worms + Notes). First real unpicking of the monolith.
3. **Arp** mode over `ChordEngine` + `Scale`, reusing the same renderers
   for outgoing-note visualisation.
4. **BPM** and **Settings** modes.
5. **Berlin** mode once its algorithm is specified.
6. **Retire** the remainder of `MidiMonitorApp` (mapping editor UI parked,
   `View` enum removed).

## 11. Deferred / out of scope for v1

- Berlin sequence algorithm (separate spec).
- Chord-mapping editor as a mode (returns later).
- Settings persistence to flash.
- External-clock follower (Settings toggle exists; follow logic later).
- Standby / soft power-off (blocked on the backlight rewire — see the
  earlier decision).

## 12. Open questions to settle during planning

- Exact Arp parameter list (which 8 params, ranges, which encoder edits
  which).
- Number of Settings screens (depends on field grouping).
- Berlin screens once the algorithm exists.
- Whether transport state is global or per-mode-interpreted (leaning:
  global state, mode decides what it means).
