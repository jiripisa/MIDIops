# MIDIops — User Manual

*Languages: **English** · [Čeština](MANUAL.cs.md)*

MIDIops is a hardware MIDI instrument built on a Teensy 4.1 with a 2.8"
(320×240) display. It runs several **modes** — a MIDI monitor, a scale-aware
**arpeggiator**, a generative **Berlin-School sequencer**, a big-tempo view,
**settings**, and a **debug** view. The same software runs in a macOS
simulator (`make sim`), so everything in this manual applies to both the real
device and the simulator.

This manual explains every control and every parameter. For wiring and a build
walkthrough see [`HARDWARE.md`](HARDWARE.md) and [`ASSEMBLY.md`](ASSEMBLY.md).

---

## Quick start

1. **Power on.** The device boots straight into **Berlin** with a four-voice
   pattern already generated. Press **Latch1** (Play/Pause) to start and stop it.
2. **Hear it.** Berlin's four voices are sent on **MIDI channels 1, 2, 3 and 4**
   by default (Bass, Mid, High, Lead). Route those channels to four instruments
   — or to one multitimbral synth — to hear the full texture. The hardware
   appears over USB as **`MIDIops`**; the simulator opens a virtual MIDI port
   named **`MIDIops Sim`**.
3. **Make it yours.** In **Settings → Scale** pick a key and scale. Back in
   Berlin, press **Latch3** (Generate) to roll a fresh pattern, and — with
   **Behavior = Live** (the default) — turn the parameter knobs to reshape it
   while it keeps playing.
4. **Get around.** Rotate **Enc5** to change screen within a mode; **press
   Enc5** to open the mode picker, then rotate to a mode and press again.

> New to the terms used here (voices, phasing, the latches, Live sculpting)?
> See the **Glossary** below.

---

## 1. Controls at a glance

The front panel has **five rotary encoders** (Enc1–Enc5, each also a push
button) and **three latching switches** (Latch1–Latch3).

| Control | Role |
|---|---|
| **Enc1–Enc4** (rotate) | Edit the four parameters of the current screen (one knob per cell, left→right). |
| **Enc1–Enc4** (press) | A **per-screen secondary action** where one exists — e.g. select a Berlin voice (`structure`/`character`), mute a voice (Berlin `voices`), arm the factory reset (Settings `system`), or confirm a preset slot. No effect on screens that define none. |
| **Enc5** (rotate) | **Switch screens** within the current mode. |
| **Enc5** (press) | Open the **mode-select overlay** (see §3). |
| **Latch1–Latch3** | **Transport buttons** — each press is one click; the software toggles/acts by its current state (the switch's physical position and LED carry no meaning). Their meaning depends on the active mode (see each mode below). |

Simulator keys: Enc1 = `1`/`2`/`3` (left/press/right), Enc2 = `4`/`5`/`6`,
Enc3 = `7`/`8`/`9`, Enc4 = `0`/`-`/`=`, Enc5 = `Q`/`W`/`E`. Latch1 = `Space`,
Latch2 = `Backspace`, Latch3 = `Return` (each key press = one button click).
Inject notes with `z x c v b n m` (white keys C4–B4); `Shift`+`1…9` picks the
channel those notes are sent on; `Esc` quits.

---

## 2. The screen

A 10-pixel **top bar** is always shown:

```
Berlin  -  structure (1/6)        ♩120  ▶
```

It shows the **mode name**, the **current screen** and its index
(`1/6` = screen 1 of 6 — Berlin has six), the **tempo (BPM)** and the
**transport state**.

Most parameter screens show a grid of **cells**, each a parameter: a small
**name** above a large **value**. Visualization screens (worms, notation,
piano-roll) fill the area below the top bar.

---

## 3. Navigating modes and screens

- **Switch screen** (within a mode): rotate **Enc5**. Screens wrap around.
- **Switch mode**: press **Enc5** to open the **mode-select overlay** — the
  mode names sit on one horizontal row, each with its icon above the name
  (oscilloscope = Monitoring, ascending notes = Arp, sequencer bars = Berlin,
  metronome = BPM, sliders = Settings, bug = Debug), and the framed item in
  the centre of the screen is the selection. The framed item is drawn largest;
  items shrink and darken with distance from the centre. Rotate **Enc5** to slide
  the row left/right — the row glides smoothly, like turning a tape, and wraps
  around. Press **Enc5** again to enter the framed mode. The overlay
  closes by itself after a few seconds of no input. While the overlay is open
  the latches do not drive the global transport, but the active mode's own latch
  functions (e.g. Berlin Generate) still apply.

The device boots into **Berlin**. Modes, in order: **Monitoring · Arp ·
Berlin · BPM · Settings · Debug**.

---

## 4. Global concepts

A few settings are **global** — shared by every mode (especially Arp and
Berlin). You change them in **Settings** and **BPM**:

- **Scale & Root** (Settings → Scale): the musical scale and tonal centre.
  Every generated/arpeggiated note is quantized into this scale. Changing it
  affects Arp and Berlin alike.
- **Tempo (BPM)** (BPM mode): the clock speed for all clock-driven modes.
  Range **30–300**, default **120**.
- **Clock source** (Settings → MIDI): **Internal** (the device generates the
  MIDI clock) or **External** (the device follows incoming MIDI clock at
  24 PPQN and shows the followed tempo).
- **MIDI Out channel** (Settings → MIDI): the channel **Arp** notes are sent
  on. Range **1–16**, default **1**. (Berlin does not use this — each of its
  four voices has its own channel on the Berlin `voices` screen.)
- **MIDI In channel** (Settings → MIDI): which channel incoming notes are
  accepted from. **OMNI (0)** accepts all channels; **1–16** filters to one.
- **Transport** (Settings → MIDI): **Send** (default) = the device emits MIDI
  Start/Continue/Stop so a DAW can follow it; **Recv** = the device follows
  incoming MIDI transport (Start plays from the beginning, Continue resumes,
  Stop halts and silences); **Off** = neither send nor follow. Independent of
  the Clock source setting.

All of the settings above — including the tempo — are **saved automatically**
about 2 seconds after the last change and restored at the next power-up.
**Settings → System** offers a **factory reset** that returns them all to the
defaults listed here.

In the visualizations, **channel 1 is green**; other channels have their own
colours so input and output read at a glance.

---

## Glossary

Terms used throughout this manual:

- **Capturing vs non-capturing modes.** **Arp** and **Berlin** *capture* the
  three latches for their own functions (listed in each section). In every other
  mode the latches are the **global transport**: Latch1 = Play/Pause,
  Latch2 = Stop, Latch3 = Reset (these emit MIDI transport when Transport = Send).
- **The edited voice** (Berlin). The `structure` and `character` screens edit
  one voice at a time; press **Enc1/2/3/4** to choose Bass/Mid/High/Lead. The
  edited voice's name shows on the piano-roll in its colour.
- **Note-phasing** (Berlin). The voices share one tempo but have different loop
  lengths (Mid 15 against the others' 16), so they drift in and out of alignment
  over many bars — the genre's signature "evolving but repeating" motion.
- **Generate / Morph** (Berlin). **Generate** (Latch3) rolls new sequences;
  **Morph** sets how different the new pattern is from the old (0 % ≈ keep the
  parts, 100 % = brand new).
- **Live sculpting** (Berlin, Behavior = Live). Turning a knob reshapes the
  *existing* sequence in place without restarting it — see §5.3.
- **In-phase muting** (Berlin). A muted voice keeps running silently, so
  unmuting drops it back in time with the others.
- **Scale quantize.** Every generated or arpeggiated note is snapped to the
  global scale (Settings → Scale), so nothing ever plays out of key.

---

## 5. Modes

### 5.1 Monitoring

A real-time monitor of incoming MIDI notes. No parameters — just two views:

| Screen | Shows |
|---|---|
| **worms** | Per-channel coloured "worms" rising from a keyboard while notes are held. |
| **notes** | A staff (notation) view of recent incoming notes. |

The latches act as the **global transport** here (Play/Pause · Stop · Reset),
which sends MIDI Start/Stop/Continue when **Transport** is set to **Send**.

### 5.2 Arp — Arpeggiator

A scale-aware arpeggiator. Hold or inject notes; the engine plays them back as
an arpeggio on the **MIDI Out channel**, locked to the clock. Notes queue
**FIFO** so they never overlap.

**How you'd use it.** Hold a chord (or, on the simulator, the `z x c v b n m`
keys) and the arp plays it in time. Press **Latch1 (Hold)** to latch the
notes so you can let go and keep both hands free for the knobs; shape the motion
live with **Rate / Gate / Direction / Octave / Swing**; press **Latch2 (Mute)**
to drop the arp out without losing the pattern, and **Latch3 (Reset)** to
restart it on the beat. Save a setup you like to a preset slot.

**Screens:** `params1` · `params2` · `worms` · `notes` · `presets`.

**Screen `params1`:**

| Knob | Parameter | Range | Default | Meaning |
|---|---|---|---|---|
| Enc1 | **Steps** | 1–16 | 3 | Steps per arpeggio cycle. |
| Enc2 | **Rate** | 1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32 | 1/16 | Step length in note values. |
| Enc3 | **Gate** | 10–100 % | 80 | How long each note sounds within its step (short = plucky). |
| Enc4 | **Direction** | Up, Down, UpDown, DownUp, Random | Up | Order the chord tones are played in. |

**Screen `params2`:**

| Knob | Parameter | Range | Default | Meaning |
|---|---|---|---|---|
| Enc1 | **Octave** | −2…+2 | 0 | Transpose the arpeggio in octaves. |
| Enc2 | **Swing** | 50–75 % | 50 | Delay every other step for a swung feel (50 = straight). |
| Enc3 | **Velocity** | Fixed, Follow, Accent | Fixed | Fixed level, follow the input velocity, or accented. |
| Enc4 | *(status)* | — | — | Shows **Hold** and **Mute** state (read-only). |

The `worms` and `notes` screens visualize the **outgoing** arpeggio.

**Screen `presets`:** save, load and delete the Arp parameters in **20
slots**. Press **Enc1 = Save**, **Enc2 = Load** or **Enc3 = Delete** to open
the slot picker — a grid of slots 01–20 where used slots are bright, empty
ones dim and the selection is framed. Rotate any of Enc1–4 to pick a slot,
then **press the same encoder again to confirm**; pressing a different
encoder cancels, as does 5 s without input or leaving the screen. The slot
number is remembered, so a save → load round-trip stays on the same slot.
Save overwrites a used slot directly; Delete keeps the picker open so you
can clean several slots in a row; Load/Delete on an empty slot just flash
`EMPTY`.

**Transport (latches):**

| Latch | Function |
|---|---|
| **Latch1 — Hold** | Press toggles **Hold** (state shown on the params2 screen): on = loop the current note(s) forever; off = play each queued note once, then move on. |
| **Latch2 — Mute** | Press toggles **Mute** (state shown on the params2 screen): on = stop sending notes (the sequence keeps running silently); off = sound again. |
| **Latch3 — Reset** | Press restarts the arpeggio from its first step. |

### 5.3 Berlin — generative sequencer

A **four-voice** generative sequencer in the Berlin-School style. Each voice
**generates** its own short looping sequence and plays it on the clock; the
four together build the classic layered Berlin texture:

- **Bass** — the root-heavy "heartbeat". It is built by its own root-anchor
  generator (root on the strong beats, occasional fifth/octave, short gates),
  so the **Algorithm** knob does not apply to it. Defaults: octave C1, length
  16, density 30 %, gate 50 %, channel 1.
- **Mid** — the pluck figure. Defaults: octave C3, length **15**, channel 2.
- **High** — the moving melody, and the voice selected for editing by default.
  Defaults: octave C4, length 16, channel 3.
- **Lead** — a sparse, high-register melody that does **call-and-response**
  with High: it plays in High's gaps (at **Generate**, each Lead step that
  collides with an active High step is deactivated). Defaults: octave C4 with
  range 2 (so it spans C4–C6), length 16, density 30 % (sparse, lots of
  rests), gate 85 % (legato), channel 4.

Mid's **15** steps running against the other voices' **16** is the genre's
signature **note-phasing**: the voices share a tempo but their loops are
different lengths, so they drift in and out of alignment over many bars.

You drive playback with the latches and shape the music with five parameter
screens. The bottom of every screen shows a **piano-roll** of all four voices
at once (see below).

**A typical session.** Pick a key in **Settings → Scale**. Back in Berlin,
press **Latch1** to start and **Latch3 (Generate)** until you land on a stack
you like — keep **Morph** (on `behavior`) high to roll something brand new, or
lower it to keep the parts and only nudge them. With **Behavior = Live** (the
default) sculpt each voice on `structure`/`character` — thin the **Density**,
open the **Octave range**, raise **Tension** — and it changes under your fingers
without restarting. Use the **`voices`** mixer to drop voices in and out (mute)
and to put each on its own MIDI channel. Save the whole four-voice patch to a
**preset** slot, and perform by sending notes on the **MIDI-in channel** to
transpose the entire stack diatonically (see *MIDI transposition* below).

**Screens:** `structure` · `character` · `voices` · `dynamics` · `behavior` ·
`presets` (the piano-roll stays visible on the parameter screens — only the
top parameter row changes).

**Per-voice vs. global.** `structure` and `character` edit **one voice at a
time** (the *edited voice*); `dynamics` and `behavior` are **global** and apply
to all four voices together. The `voices` screen is the mixer (one cell per
voice). On the two per-voice screens, **pressing Enc1 / Enc2 / Enc3 / Enc4
selects the voice directly** (Bass / Mid / High / Lead); **mute lives on the
`voices` mixer**, not here. The edited voice's name is shown at the top-right of
the piano-roll in its colour. Each per-voice cell shows **all four voices'
values stacked** (Lead on top, then High, then Mid, then Bass): the selected
voice's value is highlighted in white while the others are darker, so you see
every voice at a glance with the active one standing out.

**Screen `structure`** (per voice):

| Knob | Parameter | Range | Default | Meaning |
|---|---|---|---|---|
| Enc1 | **Algorithm** | Walk, Phase, Degree | Walk | Generation method (see below). For **Bass** this cell shows "Bass" and is locked — Bass always uses its own root-anchor generator. |
| Enc2 | **Length** | 3–32 | 16 (Mid 15) | Steps in this voice's loop. |
| Enc3 | **Density** | 0–100 % | 50 (Bass 30, Lead 30) | How many steps play a note vs. rest. |
| Enc4 | **AlgoPrm** | — | — | One shared cell: **Scatter** (1–7) under Walk, **GateLen** (3–16) under Phase. Shown as a greyed "-" under Degree and for Bass. |

**Screen `character`** (per voice):

| Knob | Parameter | Range | Default | Meaning |
|---|---|---|---|---|
| Enc1 | **Gate** | 40–99 % | 55 (Bass 50, Lead 85) | Note length within a step. Applies live while playing. |
| Enc2 | **Tension** | 0–100 % | 30 | Low = pitches hug the root/fifth (safe); high = more adventurous. |
| Enc3 | **Octave base** | C1–C5 | Lead C4, High C4, Mid C3, Bass C1 | The lowest octave of the voice. |
| Enc4 | **Octave range** | 1–3 | 1 (Lead 2) | How many octaves the notes may span. In Live, widening/narrowing proportionally stretches/squeezes the melody (in scale; the root anchor stays). |

**Screen `voices`** (mixer — one cell per voice):

| Knob | Parameter | Range | Default | Meaning |
|---|---|---|---|---|
| Enc1 | **Bass** channel / mute | 1–16 | 1 | Rotate to set the Bass MIDI channel; **press to mute/unmute**. |
| Enc2 | **Mid** channel / mute | 1–16 | 2 | Rotate to set the Mid MIDI channel; press to mute/unmute. |
| Enc3 | **High** channel / mute | 1–16 | 3 | Rotate to set the High MIDI channel; press to mute/unmute. |
| Enc4 | **Lead** channel / mute | 1–16 | 4 | Rotate to set the Lead MIDI channel; press to mute/unmute. |

> A **muted** voice keeps running silently (its sequence and playhead carry on),
> so unmuting drops it back in **in phase** with the others — the "build up,
> then take away" move. A muted voice's cell shows **MUTED** and its roll lane
> is drawn darkest.

**Screen `dynamics`** (global — applies to all voices):

| Knob | Parameter | Range | Default | Meaning |
|---|---|---|---|---|
| Enc1 | **Velocity** | 1–126 | 100 | Base note velocity. Applies live while playing. |
| Enc2 | **Humanize** | 0–30 | 20 | Random ± velocity variation per note. Applies live while playing. |
| Enc3 | **Accent** | 0–27 | 20 | Extra velocity on accented notes (beat 1, root notes). Applies live while playing. |
| Enc4 | **Resolution** | 8th, 16th | 8th | Step grid (8th = calmer, 16th = busier). |

**Screen `behavior`** (global):

| Knob | Parameter | Range | Default | Meaning |
|---|---|---|---|---|
| Enc1 | **Behavior** | Lock, Evolve, Live | Live | How the sequences change over time (see below). |
| Enc2 | **Morph** | 0–100 % | 100 | How different a regeneration is from the current sequence: 0 % ≈ same, 100 % = brand new. |
| Enc3 | **Evolve rate** | 1–8 | 4 | (Evolve only) loops between automatic variations. Greyed out and locked under Lock/Live. |
| Enc4 | — | — | — | Unused. |

> Cells drawn in grey are parameters the current algorithm/behavior ignores —
> their knobs are locked until you switch to a configuration that uses them.

**The piano-roll** shows **all four voices at once** over a shared keyboard:
**Bass blue, Mid green, High orange, Lead magenta**. The **edited voice** is drawn fully
saturated, the others dimmed, and a muted voice darkest. Each voice has **its
own playhead** — because the voices have different lengths their playheads drift
apart, so the phasing is visible right on the roll. Each note block's
**brightness still reflects its velocity** (louder = brighter). Accents are no
longer drawn white — the voice's colour identity always wins.

**Screen `presets`:** works exactly like Arp's presets screen (Enc1 = Save,
Enc2 = Load, Enc3 = Delete over 20 slots — see §5.2), with one Berlin twist:
a slot stores the **whole four-voice stack** — all parameters, all four
realized sequences, and each voice's channel and mute state — so a load brings
back the very patterns you saved, not a re-roll. Loading while playing swaps
the stack **seamlessly**: each voice's playhead keeps running (wrapped into the
new length) — ideal for live transitions. **Slots saved before the Lead-voice
update appear empty** and can simply be overwritten.

**Algorithms (Enc1 on `structure`):**

- **Walk** (Drunkard's Walk) — a meandering melody: each note steps a small
  random interval (up to **Scatter**) from the previous one, kept in scale,
  with **Tension** weighting the choice toward (low) or away from (high) the
  root and fifth.
- **Phase** (Gate/Pitch Phasing) — a pitch list (length = **Length**) and a
  gate list (length = **GateLen**) of different lengths run against each other,
  producing a long, slowly evolving pattern that "sounds random but isn't."
- **Degree** (Degree-Weighted) — each note is chosen independently, weighted
  toward consonant scale degrees (root/fifth); **Tension** flattens the bias.
- The **Bass** voice ignores these three and uses its own **root-anchor
  generator**: the root on the strong beats, the odd fifth or octave, short
  gates — the heartbeat under the other voices.

**Behaviors (Enc1 on `behavior`):**

- **Lock** — the sequence loops unchanged; your parameter edits apply on the
  next **Generate** (Latch3).
- **Evolve** — while playing, the sequence slowly drifts: 1–2 steps change
  every **Evolve rate** loops. Generate still rolls a whole new pattern.
- **Live** — your edits **sculpt the existing sequence in place** as you turn
  the knob, without re-rolling it and **without ever resetting the playhead** —
  playback keeps running through the change. Live sculpting targets the
  **edited voice** only. **Density** adds or removes notes
  to hit the new amount (the root anchor on step 1 always stays); **Octave
  base/range** transpose and fold the existing notes into the new register
  (melody contour preserved); **Length** truncates (the playhead wraps) or
  extends (only the new tail is filled); **Tension** re-pitches the notes while
  keeping the existing rhythm, gate and velocities. **Gate**, **Resolution** and
  **Velocity/Humanize/Accent** are live in every behavior (Gate shapes the
  playing notes, Resolution changes the step grid, the Dynamics knobs re-stamp
  the velocities at once). **Algorithm, Scatter and GateLen** — which decide
  *how a sequence is built* — apply at the next **Generate** (Latch3), which
  still does the full **Morph**-governed regeneration. **Morph** and **Evolve
  rate** are meta settings that govern that regeneration and the Evolve drift.

**Transport (latches):**

| Latch | Function |
|---|---|
| **Latch1 — Play/Pause** | Press toggles play/pause for **all four voices** together. Pause holds the playheads in place. (Under Transport = Recv the DAW drives playback; a press is still a manual-override toggle.) |
| **Latch2 — Stop** | Press rewinds all voices to step 1 and silences them. |
| **Latch3 — Generate** | Press regenerates **all four voices** (then masks Lead against High for call-and-response), then runs a **vertical consonance check** — clashing simultaneous notes (a minor second or tritone) get nudged to a consonant in-scale tone. (The check is skipped when any voice's **Tension** is above 60.) |

> When **Transport = Send**, Latch1 and Latch2 also emit MIDI Start/Continue/Stop
> so a connected DAW follows the device's playback: a play press sends Start (or
> Continue when resuming from pause); a Stop press sends Stop.

**MIDI transposition.** While in Berlin, notes arriving on the global **MIDI In
channel** (Settings → MIDI) transpose the whole four-voice stack **diatonically**
— everything stays in the current scale. The transposition is **latched**: the
last note sets the new key centre and it holds until the next note. Playing the
**scale root around middle C** returns home; playing higher/lower shifts the
melody up/down by scale degrees (whole octaves included). The incoming note is a
silent control — it is not sounded. The piano-roll moves with the transposition.

### 5.4 BPM

A large tempo display. **Enc1** sets the global **BPM** (30–300, default 120).
When the clock source is **External**, the tempo is read-only and follows the
incoming clock.

### 5.5 Settings

Global settings, on three screens. Every change here (and the BPM) is saved
automatically ~2 s after the last edit and survives a power cycle.

**Screen `midi`:**

| Knob | Parameter | Range | Default | Meaning |
|---|---|---|---|---|
| Enc1 | **MIDI Out channel** | 1–16 | 1 | Channel **Arp** notes are sent on. (Berlin uses its own per-voice channels — see §5.3.) |
| Enc2 | **MIDI In channel** | OMNI, 1–16 | OMNI | Accept notes from all channels (OMNI) or just one. |
| Enc3 | **Clock** | Internal, External | Internal | Generate the clock, or follow an external one. |
| Enc4 | **Transport** | Off, Send, Recv | Send | Send = emit Start/Continue/Stop (device is the transport master); Recv = follow incoming transport; Off = neither. |

**Screen `scale`:**

| Knob | Parameter | Range | Default | Meaning |
|---|---|---|---|---|
| Enc1 | **Scale** | Major, Minor, Aug, Dim, Pent+, Pent− | Major | The scale all notes are quantized to. |
| Enc2 | **Root** | C … B | C | The tonal centre. |

**Screen `system`:**

A **FACTORY RESET** cell driven by the **Enc1 press** (two-step, so it cannot
fire by accident): the first press arms it (`SURE?`), a second press within
3 seconds restores every global setting and the BPM to its default and erases
the stored values (`DONE`). If you do not confirm in time it returns to idle.
Rotating the knobs does nothing on this screen.

### 5.6 Debug

A diagnostics screen that shows live activity for every encoder and latch
(rotation counts, last delta, press counts, latch state). Useful for verifying
the hardware. Recently-changed lines highlight so you can see which control
moved. No MIDI output.

---

## 6. Clock and Transport

**Clock source** (Settings → MIDI, Enc3) and **Transport** (Settings → MIDI, Enc4) are independent settings.

- **Internal** (default): the device is the clock master. It sends MIDI Clock
  (24 PPQN), and Arp/Berlin run from it. Set the tempo in **BPM** mode.
- **External**: the device follows incoming MIDI Clock. Arp/Berlin advance on
  each incoming pulse, the displayed BPM follows the external tempo, and the
  device does **not** generate its own clock. Switch back to Internal to resume.

**Transport** controls whether MIDI Start/Continue/Stop are sent or followed, independently of the clock source:

- **Send** (default): the device emits MIDI transport messages. The global
  latches (in non-capturing modes) and Berlin's Latch1/Latch2 send Start,
  Continue, or Stop so a connected DAW follows the device's playback.
- **Recv**: the device follows incoming MIDI transport. Start plays from the
  beginning; Continue resumes from the held position; Stop halts and silences
  the sounding note immediately. Messages are consumed and not re-emitted.
- **Off**: the device neither sends nor follows transport messages.

**Two common setups:**

- **MIDIops as the master.** Clock = **Internal**, Transport = **Send**: set the
  tempo in **BPM** mode; your DAW follows the device's clock and play/stop.
- **MIDIops as a follower.** Clock = **External**, Transport = **Recv**: the DAW
  runs the clock and transport, and Berlin/Arp lock to it (the displayed BPM
  follows the incoming tempo). See the README for the macOS IAC routing steps.

**Safety (always active):** under an **External** clock source, an incoming MIDI
Stop always silences the sounding note immediately — regardless of the Transport
setting — because a DAW stopping the clock means the note's scheduled gate-off
would never arrive.

---

## 7. Simulator quick reference

| Keys | Control |
|---|---|
| `1` `2` `3` | Enc1 — left / press / right |
| `4` `5` `6` | Enc2 — left / press / right |
| `7` `8` `9` | Enc3 — left / press / right |
| `0` `-` `=` | Enc4 — left / press / right |
| `Q` `W` `E` | Enc5 — left / press / right (screen switch / mode overlay) |
| `Space` | Latch1 (one button click) |
| `Backspace` | Latch2 (one button click) |
| `Return` | Latch3 (one button click) |
| `z x c v b n m` | Inject notes C4–B4 |
| `Shift`+`1…9` | Set the channel injected notes are sent on |
| `Esc` | Quit |

---

*This manual is kept in sync with the firmware. If something here disagrees
with the device, the code in `core/` and `platform/teensy/main.cpp` is the
source of truth — please report or fix the discrepancy.*
