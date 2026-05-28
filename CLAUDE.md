# jp4midi — project notes for Claude

A hardware MIDI arpeggiator / sequencer for the Teensy 4.1 + 2.8" ILI9341
display. Milestone 1 (current scope): a MIDI monitor that filters by a
configurable channel and shows the last ~11 messages on screen.

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
make sim         # simulator (SDL window + virtual MIDI port "TeensyArp")
make firmware    # build the Teensy .hex
make flash       # build + upload to a connected Teensy
make clean       # blow away .pio/
```

Both environments live in `platformio.ini`. `build_src_filter` enforces the
"only one platform/ subtree per env" rule.

## Where future milestones plug in

* **5 rotary encoders** (channel select, tempo, gate, etc.): add
  `core/Encoders.h` defining an abstract `EncoderInput` with
  `bool poll(EncoderEvent&)`, plus implementations in
  `platform/teensy/TeensyEncoders.cpp` (interrupt-driven, Bounce2 or the
  `Encoder` library) and `platform/host/SdlEncoders.cpp` (map arrow keys or
  mouse wheel). `core/MidiMonitorApp` then grows a `void onEncoder(...)` method.
* **Arpeggiator engine**: pure `core/` code. Build it as `core/Arpeggiator`
  (note buffer + pattern generator) that consumes `MidiMessage` and emits
  `MidiMessage` via a new `MidiOutput` abstract interface. Teensy
  implementation uses `usbMIDI.sendNoteOn(...)`; host implementation can
  send to a CoreMIDI virtual *output* port via RtMidi.
* **Settings / UI screens**: a `core/View` hierarchy with `MidiMonitorApp`
  as the first concrete view. The main loop becomes "active view ->
  onInput() -> render()" with no platform code involved.

The current layout has been chosen specifically so none of those additions
require restructuring — only new files in `core/` and matching
implementations under `platform/teensy/` and `platform/host/`.

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

## The "listened channel" knob

There is exactly one source of truth for the listened channel:

```cpp
// core/MidiMonitorApp.h
static constexpr uint8_t kDefaultChannel = 0;  // 0 = OMNI, 1..16
```

Change this value, rebuild, done — both targets pick it up.
