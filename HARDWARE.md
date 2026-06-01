# Hardware reference

The bill of materials, every Teensy GPIO assignment, and the wiring tables
for each module on the board. Whenever a pin moves, a component is swapped,
or a new peripheral is wired in, this file MUST be updated in the same
commit. See `ASSEMBLY.md` for the step-by-step build walkthrough.

## Bill of materials

| # | Component | Identifier | Notes | Vendor / source |
|--:|-----------|------------|-------|-----------------|
| 1 | Teensy 4.1 microcontroller | PJRC Teensy 4.1 | ARM Cortex-M7 600 MHz, headers pre-soldered | PJRC, distributors |
| 2 | 2.8" TFT SPI display | ILI9341, "2.8 TFT SPI 240x320 V1.2" red PCB | 240x320, integrated XPT2046 touch (unused) and SD slot (unused) | AliExpress / Botland |
| 3 | Latching panel switch with LED (×3) | DFRobot DFR0789 Gravity LED Switch | Set of 5 colors. First one (PANEL) enters / leaves the chord-mapping editor; second + third (BTN2, BTN3) are wired but unmapped — visible in the Debug view. LED on each mirrors its mechanical state | DFRobot, distributors |
| 4 | Rotary encoder with push button (×5) | Keyes KY-040 | 30 detents/rev, 4 quadrature transitions per detent, integrated SW. Channel (#1), BPM (#2), View (#3) are wired with real actions; #4 and #5 are wired but unmapped — visible in the Debug view | Generic Arduino kit module |
| 5 | Full-size breadboard | MB-102 (830 tie points) | Two halves with center channel + 4 power rails. **The power rails on the MB-102 are split in the middle** — see "Power and signal levels" below | Any electronics shop |
| 6 | Dupont jumper wires | M-M and M-F, ~15 cm | Need ~60: 9 for display, 3 per DFR0789 × 3, 5 per encoder × 5, 4 for power-rail bridging (split halves on both edges), a few spares | Any electronics shop |
| 7 | USB-Micro cable | Data-capable | Power + flashing + USB-MIDI from the Teensy to the Mac | Any |

## Teensy 4.1 pin assignment

This is the single source of truth. The pin numbers come from
`platform/teensy/main.cpp` constants (`kPin*`).

| Teensy pin | Role | Connected to | Direction | Code constant |
|-----------:|------|--------------|-----------|---------------|
| 3.3V       | Power supply (multiple pins) | Breadboard + rail | OUT (regulator) | — |
| GND        | Ground (multiple pins) | Breadboard − rail | — | — |
| 2          | Mapping-mode switch signal | DFR0789 `SW` | IN, INPUT_PULLUP, **active HIGH** (latched closed = mapping editor active) | `kPinMonitorButton` |
| 3          | Channel encoder shaft button | KY-040 #1 `SW` | IN, INPUT_PULLUP, **active LOW** | `kPinEncoderSw` |
| 4          | Channel encoder A (clock) | KY-040 #1 `CLK` | IN, INPUT_PULLUP, interrupt-driven | `kPinEncoderClk` |
| 5          | Channel encoder B (data) | KY-040 #1 `DT`  | IN, INPUT_PULLUP, interrupt-driven | `kPinEncoderDt` |
| 8          | Display reset | ILI9341 `RESET` | OUT | `kPinTftRst` |
| 9          | Display data/command | ILI9341 `DC` (a.k.a. `RS`) | OUT | `kPinTftDc` |
| 10         | Display chip select | ILI9341 `CS` | OUT | `kPinTftCs` |
| 11         | SPI MOSI (hardware) | ILI9341 `SDI`/`MOSI` | OUT | (Arduino fixed) |
| 12         | SPI MISO (hardware) | ILI9341 `SDO`/`MISO` | IN  | (Arduino fixed; optional) |
| 13         | SPI clock (hardware) | ILI9341 `SCK` | OUT | (Arduino fixed) |
| 14         | BPM encoder A (clock) | KY-040 #2 `CLK` | IN, INPUT_PULLUP, interrupt-driven | `kPinBpmEncoderClk` |
| 15         | BPM encoder B (data)  | KY-040 #2 `DT`  | IN, INPUT_PULLUP, interrupt-driven | `kPinBpmEncoderDt`  |
| 16         | BPM encoder shaft button | KY-040 #2 `SW` | IN, INPUT_PULLUP, **active LOW** — in normal mode reserved (no-op); in mapping mode cycles through saved chord mappings | `kPinBpmEncoderSw`  |
| 17         | View encoder A (clock) | KY-040 #3 `CLK` | IN, INPUT_PULLUP, interrupt-driven | `kPinViewEncoderClk` |
| 18         | View encoder B (data)  | KY-040 #3 `DT`  | IN, INPUT_PULLUP, interrupt-driven | `kPinViewEncoderDt`  |
| 19         | View encoder shaft button | KY-040 #3 `SW` | IN, INPUT_PULLUP, **active LOW** — currently reserved (no-op), planned "home / reset to Monitor view" | `kPinViewEncoderSw`  |
| 0          | Encoder #4 shaft button | KY-040 #4 `SW`  | IN, INPUT_PULLUP, **active LOW** — wired but unmapped; surfaced in the Debug view. Pin 0 is also Serial1 RX (unused). | `kPinEnc4Sw`  |
| 6          | Encoder #4 A (clock) | KY-040 #4 `CLK` | IN, INPUT_PULLUP, interrupt-driven | `kPinEnc4Clk` |
| 7          | Encoder #4 B (data)  | KY-040 #4 `DT`  | IN, INPUT_PULLUP, interrupt-driven | `kPinEnc4Dt`  |
| 20         | Encoder #5 A (clock) | KY-040 #5 `CLK` | IN, INPUT_PULLUP, interrupt-driven | `kPinEnc5Clk` |
| 21         | Encoder #5 B (data)  | KY-040 #5 `DT`  | IN, INPUT_PULLUP, interrupt-driven | `kPinEnc5Dt`  |
| 22         | Encoder #5 shaft button | KY-040 #5 `SW` | IN, INPUT_PULLUP, **active LOW** — wired but unmapped; surfaced in the Debug view | `kPinEnc5Sw` |
| 1          | Latching button #2 signal | DFR0789 `SW` | IN, INPUT_PULLUP, **active HIGH** — wired but unmapped; surfaced in the Debug view as BTN2. Pin 1 is also Serial1 TX (unused). | `kPinLatch2` |
| 23         | Latching button #3 signal | DFR0789 `SW` | IN, INPUT_PULLUP, **active HIGH** — wired but unmapped; surfaced in the Debug view as BTN3 | `kPinLatch3` |

That's the full set of long-edge GPIOs the device will use. Adding
a 9th input would require soldering a header to one of the bottom
pads (pin 24 / 25 etc.) — out of scope for the current breadboard
layout.

Pins 11, 12, 13 are the dedicated hardware-SPI lines on Teensy 4.1 and
cannot be relocated. Every other long-edge pin (2–10, 14–23, plus 0,
1, 6, 7) is a free choice and can be moved by editing the `kPin*`
constants at the top of `platform/teensy/main.cpp`.

Physical breadboard layout:

* **Edge A (left, pins 0–12)** — top to bottom: `Enc4 SW` (0), `BTN2 S`
  (1), `PANEL S` (2), `Channel SW/CLK/DT` (3–5), `Enc4 CLK/DT` (6–7),
  display `RST/DC/CS/MOSI/MISO/SCK` (8–13).
* **Edge B (right, pins 13–23)** — bottom to top: `BPM CLK/DT/SW`
  (14–16), `View CLK/DT/SW` (17–19), `Enc5 CLK/DT/SW` (20–22),
  `BTN3 S` (23).

The two "mode" encoders (BPM + View) sit together on the right edge
so they're within thumb reach; the channel encoder is on the left
edge so it doesn't crowd them. Enc4 + Enc5 + BTN2 + BTN3 are the
debug-only spares — they fill the remaining long-edge pins.

## Module wiring

### ILI9341 2.8" SPI display

The 14-pin header on the long edge of the module carries — left to right
as you read the silkscreen on the back:

```
VCC  GND  CS  RESET  DC  SDI(MOSI)  SCK  LED  SDO(MISO)  T_CLK  T_CS  T_DIN  T_DO  T_IRQ
```

| Display pin | Goes to | Notes |
|-------------|---------|-------|
| **VCC**     | 3.3V rail | Logic supply (the panel itself runs on 3.3V) |
| **GND**     | − rail | |
| **CS**      | Teensy pin 10 | |
| **RESET**   | Teensy pin 8 | |
| **DC** / **RS** | Teensy pin 9 | |
| **SDI** / **MOSI** | Teensy pin 11 | Keep this wire short (≤ 15 cm) for clean SPI at default 30 MHz |
| **SCK**     | Teensy pin 13 | Same — keep short |
| **LED**     | 3.3V rail | Backlight; most red-PCB V1.2 modules have an on-board series resistor, so direct to 3.3V is safe |
| **SDO** / **MISO** | Teensy pin 12 | Optional for the basic monitor (no read-back). Keep wired if you may add the on-board SD slot or XPT2046 touch later |
| **T_CLK**, **T_CS**, **T_DIN**, **T_DO**, **T_IRQ** | leave disconnected | XPT2046 touch controller — unused in milestone 1 |

### DFRobot DFR0789 Gravity LED switch (chord-mapping mode)

Three-pin Gravity connector on the back. The switch latches mechanically:
push it once to lock down (LED on), push it again to release (LED off).
With Teensy's INPUT_PULLUP the signal is **HIGH when latched closed** and
LOW when latched open. The firmware mirrors the switch's mechanical
state into `MidiMonitorApp::mappingMode_`, so the panel LED and the
chord-mapping editor are always in sync: LED on = editor visible,
LED off = normal monitoring.

| Switch pin | Goes to |
|------------|---------|
| **G** (GND) | − rail |
| **V** (3.3V) | + rail (powers the built-in LED) |
| **S** (signal) | Teensy pin 2 |

### KY-040 #1 — Channel encoder

Five-pin header along the bottom edge of the module: `GND, +, SW, DT, CLK`
(read left-to-right with the shaft facing you).

| Encoder pin | Goes to | Notes |
|-------------|---------|-------|
| **GND** | − rail | |
| **+** | 3.3V rail | Powers the on-board 10 kΩ pull-ups on CLK and DT |
| **SW** | Teensy pin 3 | Active-LOW momentary push button (press in on the shaft) — in normal mode fires `MidiMonitorApp::restart()` so you can replay the boot splash without unplugging; in mapping mode cycles the edit's chord direction (BLOCK / UP / DOWN) |
| **DT** | Teensy pin 5 | Quadrature data line |
| **CLK** | Teensy pin 4 | Quadrature clock line |

If clockwise rotation decreases the channel instead of increasing it, swap
the constructor argument order in `platform/teensy/main.cpp`
(`TeensyEncoder(kPinEncoderDt, kPinEncoderClk)` ↔ `(kPinEncoderClk, kPinEncoderDt)`).
No rewiring required.

### KY-040 #2 — BPM encoder

Same module as #1 (identical pinout on the silkscreen). Sits on the
opposite long edge of the Teensy from the channel encoder.

| Encoder pin | Goes to | Notes |
|-------------|---------|-------|
| **GND** | − rail | |
| **+** | 3.3V rail | |
| **SW** | Teensy pin 16 | Active-LOW. In normal mode currently reserved (no-op — view cycling was moved to the dedicated view encoder). In mapping mode browses through saved chord mappings. The MIDI panic (release stuck notes) happens automatically on CC 120 / CC 123. |
| **DT** | Teensy pin 15 | |
| **CLK** | Teensy pin 14 | |

Range: 30..300 BPM, clamped at both ends. One detent = ±1 BPM. The MIDI
Clock master always runs — there is no transport start/stop yet — so
downstream gear that follows clock will lock to whatever BPM is showing
in the header.

Direction reversed? Swap the constructor arguments in
`platform/teensy/main.cpp` exactly as for the channel encoder.

### KY-040 #3 — View encoder

Third KY-040, identical module to the other two. Sits on the bottom
edge of the breadboard next to the BPM encoder so the two "mode"
knobs are within thumb reach.

| Encoder pin | Goes to | Notes |
|-------------|---------|-------|
| **GND** | − rail | |
| **+** | 3.3V rail | |
| **SW** | Teensy pin 19 | Active-LOW. Currently reserved (no-op); planned "home / jump to Monitor view" action. |
| **DT** | Teensy pin 18 | Quadrature data line. |
| **CLK** | Teensy pin 17 | Quadrature clock line. |

Rotation cycles the display through **Monitor → BigBpm → Notation →
Debug → Monitor**. CW advances forward, CCW goes backward. One detent =
±1 view. No-op while the panel switch is ON (the mapping editor
re-purposes both other encoders, but the view encoder is simply
ignored in that mode).

Direction reversed? Swap the constructor arguments in
`platform/teensy/main.cpp` exactly as for the channel encoder.

### KY-040 #4 — Spare encoder (debug-only)

Fourth KY-040, identical module. Wired up but no app-level action
attached yet; rotation and shaft button only update the Debug view's
`ENC4` and `ENC4sw` rows (last-delta, total, press count, age
highlight).

| Encoder pin | Goes to | Notes |
|-------------|---------|-------|
| **GND** | − rail | |
| **+** | 3.3V rail | |
| **SW** | Teensy pin 0 | Active-LOW. Pin 0 is also Serial1 RX — we don't use hardware serial, so this is a normal GPIO. |
| **DT** | Teensy pin 7 | Quadrature data line. |
| **CLK** | Teensy pin 6 | Quadrature clock line. |

### KY-040 #5 — Spare encoder (debug-only)

Fifth KY-040, identical module. Same role as #4 — purely a debug
target until a real action is assigned. Surfaces in the Debug view
as `ENC5` and `ENC5sw`.

| Encoder pin | Goes to | Notes |
|-------------|---------|-------|
| **GND** | − rail | |
| **+** | 3.3V rail | |
| **SW** | Teensy pin 22 | Active-LOW. |
| **DT** | Teensy pin 21 | Quadrature data line. |
| **CLK** | Teensy pin 20 | Quadrature clock line. |

### DFR0789 #2 + #3 — Spare latching buttons (debug-only)

Two extra DFR0789 latches, same module as the PANEL switch in section
"DFRobot DFR0789 Gravity LED switch". Wired but unmapped; their
latched state + toggle counter shows up as `BTN2` / `BTN3` in the
Debug view.

| Switch pin | BTN2 goes to | BTN3 goes to |
|------------|---------------|---------------|
| **G** (GND) | − rail | − rail |
| **V** (3.3V) | + rail (powers the LED) | + rail |
| **S** (signal) | Teensy pin **1** | Teensy pin **23** |

Important: the `G` pin **must** have an electrically continuous path
back to the Teensy's `GND` pin. On the MB-102 breadboard the power
rails are split in the middle (see "Power and signal levels" below),
so a DFR0789 placed past the split needs either (a) a jumper bridging
the two halves of the `−` rail, or (b) a direct GND wire from the
nearest Teensy `GND`. Without it the `S` pin floats HIGH forever and
the button looks "stuck ON, never toggles" in the Debug view.

## Power and signal levels

- **All logic is 3.3 V.** Teensy 4.1 GPIO is **not** 5 V tolerant — do not
  feed any 5 V signals back into a Teensy pin.
- The Teensy's on-board 3.3 V LDO sources up to 250 mA. The display
  backlight is the heaviest draw (~30 mA); the rest of the components
  are < 5 mA each. Plenty of headroom over USB.
- The breadboard's two power rails are tied to Teensy's `3.3V` and `GND`
  pins (one wire each from the Teensy to the nearest rail). Components
  then tap those rails locally.
- **MB-102 split-rail gotcha.** On most MB-102 boards the long `+` and
  `−` rails are not one continuous strip — they're broken in the middle
  (around hole 30) into two electrically independent halves. The visual
  cue is a tiny break in the coloured stripe printed along the rail.
  Anything plugged into the half that doesn't have a wire back to the
  Teensy gets no power / no ground. Symptom on the DFR0789 wired past
  the split: `S` reads HIGH forever, BTN row in Debug view shows
  toggles=1 (single boot transition) and never changes. Fix: bridge
  the two halves with a single jumper across the split — once on `+`,
  once on `−`, on each long edge of the board.

## Wiring overview

The Teensy 4.1 has two long edges of header pins. With the USB
connector pointing up, "edge A" runs along the left (`GND`, `0..12`,
`3.3V` from top to bottom) and "edge B" runs along the right (`VIN`,
`GND`, `3.3V`, `23..13` from top to bottom). Every used GPIO and the
module it terminates at:

```
                  ┌──────────────────────────┐
                  │       Teensy 4.1         │
                  │           [USB]          │
                  │   GND     ────     VIN   │
   Enc4  SW   <-- │   0                23    │ --> BTN3  S
   BTN2  S    <-- │   1                22    │ --> Enc5  SW
   PANEL S    <-- │   2                21    │ --> Enc5  DT
   Ch    SW   <-- │   3                20    │ --> Enc5  CLK
   Ch    CLK  <-- │   4                19    │ --> View  SW
   Ch    DT   <-- │   5                18    │ --> View  DT
   Enc4  CLK  <-- │   6                17    │ --> View  CLK
   Enc4  DT   <-- │   7                16    │ --> BPM   SW
   TFT   RST  --> │   8                15    │ --> BPM   DT
   TFT   DC   --> │   9                14    │ --> BPM   CLK
   TFT   CS   --> │  10                13    │ --> TFT   SCK
   TFT   MOSI --> │  11                12    │ <-- TFT   MISO
                  │   3.3V    ────    GND    │
                  └──────────────────────────┘
```

`-->` = Teensy drives the line (output). `<--` = Teensy reads the
line (input). `Ch` = KY-040 #1 (channel encoder).

**Rebuild map.** If the wiring gets pulled apart, this is the table
to put back together — one row per Teensy GPIO that's actually used,
top of edge A to bottom of edge B:

| Pin | Module → label on module |
|----:|---------------------------|
| `GND` (any) | breadboard `−` rail (left half) + jumper to `−` rail (right half) — bridge the MB-102 split |
| `3.3V` (any) | breadboard `+` rail (left half) + jumper to `+` rail (right half) — bridge the MB-102 split |
| 0   | KY-040 #4 (Enc4) — `SW` |
| 1   | DFR0789 #2 (BTN2) — `S` |
| 2   | DFR0789 #1 (PANEL) — `S` |
| 3   | KY-040 #1 (Channel) — `SW` |
| 4   | KY-040 #1 (Channel) — `CLK` |
| 5   | KY-040 #1 (Channel) — `DT` |
| 6   | KY-040 #4 (Enc4) — `CLK` |
| 7   | KY-040 #4 (Enc4) — `DT` |
| 8   | ILI9341 — `RESET` |
| 9   | ILI9341 — `DC` |
| 10  | ILI9341 — `CS` |
| 11  | ILI9341 — `SDI` / `MOSI` (hardware SPI, fixed pin) |
| 12  | ILI9341 — `SDO` / `MISO` (hardware SPI, fixed pin; optional) |
| 13  | ILI9341 — `SCK` (hardware SPI, fixed pin) |
| 14  | KY-040 #2 (BPM) — `CLK` |
| 15  | KY-040 #2 (BPM) — `DT` |
| 16  | KY-040 #2 (BPM) — `SW` |
| 17  | KY-040 #3 (View) — `CLK` |
| 18  | KY-040 #3 (View) — `DT` |
| 19  | KY-040 #3 (View) — `SW` |
| 20  | KY-040 #5 (Enc5) — `CLK` |
| 21  | KY-040 #5 (Enc5) — `DT` |
| 22  | KY-040 #5 (Enc5) — `SW` |
| 23  | DFR0789 #3 (BTN3) — `S` |

On top of the per-pin signal wires, **every KY-040 and every DFR0789
needs both `+` (to the 3.3 V rail) and `GND` (to the `−` rail)**.
That's a hard requirement, including for the debug-only modules:

* DFR0789 with no `GND` connection: `S` floats HIGH forever via the
  Teensy's internal pull-up; the button looks "stuck ON, toggles=1"
  in the Debug view (one boot transition, then nothing). This is
  exactly what the MB-102 split-rail traps you with — see the
  warning in the previous section.
* DFR0789 with no `+` connection: switch logic still works, but the
  indicator LED in the cap stays dark.
* KY-040 with no `+` connection: the on-board 10 kΩ pull-ups on CLK
  and DT are unpowered; the encoder will misread heavily or do
  nothing at all.
* KY-040 with no `GND` connection: shaft button can't pull `SW` to
  ground, so press detection is dead.

Display caveats: the LED pin (display backlight) goes to the `+`
rail too, separately from `VCC` — they're two different pins on the
ILI9341 module header. If LED is missing the backlight stays off and
the display looks completely dark even though the SPI bus is fine.
