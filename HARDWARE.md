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
| 3 | Latching panel switch with LED | DFRobot DFR0789 Gravity LED Switch | Set of 5 colors; we use one for the monitor on/off toggle | DFRobot, distributors |
| 4 | Rotary encoder with push button (×2) | Keyes KY-040 | 30 detents/rev, 4 quadrature transitions per detent, integrated SW. We use two: one for channel selection, one for BPM | Generic Arduino kit module |
| 5 | Full-size breadboard | MB-102 (830 tie points) | Two halves with center channel + 4 power rails | Any electronics shop |
| 6 | Dupont jumper wires | M-M and M-F, ~15 cm | Need ~25: 9 for display, 3 for monitor button, 5 per encoder × 2, 2 for power rails, a few spares | Any electronics shop |
| 7 | USB-Micro cable | Data-capable | Power + flashing + USB-MIDI from the Teensy to the Mac | Any |

## Teensy 4.1 pin assignment

This is the single source of truth. The pin numbers come from
`platform/teensy/main.cpp` constants (`kPin*`).

| Teensy pin | Role | Connected to | Direction | Code constant |
|-----------:|------|--------------|-----------|---------------|
| 3.3V       | Power supply (multiple pins) | Breadboard + rail | OUT (regulator) | — |
| GND        | Ground (multiple pins) | Breadboard − rail | — | — |
| 2          | Monitor switch signal | DFR0789 `SW` | IN, INPUT_PULLUP, **active HIGH** (latched closed) | `kPinMonitorButton` |
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
| 16         | BPM encoder shaft button | KY-040 #2 `SW` | IN, INPUT_PULLUP, **active LOW** — toggles `MidiMonitorApp` between monitor view and the big-BPM focus view | `kPinBpmEncoderSw`  |

Pins 11, 12, 13 are the dedicated hardware-SPI lines on Teensy 4.1 and
cannot be relocated. Pins 2, 3, 4, 5, 8, 9, 10, 14, 15, 16 are all free
choices and can be moved by editing the `kPin*` constants at the top of
`platform/teensy/main.cpp`.

The two encoders sit on physically opposite long edges of the Teensy
(channel encoder on pins 3–5, BPM encoder on pins 14–16) so they don't
crowd the same strip of breadboard.

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

### DFRobot DFR0789 Gravity LED switch (monitor on/off)

Three-pin Gravity connector on the back. The switch latches mechanically:
push it once to lock down (LED on), push it again to release (LED off).
With Teensy's INPUT_PULLUP the signal is **HIGH when latched closed** and
LOW when latched open.

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
| **SW** | Teensy pin 3 | Active-LOW momentary push button (press in on the shaft) — fires `MidiMonitorApp::restart()` so you can replay the boot splash without unplugging |
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
| **SW** | Teensy pin 16 | Active-LOW. Short press toggles the display between the monitor view (header + worms + keyboard) and a focus screen showing the current BPM as one large number. The MIDI panic (release stuck notes) happens automatically on CC 120 / CC 123 — there is no dedicated hardware panic button. |
| **DT** | Teensy pin 15 | |
| **CLK** | Teensy pin 14 | |

Range: 30..300 BPM, clamped at both ends. One detent = ±1 BPM. The MIDI
Clock master always runs — there is no transport start/stop yet — so
downstream gear that follows clock will lock to whatever BPM is showing
in the header.

Direction reversed? Swap the constructor arguments in
`platform/teensy/main.cpp` exactly as for the channel encoder.

## Power and signal levels

- **All logic is 3.3 V.** Teensy 4.1 GPIO is **not** 5 V tolerant — do not
  feed any 5 V signals back into a Teensy pin.
- The Teensy's on-board 3.3 V LDO sources up to 250 mA. The display
  backlight is the heaviest draw (~30 mA); the rest of the components
  are < 5 mA each. Plenty of headroom over USB.
- The breadboard's two power rails are tied to Teensy's `3.3V` and `GND`
  pins (one wire each from the Teensy to the nearest rail). Components
  then tap those rails locally.

## Wiring overview (ASCII)

```
                 ┌─────────────────────────────────────────┐
                 │            Teensy 4.1                   │
                 │                                         │
   USB ──────────┤ USB-Micro                               │
                 │                                         │
                 │ 3.3V ──┐  GND ──┐                       │
                 │        │        │                       │
                 │   pin 2◀────────┼───┐                   │
                 │   pin 3◀────────┼───┼──┐                │
                 │   pin 4◀────────┼───┼──┼──┐             │
                 │   pin 5◀────────┼───┼──┼──┼──┐          │
                 │                 │   │  │  │  │          │
                 │   pin 8──▶──────┼───┼──┼──┼──┼─┐        │
                 │   pin 9──▶──────┼───┼──┼──┼──┼─┼┐       │
                 │   pin 10─▶──────┼───┼──┼──┼──┼─┼┼┐      │
                 │   pin 11─▶──────┼───┼──┼──┼──┼─┼┼┼┐     │
                 │   pin 12─◀──────┼───┼──┼──┼──┼─┼┼┼┼┐    │
                 │   pin 13─▶──────┼───┼──┼──┼──┼─┼┼┼┼┼┐   │
                 └─────────────────┼───┼──┼──┼──┼─┼┼┼┼┼┼───┘
                                   │   │  │  │  │ ││││││
       Breadboard + rail (3.3V) ───┘   │  │  │  │ ││││││
       Breadboard − rail (GND)  ───────┘  │  │  │ ││││││
                                          │  │  │ ││││││
       DFR0789  V ◀──┐   S ◀───────────────┘  │  │ ││││││
                GND ◀┼┐                       │  │ ││││││
                    └┼┼───────[ + rail ]      │  │ ││││││
       KY-040    + ◀─┼┘                       │  │ ││││││
                GND ◀┘                        │  │ ││││││
                 SW ◀──────────────────────────┘  │ ││││││
                 DT ◀─────────────────────────────┘ ││││││
                CLK ◀────────────────────[from p4]──┘│││││
                                                     │││││
       ILI9341 VCC ◀──[ + rail ]                     │││││
                GND ◀──[ − rail ]                    │││││
              RESET ◀────────────────────────────────┘││││
                 DC ◀─────────────────────────────────┘│││
                 CS ◀──────────────────────────────────┘││
                MOSI◀───────────────────────────────────┘│
                MISO▶────────────────────────────────────┘
                SCK ◀───────────────────────[from p13]
                LED ◀──[ + rail ]
              T_xxx     (unconnected)
```

Arrow direction = signal flow direction.
