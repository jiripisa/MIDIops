# MIDIops

A hardware MIDI monitor + MIDI Clock master built on a **Teensy 4.1** with
a 2.8" ILI9341 SPI display, paired with a **macOS SDL/RtMidi simulator** so
the same app code can be iterated on without flashing every change.

Identifies itself over USB as **`MIDIops`** (class-compliant MIDI), shows
incoming notes on three switchable views, and runs a sub-µs-precise
MIDI Clock master so external gear can sync to it.

The repo is structured so future milestones — arpeggiator, more views,
more encoders, transport control — plug in as new files in `core/` with
matching backends under `platform/teensy/` and `platform/host/`. No
restructuring needed.

## What it looks like

The simulator window is the truth — 320×240 logical pixels scaled 3× on
the desktop, byte-identical to what the Teensy renders. The screenshots
below come straight from `make sim`.

### Monitor view (default)

Per-channel "worms" scroll up from a 4-octave piano keyboard while notes
are held. The header shows the listened channel and, when a channel
holds a recognizable triad/seventh, its chord name in the same colour
as the worm.

![Monitor view](docs/screenshots/monitor.png)

### Big-BPM focus

The current MIDI Clock tempo, rendered huge. Press the BPM encoder's
shaft button (or **Tab** in the simulator) to switch. The encoder still
adjusts tempo while this view is active.

![Big BPM view](docs/screenshots/big-bpm.png)

### Notation view

A grand staff (treble + bass clef) with every recent note rendered as a
proper note-head + stem, scrolling from right to left. The clef bitmaps
are downsampled from hand-drawn pixel-art references (see the file
header in `core/notation_glyphs.h` for the conversion approach). Each
note keeps its source channel's palette colour and gets a `#`
accidental if it's a black key.

![Notation view](docs/screenshots/notation.png)

## Architecture, in one paragraph

`core/` is portable C++17 and **never** includes a platform header. It
defines the app (`MidiMonitorApp`), the message value type, and the
abstract `Display` / `MidiInput` / `MidiOutput` / `Button` / `Encoder`
interfaces. Two thin backends in `platform/teensy/` and `platform/host/`
provide concrete implementations. The exact same `core/` translation
units are compiled into both binaries; `platformio.ini`'s `build_src_filter`
enforces the "only one platform subtree per env" rule.

```
core/                 portable C++17, no platform headers
platform/teensy/      Teensy 4.1: ILI9341_t3n + usbMIDI + PJRC Encoder
platform/host/        macOS:      SDL2        + RtMidi  + keyboard
```

## Quick start

### Prerequisites

Apple Silicon Mac, Homebrew under `/opt/homebrew`:

```bash
brew install platformio sdl2 rtmidi pkg-config
brew install --cask teensy        # only needed for flashing real hardware
```

PlatformIO auto-installs the Teensy toolchain and `ILI9341_t3n` /
`Encoder` libraries on the first `pio run -e teensy41`.

### Run the simulator

```bash
make sim
```

A 960×720 window opens. The process creates a virtual CoreMIDI input
port named **`MIDIops`** that any DAW or MIDI utility can route into,
and a virtual MIDI **output** port named **`MIDIops Clock`** that
broadcasts 24 PPQN clock.

Keyboard cheatsheet (printed to stderr at startup):

| Key | What it does |
|-----|--------------|
| `A`–`G` | Inject white-key Note On/Off in octave 4 |
| `1`–`9` | Pick the channel that the next `A`–`G` press lands on |
| `←` / `→` | Channel-encoder simulation (cycles OMNI..16) |
| `↑` / `↓` | BPM-encoder simulation (±1 BPM) |
| `Space` | Toggle monitoring on/off |
| `F5` | Restart (re-show splash) |
| `Backspace` | Panic — release stuck notes |
| `Tab` | Cycle monitor → big-BPM → notation views |
| `Esc` | Quit |

### Build & flash the Teensy

```bash
make firmware    # build only — prints the .hex path
make flash       # build + upload via teensy_loader_cli / Teensy Loader
```

Plug the Teensy in over USB **before** running `make flash`. The build
sets `USB_MIDI_SERIAL` so the device shows up as a class-compliant MIDI
device named `MIDIops`. If macOS still shows an old USB descriptor name
after a rename, reset CoreMIDI's cache with:

```bash
sudo killall MIDIServer
```

## Hardware

Three peripherals on the breadboard:

* **ILI9341 2.8" SPI display** — 240×320 RGB565, 40 MHz hardware SPI,
  rendered via `ILI9341_t3n` async DMA so the main loop never blocks.
* **DFR0789 latching panel switch** — toggles MIDI monitoring on/off
  (built-in LED mirrors the state).
* **Two KY-040 rotary encoders** — channel selection (with restart-app
  push) and BPM (with view-cycle push).

Pin assignment, wiring tables, and an ASCII schematic live in
[`HARDWARE.md`](HARDWARE.md). A beginner-friendly step-by-step build
walkthrough is in [`ASSEMBLY.md`](ASSEMBLY.md). Both files must be
updated in the same commit as any hardware change — see the doc-sync
rule in [`CLAUDE.md`](CLAUDE.md).

## Routing MIDI from Ableton via the macOS IAC bus

1. Open **Audio MIDI Setup** → **Window → Show MIDI Studio**.
2. Double-click **IAC Driver**, tick **Device is online**, ensure at least
   one bus is listed.
3. Start the simulator (`make sim`); it announces `[RtMidi] virtual input
   port opened: "MIDIops"` on stderr.
4. In Ableton → **Preferences → Link/Tempo/MIDI → MIDI**, enable the
   **Track** output for `MIDIops` (it appears once the simulator runs).
5. On a Live MIDI track, set the MIDI output to `MIDIops` / channel 1
   (or whichever channel matches `kDefaultChannel`). Notes you play
   appear in the simulator window.

If you want Ableton to **sync to** the simulator's clock instead, enable
the **Sync** input for `MIDIops Clock` and set Live to **External**.

## Configuring the listened channel

```cpp
// core/MidiMonitorApp.h
static constexpr uint8_t kDefaultChannel = 0;  // 0 = OMNI, 1..16 = a channel
```

A single source of truth, used by both targets.

## Layout

```
core/                       Portable C++17 — MUST NOT include Arduino/SDL/RtMidi headers
  MidiMessage.{h,cpp}       MIDI message value type + helpers
  Display.h                 Abstract drawing surface (RGB565)
  MidiInput.h               Abstract poll() interface
  MidiOutput.h              Abstract clock master interface
  Button.h                  Abstract on/off button
  Encoder.h                 Abstract rotary detents
  MidiMonitorApp.{h,cpp}    The app: views, worms, chord detection
  notation_glyphs.h         Bitmap glyphs (clefs, note head, sharp)

platform/
  teensy/                   Teensy 4.1 backend
    TeensyDisplay.{h,cpp}      Wraps ILI9341_t3n with async DMA
    TeensyMidiInput.{h,cpp}    Wraps usbMIDI.read()
    TeensyMidiOutput.{h,cpp}   IntervalTimer-driven 24 PPQN clock
    TeensyButton.{h,cpp}       Debounced INPUT_PULLUP button
    TeensyEncoder.{h,cpp}      Wraps PJRC Encoder library
    usb_names.c                Overrides USB descriptors to "MIDIops"
    main.cpp                   setup / loop
  host/                     macOS simulator backend
    SdlDisplay.{h,cpp}         RGB565 framebuffer, scaled 3×
    RtMidiInput.{h,cpp}        Virtual CoreMIDI input "MIDIops"
    RtMidiOutput.{h,cpp}       Virtual CoreMIDI output "MIDIops Clock"
    font5x7.h                  Embedded ASCII bitmap font
    main.cpp                   SDL event loop

docs/screenshots/           Simulator screenshots used in this README
scripts/                    Helper bash + Python (build, version-inject, etc.)
photos/                     Personal wiring photos + clef references (NOT tracked)
platformio.ini              Two environments: teensy41 and native
Makefile                    Thin wrapper around pio + helpers
HARDWARE.md                 Canonical hardware reference
ASSEMBLY.md                 Beginner-friendly build walkthrough
CLAUDE.md                   Project conventions for AI assistants
```
