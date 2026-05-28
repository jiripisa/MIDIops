# jp4midi

Hardware MIDI arpeggiator / sequencer built on a Teensy 4.1 with a 2.8"
ILI9341 SPI display, paired with a macOS simulator so the same app code can
be iterated on without flashing hardware every time.

Milestone 1 (this scaffold): a **MIDI monitor** that listens on a configurable
MIDI channel and shows the most recent messages on screen.

## Architecture, in one paragraph

`core/` is portable C++17 and never includes any platform header. It defines
the app (`MidiMonitorApp`), the message value type, and the abstract
`Display` / `MidiInput` interfaces. Two thin platform backends in
`platform/teensy/` and `platform/host/` provide concrete implementations. The
exact same `core/` sources are compiled into both binaries.

```
core/                 portable C++17, no platform headers
platform/teensy/      Teensy 4.1: ILI9341_t3 + usbMIDI
platform/host/        macOS:      SDL2       + RtMidi
```

## Prerequisites (one-time setup)

Apple Silicon Mac, Homebrew installed under `/opt/homebrew`:

```bash
brew install platformio sdl2 rtmidi pkg-config
```

PlatformIO will auto-install the Teensy toolchain and the `ILI9341_t3`
library on the first `pio run -e teensy41`.

To flash a Teensy you also need the Teensy Loader app (Homebrew cask works):

```bash
brew install --cask teensy
```

## Running the simulator

```bash
make sim
# or:
./scripts/run-sim.sh
```

A 960x720 window opens (320x240 logical pixels scaled 3x). The process
creates a **virtual CoreMIDI input port named `TeensyArp`** that any DAW or
MIDI utility can route into. Press **ESC** to quit.

Keyboard test injection (no external MIDI source required):

| Key | Note | Key | Note | Key | Note | Key | Note |
|-----|------|-----|------|-----|------|-----|------|
| C   | C4   | D   | D4   | E   | E4   | F   | F4   |
| G   | G4   | A   | A4   | B   | B4   |     |      |

Key down sends Note On (velocity 100), key up sends Note Off.

## Routing MIDI from Ableton via the macOS IAC bus

1. Open **Audio MIDI Setup** (Cmd-Space, type "Audio MIDI Setup").
2. **Window -> Show MIDI Studio**.
3. Double-click **IAC Driver**, tick **Device is online**, and make sure at
   least one bus (e.g. `Bus 1`) is listed. Apply.
4. Start the simulator (`make sim`). It announces `[RtMidi] virtual input
   port opened: "TeensyArp"` on stderr.
5. In Ableton Live -> **Preferences -> Link/Tempo/MIDI -> MIDI**, enable
   the **Track** output for `TeensyArp` (it appears once the simulator is
   running) — *or* route to `IAC Driver Bus 1` and then loop that into
   `TeensyArp` (most setups just point Ableton straight at `TeensyArp`).
6. On a Live MIDI track, set the MIDI output to `TeensyArp` / channel 1
   (or whichever channel matches `kDefaultChannel` in
   `core/MidiMonitorApp.h`). Notes you play should now appear in the
   simulator window.

## Building & flashing the Teensy 4.1

```bash
make firmware    # build only — prints the .hex path
make flash       # build + upload via teensy_loader_cli / Teensy Loader
```

Plug the Teensy in over USB **before** running `make flash`. The build sets
the USB type to **MIDI + Serial** via `-D USB_MIDI_SERIAL` in
`platformio.ini`, so the device shows up to your Mac as a class-compliant
MIDI input.

### Display wiring (ILI9341 ⇄ Teensy 4.1, hardware SPI)

The three control pins are configurable at the top of
`platform/teensy/main.cpp`; the SPI data pins are fixed by the chip.

| ILI9341 pin | Teensy 4.1 pin | Notes                          |
|-------------|---------------|--------------------------------|
| VCC         | 3.3V          | Display logic supply            |
| GND         | GND           |                                 |
| CS          | 10            | Chip Select (changeable)        |
| RESET       | 8             | Reset (changeable)              |
| D/C         | 9             | Data/Command (changeable)       |
| SDI (MOSI)  | 11            | Hardware SPI MOSI (fixed)       |
| SCK         | 13            | Hardware SPI clock (fixed)      |
| LED         | 3.3V          | Backlight (or PWM pin if dimmed)|
| SDO (MISO)  | 12            | Optional, only for touch/SD     |

## Configuring the listened channel

Edit one constant in `core/MidiMonitorApp.h`:

```cpp
static constexpr uint8_t kDefaultChannel = 0;  // 0 = OMNI, 1..16 = a channel
```

That single value is used by both the Teensy and the simulator.

## Layout

```
core/                       Portable C++17 — MUST NOT include Arduino/SDL/RtMidi headers
  MidiMessage.{h,cpp}
  Display.h
  MidiInput.h
  MidiMonitorApp.{h,cpp}

platform/
  teensy/                   Teensy 4.1 backend
    TeensyDisplay.{h,cpp}
    TeensyMidiInput.{h,cpp}
    main.cpp                (setup / loop)
  host/                     macOS simulator backend
    SdlDisplay.{h,cpp}
    RtMidiInput.{h,cpp}
    font5x7.h               (embedded ASCII bitmap font)
    main.cpp                (SDL event loop)

scripts/                    Helper bash scripts (also wrapped by Makefile)
platformio.ini              Two environments: teensy41 and native
```
