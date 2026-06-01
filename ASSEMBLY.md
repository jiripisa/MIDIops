# Assembly walkthrough

Beginner-friendly step-by-step build of the MIDIops hardware monitor.
Follow these steps in order. If any wiring detail looks wrong on the
breadboard, fall back to `HARDWARE.md` which is the canonical reference
for pins and connections.

When this document and `HARDWARE.md` disagree, **the code constants in
`platform/teensy/main.cpp` win**. Both docs MUST be updated when those
constants change.

## 0. What you'll end up with

A Teensy 4.1 sitting on a full-size breadboard, driving an ILI9341 2.8"
TFT display through SPI, with:
- A latching panel switch (DFRobot DFR0789) — switch ON enters the
  chord-mapping editor, LED mirrors the state
- A rotary encoder (KY-040 #1) controlling the monitored MIDI channel;
  shaft button restarts the app (or cycles chord direction in mapping
  mode)
- A second rotary encoder (KY-040 #2) setting the BPM that the device
  broadcasts as a MIDI Clock master, plus the chord engine's
  NoteOn/NoteOff stream, to any DAW listening on the `MIDIops` USB
  MIDI device; shaft button browses through saved mappings while in
  mapping mode
- A third rotary encoder (KY-040 #3) cycling between the device's three
  display views (Monitor, Big-BPM, Notation); shaft button reserved
  for a future "home" action

## 1. Tools and parts

Check `HARDWARE.md` for the complete bill of materials. The short list:

- Teensy 4.1 with male headers already soldered along both long edges
- ILI9341 2.8" SPI display (red PCB, 14-pin header)
- One DFR0789 panel switch
- Three KY-040 rotary encoders (channel + BPM + view)
- Full-size breadboard (MB-102 or equivalent)
- About 30 dupont jumper wires (mix of M-M for breadboard rails and
  signal hops, M-F for connecting modules with their own headers)
- USB-Micro data cable

Software side — see `README.md` for `brew install` commands. In short:
PlatformIO + the Teensy Loader app, both available via Homebrew.

## 2. Breadboard primer

If you've never used a breadboard, this is the model you need:

```
┌────────────────────────────────────────────────┐
│  +  ─────────────────────────────────────────  │  ← + rail (one long wire)
│  −  ─────────────────────────────────────────  │  ← − rail (one long wire)
│  A  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  │  ┐ A-E in one column
│  B  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  │  │ are internally
│  C  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  │  ├ joined
│  D  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  │  │
│  E  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  │  ┘
│  ═══════════ CENTER CHANNEL (no join across) ═  │  ← Teensy straddles here
│  F  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  │  ┐
│  G  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  │  ├ F-J in one column
│  H  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  │  │ are internally
│  I  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  │  ┤ joined
│  J  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  ⋅  │  ┘
│  +  ─────────────────────────────────────────  │
│  −  ─────────────────────────────────────────  │
└────────────────────────────────────────────────┘
```

Three rules to remember:
1. The five holes in one column on the same half (A-E together, or F-J
   together) are internally connected.
2. The center channel breaks the connection — A and F in the same column
   are **not** connected.
3. The + and − rails are one long wire each. Anything you tie to a + rail
   hole has 3.3 V available all along that rail.

## 3. Place the Teensy

Position the Teensy 4.1 across the center channel so half its pins land
in the A-E rows and the other half in the F-J rows. Push it down evenly
until every pin is seated. The USB connector should hang off one end of
the breadboard so you can plug in a cable.

Orient it however you like, but **remember which row carries which
pins**. The pinout label card that comes with the Teensy (the one with
"Welcome to Teensy 4.1") is your friend.

Relevant pins for this build (see `HARDWARE.md` for the master table):

- One long edge: `GND`, `0..12`, `3.3V` — pins 2, 3, 4, 5, 8, 9, 10, 11,
  12 live here
- Other long edge: `VIN`, `GND`, `3.3V`, `23..13` — pin 13 lives here

## 4. Run the power rails

You need 3.3 V and GND distributed along the breadboard:

1. **Red wire**: from any Teensy `3.3V` pin to the **+ rail** on the
   nearest side of the breadboard.
2. **Black wire**: from any Teensy `GND` pin to the **− rail** on the
   same side.

The two `3.3V` and several `GND` pins on the Teensy are all internally
tied, so pick whichever is convenient. You don't need to bridge the rails
on opposite sides of the board for this build.

## 5. Wire the display

The ILI9341 module has a 14-pin header along one of its long edges.
Each module is labeled on the silkscreen; the order on the red-PCB V1.2
variant is:

```
VCC  GND  CS  RESET  DC  SDI(MOSI)  SCK  LED  SDO(MISO)  T_CLK  T_CS  T_DIN  T_DO  T_IRQ
```

Use **9 M-F jumper wires**. The female end pushes onto the display pin
header; the male end goes into the breadboard column that connects to the
target Teensy pin. (See `HARDWARE.md` for the full table.)

Key dos and don'ts:

- **Keep MOSI (pin 11) and SCK (pin 13) wires short.** They carry the
  SPI clock and data at 30 MHz; long, sloppy dupont lines pick up noise
  and the display will glitch or stay blank. ≤ 15 cm is a good ceiling.
- **LED goes to the 3.3 V rail.** That's the backlight. If you forget
  this wire, the display will look completely black even though
  everything else is correct.
- **T_CLK, T_CS, T_DIN, T_DO, T_IRQ are the touch controller — leave
  them disconnected** for milestone 1.

## 6. Wire the mapping switch (DFR0789)

The DFR0789 has a 3-wire Gravity connector (`G`, `V`, `S`). It's a
latching switch — push it once to lock down, push again to release. The
LED inside the cap lights up while the switch is latched closed.

Three jumpers:

| Switch pin | Goes to |
|------------|---------|
| `G`        | − rail (GND) |
| `V`        | + rail (3.3 V, powers the LED) |
| `S`        | Teensy pin **2** |

The firmware mirrors the switch's mechanical position into the
mapping-mode flag, so the LED on the panel and the chord-mapping
editor state always match: latched closed = editor active, released =
back to normal monitoring. See section 7d for the editor workflow.

## 7. Wire the channel encoder (KY-040 #1)

Five pins along the bottom edge, labeled `GND, +, SW, DT, CLK` on the
silkscreen:

| Encoder pin | Goes to |
|-------------|---------|
| `GND`       | − rail |
| `+`         | + rail (powers the on-board pull-ups) |
| `SW`        | Teensy pin **3** |
| `DT`        | Teensy pin **5** |
| `CLK`       | Teensy pin **4** |

`SW` triggers the app's "restart" — it re-shows the boot splash and
clears all the live state without you having to unplug USB. Handy for
testing the startup look.

If you turn the knob clockwise and the monitored channel **decreases**
instead of increases, the easiest fix is to swap the two constructor
arguments in `platform/teensy/main.cpp` (`TeensyEncoder(...)`) — see the
comment there. You can also just physically swap `CLK ↔ DT` on the
breadboard; either way works.

## 7b. Wire the BPM encoder (KY-040 #2)

Identical module as #1. Place it on the **opposite long edge** of the
Teensy from the channel encoder so the two strips of breadboard don't
get tangled.

| Encoder pin | Goes to |
|-------------|---------|
| `GND`       | − rail |
| `+`         | + rail |
| `SW`        | Teensy pin **16** |
| `DT`        | Teensy pin **15** |
| `CLK`       | Teensy pin **14** |

Range 30..300 BPM, default 120, one detent = ±1 BPM. The device sends
MIDI Clock pulses (24 per quarter note) continuously, so any DAW with
its tempo source set to `MIDIops` will follow the BPM you set with this
knob.

`SW` in normal mode is currently a no-op (view cycling moved to the
dedicated view encoder — see 7c). In mapping mode it browses through
saved chord mappings.

The MIDI panic (release stuck notes) happens automatically when the
DAW sends CC 120 (All Sound Off) or CC 123 (All Notes Off) — Ableton
emits these on transport stop — so a dedicated hardware panic button
isn't needed.

Same direction-reversal trick applies as the channel encoder.

## 7c. Wire the view encoder (KY-040 #3)

Third identical KY-040. Place it next to the BPM encoder on the
bottom edge of the breadboard so the two "mode" knobs sit together,
within thumb reach, away from the channel knob.

| Encoder pin | Goes to |
|-------------|---------|
| `GND`       | − rail |
| `+`         | + rail |
| `SW`        | Teensy pin **19** |
| `DT`        | Teensy pin **18** |
| `CLK`       | Teensy pin **17** |

Rotation cycles through three display modes:

1. **Monitor view** — per-channel coloured worms scrolling up from a piano keyboard, with held-note chord names in the header and a chord-queue strip below it.
2. **Big-BPM focus** — current tempo as one big number with a `BPM` caption.
3. **Notation view** — a grand staff (treble + bass clef) with every held note drawn as a filled note-head at its correct pitch position, including sharps and ledger lines for notes outside the staff. Held-note names appear below the staff and drift down + fade away when released.

CW rotation advances forward (Monitor → BigBpm → Notation → Monitor),
CCW goes backward. The `SW` is currently a no-op (reserved for a
future "home / jump to Monitor view" action).

Same direction-reversal trick applies as the other encoders.

## 7d. Use the panel switch for chord-mapping mode

The DFR0789 latching switch (wired in section 6) toggles between
normal operation (switch DOWN, LED off) and the **chord mapping
editor** (switch UP, LED on).

While in mapping mode:

* Send a note on the `MIDIops` MIDI input to capture it as the trigger
  for the next mapping (or load an existing mapping with that
  trigger).
* Rotate the **channel** encoder to cycle through chord types: maj /
  min / dim / aug / 7 / m7 / maj7.
* Press the **channel** shaft button to cycle the playback direction:
  BLOCK / UP / DOWN.
* Rotate the **BPM** encoder to set the gate length in MIDI ticks
  (1..96, where 24 = quarter note at 24-PPQN base).
* Press the **BPM** shaft button to browse to the next saved mapping.
* Flip the switch back DOWN to exit. All edits auto-save into the
  engine as you go — there's no separate save action.

Once back in normal mode, any incoming NoteOn matching a mapping's
trigger plays the chord on the configured output channel. Triggers
that arrive while a chord is already sounding are queued FIFO and
play in trigger order.

## 8. Pre-power checklist

Before you connect USB:

- [ ] Photograph the wiring. If something doesn't work, the picture saves
  you a lot of squinting.
- [ ] Every component's GND wire goes to the − rail, every VCC/V/+ goes
  to the + rail.
- [ ] No bare wire ends touching each other.
- [ ] No jumper straddles the center channel by accident (that would
  short two unrelated Teensy pins together).
- [ ] Pin 11 (MOSI) on the Teensy goes to the display's `SDI/MOSI`, NOT
  to `SCK`. Pin 13 (SCK) goes to the display's `SCK`, NOT to `SDI`.
  Mixing these up is the most common cause of a blank or noisy display.

## 9. First boot

1. Connect USB-Micro from the Teensy to your Mac.
2. Open a terminal in the project root:
   ```bash
   cd ~/Development/Claude/jp4midi
   make flash
   ```
3. If the Teensy doesn't enter programming mode automatically, press the
   small black button next to its USB port.
4. After a few seconds you should see:
   - A 3-second `MIDIops` synthwave splash with the commit hash and
     build timestamp overlaid at the bottom edge.
   - Then the live monitor: a dark roll area with the keyboard at the
     bottom and the header strip on top showing `CH:OMNI` (or whichever
     channel is selected).
   - The DFR0789's LED reflects the mapping-mode state — lit when the
     mapping editor is active, dark when in normal monitoring.

## 10. Smoke test

- Turn the **channel** encoder clockwise: the header should cycle
  `OMNI → 1 → 2 …`. Counter-clockwise reverses, stopping at `OMNI`.
- Press the channel-encoder shaft button: the splash should re-appear
  for 3 seconds, then resume the normal view in OMNI mode with the
  keyboard and roll empty.
- Turn the **BPM** encoder: the number in the top-right corner of the
  header should change by 1 BPM per detent (range 30..300). In Ableton,
  set the tempo source to `MIDIops` and confirm the project tempo
  follows what's showing on the display.
- Turn the **view** encoder: the screen should cycle through Monitor,
  Big-BPM (giant tempo number), and Notation (grand staff).
- Toggle the panel switch UP: the screen should show the synthwave
  `PRESS A NOTE / TO MAPPING` prompt. Send any NoteOn from your DAW
  (or from the simulator) and the editor view appears with that note
  pre-filled as the trigger. Toggle the switch DOWN to exit.
- Send MIDI from your Mac (Ableton via the IAC Driver routed to
  `MIDIops`, or the simulator's keyboard injection): coloured worms
  scroll up from the keyboard, keys light up under held notes, chord
  names appear in the header, and — if you've defined a mapping — the
  configured chord plays out on the configured output channel,
  visible in the monitor view as gray outline (ghost) worms.

## 11. When something doesn't work

- **Display completely dark, no backlight.** LED pin not on 3.3 V, or
  VCC not on 3.3 V. Check those two wires.
- **Display white, backlight on, no image.** SPI is silent. Most often
  MOSI and SCK are swapped (pin 11 ↔ pin 13). Confirm and reflash.
- **Display shows random pixels / partial garbage.** Usually CS or DC
  wrong, or SPI noise from long wires. Verify pin 9 and pin 10. Also try
  lowering the SPI clock — there's a one-line tweak in
  `platform/teensy/TeensyDisplay.cpp` (`tft.setSPISpeed(20000000)` after
  `tft.begin()`).
- **Knob direction reversed.** Swap the constructor arguments in
  `platform/teensy/main.cpp` (one-line fix, no rewiring).
- **Mapping switch toggles the wrong way.** Check the polarity. The
  `TeensyButton` constructor in `platform/teensy/main.cpp` defaults to
  `activeHigh = true` for the DFR0789; if your latching switch has the
  inverse wiring, pass `false`.
- **Ableton still shows the device as "Teensy MIDI" after re-flash.**
  CoreMIDI caches device names. Delete the stale entry in `Audio MIDI
  Setup → Show MIDI Studio`, unplug, replug. If still stale,
  `sudo killall MIDIServer`. See the git log around the `usb_names.c`
  commit for the full explanation.
