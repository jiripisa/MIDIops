# MIDIops

A hardware MIDI instrument built on a **Teensy 4.1** with a 2.8" ILI9341 SPI
display, paired with a **macOS SDL/RtMidi simulator** so the same app code can
be iterated on without flashing every change.

It runs several switchable **modes**: a MIDI **monitor**, a scale-aware
**arpeggiator**, a generative **Berlin-School sequencer**, a big-tempo view,
on-device **settings**, and a hardware **debug** view. It is a MIDI **Clock
master** (24 PPQN) that can also **follow an external clock**, and it
identifies itself over USB as a class-compliant MIDI device named **`MIDIops`**.

```
core/                 portable C++17, no platform headers
platform/teensy/      Teensy 4.1: ILI9341_t3n + usbMIDI + PJRC Encoder
platform/host/        macOS:      SDL2        + RtMidi  + keyboard
```

The architecture rule (see [`CLAUDE.md`](CLAUDE.md)): `core/` is portable
C++17 and never includes platform headers — the same `core/` translation units
compile into both the firmware and the simulator.

## 📖 User manual

Full controls + every parameter, in two languages:
**[English](MANUAL.md)** · **[Čeština](MANUAL.cs.md)**.

## Controls

The front panel has **five rotary encoders** (Enc1–Enc5, each also a push
button) and **three latching switches** (Latch1–Latch3). Roles are assigned by
the active mode/screen, not fixed to the hardware:

- **Enc1–Enc4** — edit the four parameters of the current screen.
- **Enc5** — rotate to **switch screens**, press to open the **mode-select
  overlay** (rotate to choose a mode, press to enter).
- **Latch1–Latch3** — **transport**, with a per-mode meaning (e.g. in Berlin:
  Play/Pause · Stop · Generate; in Arp: Hold · Mute · Reset).

See the [user manual](MANUAL.md) for every screen, parameter (range / default /
meaning) and the full simulator key map.

## Modes

| Mode | What it does |
|---|---|
| **Monitoring** | Per-channel "worms" + notation views of incoming MIDI notes. |
| **Arp** | Scale-aware arpeggiator; held/injected notes are arpeggiated on the clock (steps, rate, gate, direction, octave, swing, velocity; Hold/Mute/Reset). |
| **Berlin** | Single-voice generative Berlin-School sequencer — three generators (Drunkard's Walk / Gate-Pitch Phasing / Degree-Weighted), Morph regeneration, Locked/Evolve/Live behaviors, piano-roll with a keyboard. |
| **BPM** | Large tempo display; sets the global BPM (30–300). |
| **Settings** | Scale & root, MIDI out/in channel, clock source (Internal/External). |
| **Debug** | Live per-control telemetry for hardware bring-up. |

Scale, root, tempo and the MIDI channels are **global** (set in Settings/BPM)
and shared by Arp and Berlin.

## What it looks like

The simulator window is the truth — 320×240 logical pixels scaled 3× on the
desktop, byte-identical to what the Teensy renders.

### Monitoring — worms

Per-channel coloured "worms" scroll up from a 4-octave keyboard while incoming
notes are held; channel 1 is green. The header shows the channel filter and
tempo.

![Monitoring — worms](docs/screenshots/monitor.png)

### BPM

The current MIDI Clock tempo, rendered huge.

![BPM view](docs/screenshots/big-bpm.png)

### Notation

A grand staff with recent notes as note-heads + stems, held-note names below.

![Notation view](docs/screenshots/notation.png)

## Quick start

### Prerequisites

Apple Silicon Mac, Homebrew under `/opt/homebrew`:

```bash
brew install platformio sdl2 rtmidi pkg-config
brew install --cask teensy        # only needed for flashing real hardware
```

PlatformIO auto-installs the Teensy toolchain and `ILI9341_t3n` / `Encoder`
libraries on the first `pio run -e teensy41`.

### Run the simulator

```bash
make sim
```

A window opens. The process creates a virtual CoreMIDI **input** port named
**`MIDIops Sim`** that any DAW can route notes/clock into, and a virtual
**output** port of the same name that broadcasts the 24 PPQN clock
plus the arpeggiator/Berlin NoteOn/NoteOff stream.

Simulator keys (the encoders are key trios `{left, press, right}`):

| Keys | Control |
|---|---|
| `1` `2` `3` · `4` `5` `6` · `7` `8` `9` · `0` `-` `=` | Enc1 · Enc2 · Enc3 · Enc4 |
| `Q` `W` `E` | Enc5 (switch screens / mode overlay) |
| `Space` · `Backspace` · `Return` | Latch1 · Latch2 · Latch3 (toggle) |
| `z x c v b n m` | Inject notes C4–B4 |
| `Shift`+`1…9` | Channel the injected notes are sent on |
| `Esc` | Quit |

### Build & flash the Teensy

```bash
make firmware       # build only — prints the .hex path
make flash          # build + upload via teensy_loader_cli / Teensy Loader
pio test -e test    # run the host unit-test suite (Unity)
```

Plug the Teensy in over USB **before** running `make flash`. The build sets
`USB_MIDI_SERIAL` so the device shows up as a class-compliant MIDI device named
`MIDIops`. If macOS still shows an old USB descriptor name after a rename,
reset CoreMIDI's cache with `sudo killall MIDIServer`.

## Hardware

Five KY-040 encoders (Enc1–Enc5) and three DFR0789 latching switches
(Latch1–Latch3) on a 2.8" ILI9341 SPI display. Their **roles are assigned by
the active mode at runtime** — the pin names are physical identities, not
fixed functions.

| Peripheral | Pins |
|---|---|
| ILI9341 2.8" SPI display | 8 (RST), 9 (DC), 10 (CS), 11/12/13 (hardware SPI) |
| Enc1 (CLK/DT/SW) | 4 / 5 / 3 |
| Enc2 (CLK/DT/SW) | 14 / 15 / 16 |
| Enc3 (CLK/DT/SW) | 17 / 18 / 19 |
| Enc4 (CLK/DT/SW) | 6 / 7 / 0 |
| Enc5 (CLK/DT/SW) | 20 / 21 / 22 |
| Latch1 / Latch2 / Latch3 | 2 / 1 / 23 |

The canonical pin table, per-module wiring and an ASCII schematic live in
[`HARDWARE.md`](HARDWARE.md); a beginner-friendly build walkthrough is in
[`ASSEMBLY.md`](ASSEMBLY.md). Both must be updated in the same commit as any
hardware change — see the doc-sync rules in [`CLAUDE.md`](CLAUDE.md).

## Routing MIDI from Ableton via the macOS IAC bus

1. Open **Audio MIDI Setup** → **Window → Show MIDI Studio**.
2. Double-click **IAC Driver**, tick **Device is online**, ensure at least one
   bus is listed.
3. Start the simulator (`make sim`); it announces `[RtMidi] virtual input
   port opened: "MIDIops Sim"` on stderr.
4. In Ableton → **Preferences → Link/Tempo/MIDI → MIDI**, enable the **Track**
   output for `MIDIops Sim` (it appears once the simulator runs).
5. On a Live MIDI track, set the MIDI output to `MIDIops Sim` on the channel that
   matches the **MIDI In channel** in Settings (default OMNI accepts all).
   Notes you play appear in the simulator.

To **hear** Arp/Berlin, route `MIDIops Sim` (input on Ableton's side) into an
instrument track. That port also broadcasts MIDI Clock; to instead **follow**
Ableton's clock, set Settings → Clock = **External** and enable Ableton's clock
output to `MIDIops Sim`.

## Layout

```
core/                        Portable C++17 — MUST NOT include Arduino/SDL/RtMidi headers
  app/                       AppShell (mode runtime), Mode/Screen, AppServices
  modes/                     MonitoringMode, ArpMode, BerlinMode, BpmMode, SettingsMode, DebugMode
  ArpEngine.*, ArpTypes.h    Clock-driven arpeggiator engine + params
  BerlinEngine.*, Berlin*    Generative sequencer engine, params, RNG, sequence
  *Generator.*               Berlin generators (DrunkardWalk / GatePitchPhasing / DegreeWeighted) + shared helpers
  Scale.*                    Scale quantization (degrees, root, scale types)
  ClockFollower.*            Derives BPM from an incoming external clock
  Display.h / MidiInput.h / MidiOutput.h / Encoder.h / Button.h   Abstract platform interfaces
  MidiMessage.*              MIDI message value type (incl. realtime clock/transport)
  NoteWormModel.*, render/   Visualization models + renderers (worms, notation, piano-roll, param grid)

platform/
  teensy/                    Teensy 4.1 backend (ILI9341_t3n, usbMIDI, IntervalTimer clock, PJRC Encoder), main.cpp
  host/                      macOS simulator backend (SDL2 framebuffer, RtMidi virtual ports, keyboard), main.cpp

docs/                        Specs, implementation plans, screenshots
scripts/                     Helper bash + Python (build, version-inject, image converter)
platformio.ini               Environments: teensy41, native (sim), test (Unity unit tests)
Makefile                     Thin wrapper around pio + helpers
MANUAL.md / MANUAL.cs.md      Bilingual user manual
HARDWARE.md / ASSEMBLY.md     Hardware reference + build walkthrough
CLAUDE.md                    Project conventions for AI assistants
```
