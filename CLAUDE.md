# MIDIops — project notes for Claude

A mode-based hardware MIDI instrument for the Teensy 4.1 + 2.8"
ILI9341 display. It runs a set of modes — Monitoring / Arp / Berlin /
BPM / Settings / Debug — on an `AppShell` + `Mode`/`Screen` runtime,
driven by a unified 24-PPQN clock that is either the internal master or
a followed external MIDI clock. The same `core/` is built alongside a
macOS SDL/RtMidi simulator so behaviour can be developed off-hardware.

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

The six modes (boot order = `addMode()` order in the mains):

* **Monitoring** — incoming MIDI visualised as per-channel coloured
  worms scrolling up from a keyboard, plus a notation screen; held-note
  chord names in the header.
* **Arp** — arpeggiator. Hold notes, configure pattern/octaves/gate per
  screen; Latch1 = Hold, Latch2 = Mute, Latch3 = Reset. A fifth
  "presets" screen saves/loads/deletes the params in 20 slots.
* **Berlin** — generative Berlin-School sequencer (see the spec
  reference below); Latch1 = Play, Latch2 = Stop, Latch3 = Generate.
  Its "presets" screen stores params + the realized sequence in 20
  slots; loading mid-play swaps seamlessly (playhead wraps).
* **BPM** — large tempo display; Enc1 sets BPM (read-only while the
  clock source is External).
* **Settings** — global settings on three screens (MIDI: out channel /
  in channel / clock source / transport; Scale: scale type / root;
  System: two-step factory reset).
* **Debug** — live telemetry for every encoder and latch; for bring-up.

Control scheme (roles are assigned at runtime by the active mode/screen,
not by the pin names):
  * **Enc1–Enc4** — the per-screen parameter knobs (rotate to edit,
    press for the screen's secondary action).
  * **Enc5** — rotate to switch screen within a mode; press to open the
    mode-select overlay (rotate to pick a mode, press to confirm).
  * **Latch1–Latch3** — stateless transport buttons: every mechanical flip
    is one click and the switch position carries no meaning (all state lives
    in the app and is shown on screen). Per-mode meaning: globally
    (non-capturing modes) a Latch1 press toggles Play/Pause, Latch2 = Stop,
    Latch3 = Reset; capturing modes (Arp, Berlin) repurpose the presses as
    listed above.

Global settings (in Settings mode): scale type + root, MIDI out channel,
MIDI in channel (0 = OMNI), clock source (Internal / External), and
transport (Off / Send / Receive). All of them — plus the BPM — persist:
`AppShell` auto-saves a versioned blob ~2 s after the last change through
the `core/Storage.h` interface (Teensy: LittleFS on program flash via
`TeensyStorage`; host: files under `~/.midiops/` via `FileStorage`).

The physical panel (pins per `platform/teensy/main.cpp`):
  * **Five KY-040 encoders** Enc1–Enc5 — Enc1 4/5/3, Enc2 14/15/16,
    Enc3 17/18/19, Enc4 6/7/0, Enc5 20/21/22 (CLK/DT/SW).
  * **Three DFR0789 latches** — Latch1 pin 2, Latch2 pin 1, Latch3 pin 23.

Roles are assigned at runtime by the modes (see the control scheme
above); the pin names describe physical identity only.

## Where future milestones plug in

The current layout has been chosen specifically so future additions
don't require restructuring — only new files in `core/` and matching
implementations under `platform/teensy/` and `platform/host/`.

Likely next steps:
  * **More scales / Berlin generator options** in `core/Scale.*` and
    `core/BerlinTypes.h`.
  * **More Arp patterns** in `core/ArpTypes.h` + `core/ArpEngine.cpp`.
  * **Multi-voice Berlin** (bass/mid/lead with cross-voice phasing) per
    Part 2 of the spec document below.

(Persistence is done: global settings auto-save via `core/Storage.h`,
and the modes' preset slots live in `core/Presets.*` + the shared
`core/app/PresetScreen.*` picker.)

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
  `0` reserved as the sentinel for OMNI in `AppShell::midiInChannel_`.

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

## User manual — keep in sync

There is a bilingual end-user manual at the repo root, linked from
`README.md`:

* `MANUAL.md` — English.
* `MANUAL.cs.md` — Czech (a faithful translation of the English).

It documents every mode, screen, parameter (name / range / default /
meaning), the control scheme (Enc1–5, Latch1–3), navigation, the global
settings, and the simulator key bindings. It MUST stay current as the
user-facing behaviour evolves. Update BOTH language files in the same commit
as the code change. Common triggers:

* A parameter is added, removed, renamed, or its range/default changes
  (e.g. in `core/ArpTypes.h`, `core/BerlinTypes.h`, a mode's screens).
* A mode or screen is added/removed/reordered, or an encoder/latch is
  remapped (mode `onRawInput`, `screen()` order, `addMode()` order in the
  mains).
* The simulator key bindings change (`platform/host/main.cpp`).
* Navigation or global-settings behaviour changes (`core/app/AppShell.*`,
  `core/modes/SettingsMode.*`).

Keep the two language files structurally identical (same sections, tables,
and rows). If a change is purely internal and changes nothing the user sees
or touches, leave the manual alone.

## The MIDI-in channel filter

The global MIDI-in channel filter lives in `AppShell::midiInChannel_`
(`0` = OMNI, the default; `1..16` for a single channel). It is edited at
runtime in **Settings → MIDI** with Enc2 — no rebuild needed. Both targets
share the same `core/` state.

(The old compile-time `kDefaultChannel` constant and the `MidiMonitorApp`
class no longer exist — that legacy chord-trigger app has been removed.)
