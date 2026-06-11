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

## 1. Controls at a glance

The front panel has **five rotary encoders** (Enc1–Enc5, each also a push
button) and **three latching switches** (Latch1–Latch3).

| Control | Role |
|---|---|
| **Enc1–Enc4** (rotate) | Edit the four parameters of the current screen (one knob per cell, left→right). |
| **Enc1–Enc4** (press) | Reserved (no function in normal modes yet). |
| **Enc5** (rotate) | **Switch screens** within the current mode. |
| **Enc5** (press) | Open the **mode-select overlay** (see §3). |
| **Latch1–Latch3** | **Transport.** Their meaning depends on the active mode (see each mode below). |

Simulator keys: Enc1 = `1`/`2`/`3` (left/press/right), Enc2 = `4`/`5`/`6`,
Enc3 = `7`/`8`/`9`, Enc4 = `0`/`-`/`=`, Enc5 = `Q`/`W`/`E`. Latch1 = `Space`,
Latch2 = `Backspace`, Latch3 = `Return` (each key **toggles** the switch).
Inject notes with `z x c v b n m` (white keys C4–B4); `Shift`+`1…9` picks the
channel those notes are sent on; `Esc` quits.

---

## 2. The screen

A 10-pixel **top bar** is always shown:

```
Berlin  -  structure (1/4)        ♩120  ▶
```

It shows the **mode name**, the **current screen** and its index
(`1/4` = screen 1 of 4), the **tempo (BPM)** and the **transport state**.

Most parameter screens show a grid of **cells**, each a parameter: a small
**name** above a large **value**. Visualization screens (worms, notation,
piano-roll) fill the area below the top bar.

---

## 3. Navigating modes and screens

- **Switch screen** (within a mode): rotate **Enc5**. Screens wrap around.
- **Switch mode**: press **Enc5** to open the **mode-select overlay**, rotate
  **Enc5** to highlight a mode, press **Enc5** again to enter it. The overlay
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
- **MIDI Out channel** (Settings → MIDI): the channel Arp/Berlin notes are sent
  on. Range **1–16**, default **1**.
- **MIDI In channel** (Settings → MIDI): which channel incoming notes are
  accepted from. **OMNI (0)** accepts all channels; **1–16** filters to one.
- **Transport** (Settings → MIDI): **Send** (default) = the device emits MIDI
  Start/Continue/Stop so a DAW can follow it; **Recv** = the device follows
  incoming MIDI transport (Start plays from the beginning, Continue resumes,
  Stop halts and silences); **Off** = neither send nor follow. Independent of
  the Clock source setting.

In the visualizations, **channel 1 is green**; other channels have their own
colours so input and output read at a glance.

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

**Screens:** `params1` · `params2` · `worms` · `notes`.

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

**Transport (latches):**

| Latch | Function |
|---|---|
| **Latch1 — Hold** | ON = loop the current note(s) forever; OFF = play each queued note once, then move on. |
| **Latch2 — Mute** | ON = stop sending notes (the sequence keeps running silently); OFF = sound again. |
| **Latch3 — Reset** | Restart the arpeggio from its first step. |

### 5.3 Berlin — generative sequencer

A single-voice generative sequencer in the Berlin-School style. It **generates**
a short looping sequence with one of three algorithms and plays it on the clock.
You drive playback with the latches and shape the music with four parameter
screens. The bottom of every screen shows a **piano-roll** of the current
sequence with a **left-hand keyboard**: each note has its own lane, keys used by
the sequence are marked with a small grey dot, and the **currently played note's
key greys out** as the playhead moves. Each note block's **brightness reflects
its velocity** — louder notes are brighter, quieter notes dimmer.

**Screens:** `structure` · `character` · `dynamics` · `behavior` (the piano-roll
stays visible on all four — only the top parameter row changes).

**Screen `structure`:**

| Knob | Parameter | Range | Default | Meaning |
|---|---|---|---|---|
| Enc1 | **Algorithm** | Walk, Phase, Degree | Walk | Generation method (see below). |
| Enc2 | **Length** | 3–32 | 16 | Steps in the loop. |
| Enc3 | **Resolution** | 8th, 16th | 8th | Step grid (8th = calmer, 16th = busier). |
| Enc4 | **Density** | 0–100 % | 50 | How many steps play a note vs. rest. |

**Screen `character`:**

| Knob | Parameter | Range | Default | Meaning |
|---|---|---|---|---|
| Enc1 | **Gate** | 40–99 % | 55 | Note length within a step. Applies live while playing. |
| Enc2 | **Tension** | 0–100 % | 30 | Low = pitches hug the root/fifth (safe); high = more adventurous. |
| Enc3 | **Octave base** | C1–C5 | C3 | The lowest octave of the voice. |
| Enc4 | **Octave range** | 1–3 | 2 | How many octaves the notes may span. In Live, widening/narrowing proportionally stretches/squeezes the melody (in scale; the root anchor stays). |

**Screen `dynamics`:**

| Knob | Parameter | Range | Default | Meaning |
|---|---|---|---|---|
| Enc1 | **Velocity** | 1–126 | 100 | Base note velocity. Applies live while playing. |
| Enc2 | **Humanize** | 0–30 | 20 | Random ± velocity variation per note. Applies live while playing. |
| Enc3 | **Accent** | 0–27 | 20 | Extra velocity on accented notes (beat 1, root notes). Applies live while playing. |
| Enc4 | **Scatter** | 1–7 | 3 | (Walk only) step size of the melodic wander. Greyed out and locked under Phase/Degree. |

**Screen `behavior`:**

| Knob | Parameter | Range | Default | Meaning |
|---|---|---|---|---|
| Enc1 | **Behavior** | Lock, Evolve, Live | Live | How the sequence changes over time (see below). |
| Enc2 | **Morph** | 0–100 % | 100 | How different a regeneration is from the current sequence: 0 % ≈ same, 100 % = brand new. |
| Enc3 | **Evolve rate** | 1–8 | 4 | (Evolve only) loops between automatic variations. Greyed out and locked under Lock/Live. |
| Enc4 | **GateLen** | 3–16 | 6 | (Phase only) length of the gate list. Greyed out and locked under Walk/Degree. |

> Cells drawn in grey are parameters the current algorithm/behavior ignores —
> their knobs are locked until you switch to a configuration that uses them.

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

**Behaviors (Enc1 on `behavior`):**

- **Lock** — the sequence loops unchanged; your parameter edits apply on the
  next **Generate** (Latch3).
- **Evolve** — while playing, the sequence slowly drifts: 1–2 steps change
  every **Evolve rate** loops. Generate still rolls a whole new pattern.
- **Live** — your edits **sculpt the existing sequence in place** as you turn
  the knob, without re-rolling it and **without ever resetting the playhead** —
  playback keeps running through the change. **Density** adds or removes notes
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
| **Latch1 — Play/Pause** | Switch position = play/pause. Pause holds the playhead in place. |
| **Latch2 — Stop** | Each flip rewinds to step 1 and silences the note. |
| **Latch3 — Generate** | Each flip generates a new sequence (using **Morph** intensity). |

> On a latching switch, "each flip" means one action whether you flip it up or
> down — so a single press regenerates once.

> When **Transport = Send**, Latch1 and Latch2 also emit MIDI Start/Continue/Stop
> so a connected DAW follows the device's playback: Latch1 ON sends Start (or
> Continue when resuming from pause); Latch2 Stop sends Stop.

### 5.4 BPM

A large tempo display. **Enc1** sets the global **BPM** (30–300, default 120).
When the clock source is **External**, the tempo is read-only and follows the
incoming clock.

### 5.5 Settings

Global settings, on two screens.

**Screen `midi`:**

| Knob | Parameter | Range | Default | Meaning |
|---|---|---|---|---|
| Enc1 | **MIDI Out channel** | 1–16 | 1 | Channel Arp/Berlin notes are sent on. |
| Enc2 | **MIDI In channel** | OMNI, 1–16 | OMNI | Accept notes from all channels (OMNI) or just one. |
| Enc3 | **Clock** | Internal, External | Internal | Generate the clock, or follow an external one. |
| Enc4 | **Transport** | Off, Send, Recv | Send | Send = emit Start/Continue/Stop (device is the transport master); Recv = follow incoming transport; Off = neither. |

**Screen `scale`:**

| Knob | Parameter | Range | Default | Meaning |
|---|---|---|---|---|
| Enc1 | **Scale** | Major, Minor, Aug, Dim, Pent+, Pent− | Major | The scale all notes are quantized to. |
| Enc2 | **Root** | C … B | C | The tonal centre. |

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
| `Space` | Latch1 (toggle) |
| `Backspace` | Latch2 (toggle) |
| `Return` | Latch3 (toggle) |
| `z x c v b n m` | Inject notes C4–B4 |
| `Shift`+`1…9` | Set the channel injected notes are sent on |
| `Esc` | Quit |

---

*This manual is kept in sync with the firmware. If something here disagrees
with the device, the code in `core/` and `platform/teensy/main.cpp` is the
source of truth — please report or fix the discrepancy.*
