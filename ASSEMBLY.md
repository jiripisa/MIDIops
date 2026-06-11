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
TFT display through SPI, with a five-encoder / three-latch control
panel. The firmware is a mode-based MIDI instrument (Monitoring / Arp /
Berlin / BPM / Settings / Debug); the function of each control changes
with the active mode, so the hardware is wired by physical identity, not
by a fixed role:
- Three latching panel switches (DFRobot DFR0789, "Latch1–Latch3") —
  each latches mechanically and its LED mirrors the state. They drive
  transport: globally Latch1 = Play/Pause, Latch2 = Stop, Latch3 =
  Reset, but a capturing mode repurposes them (Arp Hold/Mute/Reset,
  Berlin Play/Stop/Generate)
- Four parameter encoders (KY-040, "Enc1–Enc4") — the per-screen knobs;
  rotate to edit a parameter, press for its secondary action
- A navigation encoder (KY-040, "Enc5") — rotate to switch screen within
  a mode, press to open the mode-select overlay (press again to confirm)

Every control is also surfaced in the Debug mode, which is the natural
bring-up check that the hardware is alive.

## 1. Tools and parts

Check `HARDWARE.md` for the complete bill of materials. The short list:

- Teensy 4.1 with male headers already soldered along both long edges
- ILI9341 2.8" SPI display (red PCB, 14-pin header)
- Three DFR0789 panel switches (Latch1 + Latch2 + Latch3)
- Five KY-040 rotary encoders (Enc1 + Enc2 + Enc3 + Enc4 + Enc5)
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

## 6. Wire the first latch (DFR0789 — Latch1)

The DFR0789 has a 3-wire Gravity connector (`G`, `V`, `S`). It's a
latching switch — push it once to lock down, push again to release. The
LED inside the cap lights up while the switch is latched closed.

Three jumpers:

| Switch pin | Goes to |
|------------|---------|
| `G`        | − rail (GND) |
| `V`        | + rail (3.3 V, powers the LED) |
| `S`        | Teensy pin **2** |

The firmware reads the latch level every loop and delivers it to the
active mode. What Latch1 does depends on that mode — globally it is the
transport Play/Pause, while a capturing mode repurposes it (Arp Hold,
Berlin Play). Its LED always tracks the mechanical position.

## 7. Wire the first encoder (KY-040 #1 — Enc1)

Five pins along the bottom edge, labeled `GND, +, SW, DT, CLK` on the
silkscreen:

| Encoder pin | Goes to |
|-------------|---------|
| `GND`       | − rail |
| `+`         | + rail (powers the on-board pull-ups) |
| `SW`        | Teensy pin **3** |
| `DT`        | Teensy pin **5** |
| `CLK`       | Teensy pin **4** |

Enc1 is the first per-screen parameter knob: rotation edits the active
screen's Enc1 parameter and the `SW` press fires its secondary action.
The exact meaning changes with the mode/screen.

If you turn the knob clockwise and the value **decreases** instead of
increases, the easiest fix is to swap the two constructor arguments in
`platform/teensy/main.cpp` (`TeensyEncoder(...)`) — see the comment
there. You can also just physically swap `CLK ↔ DT` on the breadboard;
either way works.

## 7b. Wire the second encoder (KY-040 #2 — Enc2)

Identical module as #1. Place it on the **opposite long edge** of the
Teensy from Enc1 so the two strips of breadboard don't get tangled.

| Encoder pin | Goes to |
|-------------|---------|
| `GND`       | − rail |
| `+`         | + rail |
| `SW`        | Teensy pin **16** |
| `DT`        | Teensy pin **15** |
| `CLK`       | Teensy pin **14** |

Enc2 is the second per-screen parameter knob (rotation + press), role
assigned per mode/screen.

The MIDI panic (release stuck notes) happens automatically when the
DAW sends CC 120 (All Sound Off) or CC 123 (All Notes Off) — Ableton
emits these on transport stop — so a dedicated hardware panic button
isn't needed.

Same direction-reversal trick applies as Enc1.

## 7c. Wire the third encoder (KY-040 #3 — Enc3)

Third identical KY-040. Place it next to Enc2 on the bottom edge of the
breadboard so the two right-edge knobs sit together, within thumb reach.

| Encoder pin | Goes to |
|-------------|---------|
| `GND`       | − rail |
| `+`         | + rail |
| `SW`        | Teensy pin **19** |
| `DT`        | Teensy pin **18** |
| `CLK`       | Teensy pin **17** |

Enc3 is the third per-screen parameter knob (rotation + press), role
assigned per mode/screen.

Same direction-reversal trick applies as the other encoders.

## 7d. Wire the fourth encoder (KY-040 #4 — Enc4)

Identical KY-040 module to the other three. Enc4 is the fourth
per-screen parameter knob; on screens that don't bind a fourth knob it
only updates the Debug mode's counters, so it's also a handy hardware
sanity check.

| Encoder pin | Goes to |
|-------------|---------|
| `GND`       | − rail |
| `+`         | + rail |
| `SW`        | Teensy pin **0** (also Serial1 RX — unused, so it's a normal GPIO) |
| `DT`        | Teensy pin **7** |
| `CLK`       | Teensy pin **6** |

Rotation increments the `ENC4` total in the Debug mode; the shaft
button increments the `ENC4sw` press counter.

## 7e. Wire the fifth encoder (KY-040 #5 — Enc5)

Identical to the others. Enc5 is the **navigation** knob: rotate to
switch the screen within the active mode, press to open the mode-select
overlay (press again to confirm a highlighted mode). It also surfaces in
the Debug mode as `ENC5` / `ENC5sw`.

| Encoder pin | Goes to |
|-------------|---------|
| `GND`       | − rail |
| `+`         | + rail |
| `SW`        | Teensy pin **22** |
| `DT`        | Teensy pin **21** |
| `CLK`       | Teensy pin **20** |

## 7f. Wire the second and third latches (DFR0789 ×2 — Latch2 + Latch3)

Two more DFR0789 modules, same model and pinout as Latch1 from section
6. They show up in the Debug mode as `LATCH2` and `LATCH3` (latch state
pill + toggle counter); their action depends on the active mode (global
transport Stop/Reset, or per-mode such as Arp Mute/Reset or Berlin
Stop/Generate).

| Switch pin | Latch2 → | Latch3 → |
|------------|--------|--------|
| `G` (GND)  | − rail | − rail |
| `V` (3.3V) | + rail | + rail |
| `S` (signal) | Teensy pin **1** (also Serial1 TX — unused) | Teensy pin **23** |

**Watch the split rail.** Latch3 sits next to the Enc2/Enc3/Enc5 cluster
on the right side of the breadboard, well past the MB-102's middle
split. If your `−` rail isn't bridged across the split (step 4),
Latch3 will look "stuck ON, toggles=1" in the Debug mode — the `S` pin
floats HIGH because `G` has nowhere to drain. Same symptom would hit
Latch2 if its half of the `−` rail is the un-bridged one.

## 7g. How the latches and encoders behave at runtime

There is no fixed mapping from a control to a function — the active mode
decides. A quick mental model:

* **Latches** drive transport. In a non-capturing mode (e.g. Monitoring)
  Latch1 = Play/Pause, Latch2 = Stop, Latch3 = Reset. A capturing mode
  repurposes them: **Arp** = Hold / Mute / Reset, **Berlin** = Play /
  Stop / Generate. The LED on each latch always tracks its mechanical
  position.
* **Enc1–Enc4** are the per-screen parameter knobs — rotate to edit,
  press for a secondary action.
* **Enc5** navigates: rotate to change screen, press to open the
  mode-select overlay.

To see this for real, the next steps flash the firmware and exercise
every control from the Debug mode.

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
   - Then the device boots into **Monitoring** mode: a dark roll area
     with the keyboard at the bottom and the top bar showing the mode +
     screen name.
   - Each latch's LED tracks its mechanical position (lit when latched
     closed).

## 10. Bring-up checklist

Work through these in order — Debug mode first, since it's the natural
way to confirm every control is wired before trusting the musical modes.

- **Monitoring + MIDI.** With the device in Monitoring, send MIDI from
  your Mac (Ableton via the IAC Driver routed to `MIDIops`, or the
  simulator's keyboard injection): coloured worms scroll up from the
  keyboard and keys light up under held notes.
- **Navigation.** Rotate **Enc5** — the screen should change within the
  current mode. Press **Enc5** — the mode-select overlay opens; rotate
  Enc5 to highlight a mode and press again to enter it.
- **Debug mode — exercise every control.** Open the overlay and enter
  **Debug**. Then:
  - Rotate Enc1 / Enc2 / Enc3 / Enc4 / Enc5 → the matching `total` and
    `last` columns update and the row briefly highlights yellow.
  - Press Enc1-SW … Enc5-SW → the `press #` counter increments.
  - Flip Latch1 / Latch2 / Latch3 → the `ON`/`off` pill toggles and the
    row highlights briefly.
  - If a latch is "stuck ON, toggles=1, never changes", suspect a GND
    wiring problem (most often the MB-102 split rail — see step 4).
- **Arp.** Enter **Arp** mode, hold a chord on the `MIDIops` input, and
  flip **Latch1** (Hold) — an arpeggio should play out on the MIDI-out
  channel. Latch2 mutes, Latch3 restarts from the first step.
- **Berlin.** Enter **Berlin** mode and flip **Latch3** (Generate) — a
  new sequence is generated; flip **Latch1** (Play) to hear it run.
- **BPM.** Enter **BPM** mode and turn **Enc1** — the large tempo number
  changes (range 30..300). In Ableton, set the tempo source to `MIDIops`
  and confirm the project tempo follows the display (Internal clock).

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
- **A DFR0789 latch looks "stuck ON" in the Debug mode
  (toggles=1 at boot, never changes).** The module's `G` pin has no
  electrical path to Teensy `GND`. Most common cause: the MB-102's
  middle rail split wasn't bridged (step 4). Quick check: multimeter
  continuity from the module's `G` screw terminal to a Teensy `GND`
  pin should beep — if it doesn't, the rails are split. Less common:
  cold solder joint on the `G` jumper.
- **A KY-040 reports noisy or no rotation in the Debug mode.**
  Same root cause family: `+` not reaching the module's on-board
  pull-ups, or `GND` missing. Continuity-check both rails.
- **A latch toggles the wrong way.** Check the polarity. The
  `TeensyButton` constructor in `platform/teensy/main.cpp` defaults to
  `activeHigh = true` for the DFR0789; if your latching switch has the
  inverse wiring, pass `false`.
- **Ableton still shows the device as "Teensy MIDI" after re-flash.**
  CoreMIDI caches device names. Delete the stale entry in `Audio MIDI
  Setup → Show MIDI Studio`, unplug, replug. If still stale,
  `sudo killall MIDIServer`. See the git log around the `usb_names.c`
  commit for the full explanation.
