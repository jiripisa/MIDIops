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
- A latching panel switch (DFRobot DFR0789 #1, "PANEL") — switch ON
  enters the chord-mapping editor, LED mirrors the state
- A rotary encoder (KY-040 #1, "Channel") controlling the monitored
  MIDI channel; shaft button restarts the app (or cycles chord
  direction in mapping mode)
- A second rotary encoder (KY-040 #2, "BPM") setting the BPM that the
  device broadcasts as a MIDI Clock master, plus the chord engine's
  NoteOn/NoteOff stream, to any DAW listening on the `MIDIops` USB
  MIDI device; shaft button browses through saved mappings while in
  mapping mode
- A third rotary encoder (KY-040 #3, "View") cycling between the
  device's four display views (Monitor, Big-BPM, Notation, Debug);
  shaft button reserved for a future "home" action
- Two extra rotary encoders (KY-040 #4 "Enc4" + KY-040 #5 "Enc5") and
  two extra DFR0789 latching buttons (BTN2 + BTN3) — all wired up
  and visible in the Debug view but without app-level actions yet,
  reserved for future features

## 1. Tools and parts

Check `HARDWARE.md` for the complete bill of materials. The short list:

- Teensy 4.1 with male headers already soldered along both long edges
- ILI9341 2.8" SPI display (red PCB, 14-pin header)
- Three DFR0789 panel switches (PANEL + BTN2 + BTN3)
- Five KY-040 rotary encoders (Channel + BPM + View + Enc4 + Enc5)
- Full-size breadboard (MB-102 or equivalent)
- About 60 dupont jumper wires (mix of M-M for breadboard rails and
  signal hops, M-F for connecting modules with their own headers).
  Budget: 9 for display, 3 per DFR0789 × 3, 5 per KY-040 × 5,
  4 for power-rail bridging, a few spares.
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
3. The + and − rails look like one long wire each, **but on the MB-102
   they're split in half** in the middle of the board (around hole 30
   — look for a tiny break in the coloured stripe printed along each
   rail). The two halves are electrically independent until you bridge
   them with a jumper. So if you wire a component into the half that
   doesn't have a power wire back to the Teensy, it gets **no power
   and no ground** — silent failure. We work around this by adding two
   short bridge jumpers along each long edge in step 4.

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

You need 3.3 V and GND distributed along **both halves** of **both
long edges** of the breadboard (4 rail strips total, since the MB-102
splits each rail in the middle).

The minimum reliable setup is 4 wires and 4 bridges:

1. **Red wire**: any Teensy `3.3V` pin → nearest `+` rail half.
2. **Black wire**: any Teensy `GND` pin → nearest `−` rail half.
3. **`+` bridge × 2**: one short jumper on each long edge of the
   board, hopping across the central rail split (around hole 30) so
   the second half also carries 3.3 V.
4. **`−` bridge × 2**: same idea on each `−` rail.

(You can skip the bridges to the rails on the opposite long edge if
no module on that edge needs power — but in this build every long
edge has modules, so wire all four rail strips.)

The two `3.3V` and several `GND` pins on the Teensy are all
internally tied, so pick whichever is convenient. The bridges across
the split take a single ~3 cm jumper each.

**Why this matters.** If you skip a bridge and then plug a DFR0789 or
KY-040 into the un-bridged rail half, the module gets no `GND`. The
firmware compensates with internal pull-ups, so the pin reads HIGH
forever and the button looks "stuck ON" in the Debug view (toggles=1
at boot, then no change). Same root cause for an encoder whose `+`
isn't reaching power: the on-board pull-ups die and rotation reads
as noise. Always continuity-check rails before powering on.

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

## 7d. Wire the spare encoder #4 (KY-040 #4)

Identical KY-040 module to the other three. No app-level action
attached yet — wired purely so the Debug view can show that the
hardware is alive, and so a future feature can adopt it without
re-soldering.

| Encoder pin | Goes to |
|-------------|---------|
| `GND`       | − rail |
| `+`         | + rail |
| `SW`        | Teensy pin **0** (also Serial1 RX — unused, so it's a normal GPIO) |
| `DT`        | Teensy pin **7** |
| `CLK`       | Teensy pin **6** |

Rotation increments the `ENC4` total in the Debug view; the shaft
button increments the `ENC4sw` press counter. That's the whole
behaviour today.

## 7e. Wire the spare encoder #5 (KY-040 #5)

Identical to #4. Surfaces in the Debug view as `ENC5` / `ENC5sw`.

| Encoder pin | Goes to |
|-------------|---------|
| `GND`       | − rail |
| `+`         | + rail |
| `SW`        | Teensy pin **22** |
| `DT`        | Teensy pin **21** |
| `CLK`       | Teensy pin **20** |

## 7f. Wire the spare latching buttons #2 and #3 (DFR0789 ×2)

Two more DFR0789 modules, same model and pinout as the PANEL switch
from section 6. They show up in the Debug view as `BTN2` and `BTN3`
(latch state pill + toggle counter), with no app-level action.

| Switch pin | BTN2 → | BTN3 → |
|------------|--------|--------|
| `G` (GND)  | − rail | − rail |
| `V` (3.3V) | + rail | + rail |
| `S` (signal) | Teensy pin **1** (also Serial1 TX — unused) | Teensy pin **23** |

**Watch the split rail.** BTN3 sits next to the BPM/View/Enc5 cluster
on the right side of the breadboard, well past the MB-102's middle
split. If your `−` rail isn't bridged across the split (step 4),
BTN3 will look "stuck ON, toggles=1" in the Debug view — the `S` pin
floats HIGH because `G` has nowhere to drain. Same symptom would hit
BTN2 if its half of the `−` rail is the un-bridged one.

## 7g. Use the panel switch for chord-mapping mode

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
- [ ] **Both halves of the `+` and `−` rails are bridged across the
  MB-102's middle split** (step 4). Multimeter continuity check: probe
  one end of a rail strip and the far end on the other half — should
  beep. If not, add a jumper.
- [ ] No bare wire ends touching each other.
- [ ] No jumper straddles the center channel by accident (that would
  short two unrelated Teensy pins together).
- [ ] Pin 11 (MOSI) on the Teensy goes to the display's `SDI/MOSI`, NOT
  to `SCK`. Pin 13 (SCK) goes to the display's `SCK`, NOT to `SDI`.
  Mixing these up is the most common cause of a blank or noisy display.
- [ ] All three DFR0789 modules have **three** wires connected (`G`,
  `V`, `S`), not just `S` and `V`. Missing `G` = button stuck ON.
- [ ] All five KY-040 modules have all **five** wires (`GND`, `+`,
  `SW`, `DT`, `CLK`). Missing `+` = noisy / dead rotation.

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
  Big-BPM (giant tempo number), Notation (grand staff), and **Debug**
  (per-control telemetry).
- In the **Debug view**, exercise every knob and button:
  - Rotate Channel / BPM / View / Enc4 / Enc5 → the corresponding
    `total` and `last` columns update, and the row briefly highlights
    yellow for 500 ms.
  - Press Channel-SW / BPM-SW / View-SW / Enc4-SW / Enc5-SW → the
    `press #` counter increments.
  - Flip the PANEL / BTN2 / BTN3 latches → the `ON`/`off` pill
    toggles and the row highlights briefly.
  - If a button is "stuck ON, toggles=1, never changes", suspect a
    GND wiring problem (most often the MB-102 split rail — see
    step 4).
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
- **A DFR0789 latching button looks "stuck ON" in the Debug view
  (toggles=1 at boot, never changes).** The module's `G` pin has no
  electrical path to Teensy `GND`. Most common cause: the MB-102's
  middle rail split wasn't bridged (step 4). Quick check: multimeter
  continuity from the module's `G` screw terminal to a Teensy `GND`
  pin should beep — if it doesn't, the rails are split. Less common:
  cold solder joint on the `G` jumper.
- **A KY-040 reports noisy or no rotation in the Debug view.**
  Same root cause family: `+` not reaching the module's on-board
  pull-ups, or `GND` missing. Continuity-check both rails.
- **Mapping switch toggles the wrong way.** Check the polarity. The
  `TeensyButton` constructor in `platform/teensy/main.cpp` defaults to
  `activeHigh = true` for the DFR0789; if your latching switch has the
  inverse wiring, pass `false`.
- **Ableton still shows the device as "Teensy MIDI" after re-flash.**
  CoreMIDI caches device names. Delete the stale entry in `Audio MIDI
  Setup → Show MIDI Studio`, unplug, replug. If still stale,
  `sudo killall MIDIServer`. See the git log around the `usb_names.c`
  commit for the full explanation.
