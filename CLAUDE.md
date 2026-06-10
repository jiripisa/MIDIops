# MIDIops — project notes for Claude

A hardware MIDI chord trigger / monitor for the Teensy 4.1 + 2.8"
ILI9341 display. Current state: incoming NoteOn → mapped to a chord
(maj/min/dim/aug/7/m7/maj7, BLOCK or arpeggiated up/down), played out
on a per-mapping output channel. Triggers queue FIFO so chords never
overlap. Three views (monitor / big-BPM / notation) plus a full
mapping editor on the device.

## The architectural rule (do not break this)

**`core/` is portable C++17 and never includes platform headers.** No
`Arduino.h`, no `SDL.h`, no `RtMidi.h`, no Teensy-specific headers. The same
`core/` translation units are compiled into both binaries:

```
core/         portable           depends on: C++17 stdlib only
platform/teensy/   hardware      depends on: Arduino, ILI9341_t3, usbMIDI
platform/host/     simulator     depends on: SDL2, RtMidi
```

When adding a feature:

1. Put the logic and state in `core/`.
2. If it needs hardware access, extend an abstract interface (`Display`,
   `MidiInput`, or a new one) and add implementations under both
   `platform/teensy/` and `platform/host/`.
3. If you find yourself wanting to include `Arduino.h` from `core/`, the
   right answer is always "add a method to an abstract interface".

## Build & run

```bash
make sim         # simulator (SDL window + virtual MIDI port "MIDIops")
make firmware    # build the Teensy .hex
make flash       # build + upload to a connected Teensy
make clean       # blow away .pio/
```

Both environments live in `platformio.ini`. `build_src_filter` enforces the
"only one platform/ subtree per env" rule.

## Current scope

* **Monitor view** — per-channel coloured worms scrolling up from a
  4-octave keyboard. Engine-played notes appear as gray rectangular
  outlines (ghost notes) layered on top. Header shows channel filter +
  BPM + held-note chord names; queue strip below shows the chord engine's
  playback list.
* **Big-BPM view** — current MIDI Clock tempo, large.
* **Notation view** — grand staff with rolling note heads + stems,
  held-note names below (drift down + fade after release), held-chord
  name above.
* **Mapping mode** — flip the latching panel switch ON. Capture a
  trigger by pressing a note; configure chord type / gate ticks /
  direction / output channel with the encoders. Auto-saves to the
  chord engine. Up to 16 mappings.
* **Splash + mapping-prompt artwork** — full-screen 320×240 RGB565
  bitmaps blitted from auto-generated headers
  (`core/splash_image.h`, `core/mapping_prompt_image.h`). Regenerated
  from PNG via `scripts/build_splash_image.py`.

Three rotary encoders are wired:
  * Channel encoder on pins 3 (SW), 4 (CLK), 5 (DT).
  * BPM encoder on pins 14 (CLK), 15 (DT), 16 (SW).
  * View encoder on pins 17 (CLK), 18 (DT), 19 (SW).

Latching front-panel switch (DFR0789) on pin 2 drives mapping mode.

## Where future milestones plug in

The current layout has been chosen specifically so future additions
don't require restructuring — only new files in `core/` and matching
implementations under `platform/teensy/` and `platform/host/`.

Likely next steps:
  * **Persistence** of mappings to flash so they survive a power
    cycle (Teensy has 7+ MB free).
  * **More chord types** in `ChordEngine::ChordType` + interval
    tables in `core/ChordEngine.cpp`.
  * **Transport** (Start / Continue / Stop) wired to one of the
    encoder shaft buttons or a future panel button.
  * **Per-mapping output channel editor** — currently the editor
    knobs only cover type / gate / direction.

## Reference: Berlin School mode

The **Berlin** mode (a generative Berlin-School MIDI sequencer) is built
from an external spec distilled from 235 "Synth Seeker" video transcripts:

`/Users/jpisa/Development/Claude/synthseeker/berlin-school-theory-and-generator-spec-EN.md`

Part 1 = theory (scales, tension/release, note-phasing, drunkard's walk,
high/mid/low voices, odd meters); Part 2 = generator spec (data tables,
parameter ranges/defaults, the "Generate" algorithm, presets, DO/DON'T).
This document is the source of truth for Berlin mode's generation rules
and parameters — consult it whenever working on that mode.

## Conventions

* C++17 throughout. No exceptions in `core/` (RtMidi throws, host code
  catches at the boundary).
* All identifiers, file contents and comments in English regardless of chat
  language.
* RGB565 (`uint16_t`) is the canonical pixel format because the ILI9341 uses
  it natively; the SDL backend declares its texture with
  `SDL_PIXELFORMAT_RGB565` so no conversion is needed.
* MIDI channels in `core::MidiMessage` are stored 1..16 (never 0..15), with
  `0` reserved as the sentinel for OMNI in `MidiMonitorApp::channel_`.

## Hardware documentation — keep in sync

There are two hardware-facing docs at the repo root that MUST stay
current as the build evolves:

* `HARDWARE.md` — bill of materials, the master Teensy pin-assignment
  table, per-module wiring, and the ASCII schematic. This is the
  canonical reference; any disagreement between docs is resolved in
  favour of the code constants in `platform/teensy/main.cpp` (`kPin*`).
* `ASSEMBLY.md` — beginner-friendly step-by-step build walkthrough.

Whenever you change the hardware setup, update both files in the same
commit as the code change. Common triggers:

* A `kPin*` constant in `platform/teensy/main.cpp` moves to a different
  Teensy GPIO.
* A peripheral is added, removed, or replaced (display, button,
  encoder, future encoders, future arpeggiator output, etc.).
* A wiring convention changes (e.g. pulling a pin via a different rail,
  swapping polarity).
* The bill of materials changes (new vendor, swapped sensor variant).

If the change is software-only and touches no wiring at all, leave both
files alone.

## The "listened channel" knob

There is exactly one source of truth for the listened channel:

```cpp
// core/MidiMonitorApp.h
static constexpr uint8_t kDefaultChannel = 0;  // 0 = OMNI, 1..16
```

Change this value, rebuild, done — both targets pick it up.
