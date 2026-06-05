# Arp mode — design

Status: for review · Date: 2026-06-06 · Supersedes the draft
`2026-06-03-arp-design-DRAFT.md`. Part of the v1 series
(`2026-06-02-v1-mode-architecture-design.md`, §7.2).

## 1. What Arp does

A scale-aware monophonic arpeggiator. You play a note; Arp builds the
**diatonic triad** on that note from the current scale and plays it
**arpeggiated** out on the global MIDI-out channel, locked to the master
clock. It runs while a note is held and stays musically quantized — chord
changes never cut a cycle off mid-way (the PoC's FIFO feel).

## 2. Note → chord → arpeggio (the generator)

- The played note is **quantized to the nearest scale tone** (so the arp
  never plays out of key).
- From that root, build the **diatonic triad**: root, scale-3rd (two scale
  steps up), scale-5th (four scale steps up). The chord quality follows the
  scale degree automatically (e.g. in C major: C→C E G, D→D F A, E→E G B).
- **Steps** decides how many notes are played, wrapping into higher octaves
  once the triad's three tones are used up. C major, playing C1:
  - steps=3 → C1 E1 G1
  - steps=4 → C1 E1 G1 C2
  - steps=5 → C1 E1 G1 C2 E2
- **Direction** then orders that step list: Up, Down, Up-Down, Down-Up, or
  Random (Random never repeats the same step twice in a row).
- **Octave** shifts the whole result by −2..+2 octaves.

The generator — `(rootNote, Scale, Steps, Direction, Octave) → ordered list
of MIDI notes` — is a **pure function**, unit-tested independently of timing.

## 3. Timing, transport, and the FIFO queue

- **Clock-locked.** Step advance is driven by the master clock at 24 PPQN;
  **Rate** is the clock division (ticks per step). **Swing** delays
  even-numbered steps. **Gate** sets each note's length as a fraction of one
  step.
- **Plays while held + loops.** Holding a note loops its arpeggio cycle
  continuously. The clock master always runs, so no Play press is needed.
- **FIFO queue (PoC behaviour).** Reconciling "full FIFO" with
  "loops while held":
  - The **active** note is the queue head; the engine plays its arpeggio,
    looping while that note is still held.
  - On the active note's **release**, the current cycle finishes (no
    mid-cycle cut), then the head is dequeued and the next queued note (if
    any) becomes active.
  - New NoteOns are **appended** to the FIFO; they never interrupt the active
    cycle. A staccato note (pressed + released before it reaches the head)
    plays exactly one cycle when its turn comes, then dequeues — exactly like
    the PoC chord queue.
  - Queue depth bound: 16 (drop oldest-or-newest beyond that — decide in
    plan; PoC dropped new).
- **Latch (param)** changes release handling: NoteOffs are ignored, so the
  active note loops indefinitely; a new NoteOn then **replaces** the active
  note at the next cycle boundary (single-slot, since there are no releases
  to advance a FIFO under Latch).
- **Transport:** Latch1 Play/Pause and Latch2 Stop silence/resume the arp
  (Stop sends note-offs for anything sounding and clears the queue); Latch3
  Reset returns the step index to 0. (Since the arp also plays on held notes
  independent of transport, Stop is effectively a global mute/panic for the
  mode.)

## 4. Parameters (8, two screens of 4)

**Params 1–4** (Enc1–Enc4):

| # | Param | Range | Default | Meaning |
|---|-------|-------|---------|---------|
| 1 | **Steps** | 1–16 | 3 | arpeggio length; >3 wraps into higher octaves |
| 2 | **Rate** | 1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32 | 1/16 | step speed (clock division) |
| 3 | **Gate** | 10–100 % | 80 % | note length as a fraction of a step |
| 4 | **Direction** | Up, Down, Up-Down, Down-Up, Random | Up | step order |

**Params 5–8** (Enc1–Enc4):

| # | Param | Range | Default | Meaning |
|---|-------|-------|---------|---------|
| 5 | **Octave** | −2..+2 | 0 | transpose the whole arp by octaves |
| 6 | **Swing** | 50–75 % | 50 % | delay on even steps |
| 7 | **Velocity mode** | Fixed / Follow-input / Accent | Fixed | Fixed = a set value; Follow-input = use the played note's velocity; Accent = stronger on step 1 |
| 8 | **Latch** | Off / On | Off | keep arpeggiating after release until the next note |

(Velocity mode = Fixed needs a stored level — default 100; the encoder cycles
the mode, and the fixed level is a sub-value shown when mode = Fixed. Final
UI detail settled in the plan.)

Each param screen maps Enc1–Enc4 to its four parameters. Enc5 rotates between
the four Arp screens; Enc5 press opens the mode-change overlay (shell-level,
unchanged).

## 5. Scale (new, minimal)

`Scale` does not exist yet. Introduce a minimal portable `core/Scale.h`:
- Scale **type** (maj, min, aug, dim, pentatonic-maj, pentatonic-min — the
  set from the v1 Settings spec) + **root key** (C..B).
- Operations: `quantize(note)` → nearest in-scale note; `degreeNote(root,
  steps)` → the note `steps` scale-degrees above `root` (for triad building).
- **Source of scale+root:** the `AppShell` holds the current scale type +
  root (default **C major**) and exposes them via `AppServices`. Arp reads
  them. The **Settings mode (Plan 4)** will later just write to these — no
  later migration of Arp needed.

## 6. Engine architecture

`ArpEngine` (portable `core/`, no `Display`, unit-testable):
- **Inputs:** `noteOn(note, velocity)`, `noteOff(note)`, a `Scale&`, the 8
  params, `setBpm`, `tick(nowMs)`, and a `MidiOutput*`.
- **Pure generator** (§2) produces the ordered step list for the active root.
- **Scheduler:** advances the step index on clock-derived timing (Rate, Swing)
  and emits NoteOn/NoteOff to `MidiOutput` honouring Gate + Velocity mode.
- **Queue:** the FIFO + active-note + cycle-boundary logic of §3.
- **Echo:** like `ChordEngine`, an echo callback fires for each emitted note
  so the shared `NoteWormModel` can visualise the **outgoing** notes. Arp
  feeds them via the model's existing output path (`onEngineNoteOn/Off`).
- `ChordEngine` is left untouched (it serves the future mapping mode).

Split for testability: the generator (pure, no time) and the scheduler/queue
(time-driven, fake clock) are tested separately; rendering is visual.

## 7. The Arp mode + screens

`ArpMode` (in `core/modes/`) owns: an `ArpEngine`, a `NoteWormModel` (for the
outgoing-note visualisation), and the 8 parameter values. Four screens
(Enc5 cycles them):

1. **Params 1–4** — Enc1–Enc4 edit Steps / Rate / Gate / Direction.
2. **Params 5–8** — Enc1–Enc4 edit Octave / Swing / Velocity mode / Latch.
3. **Worms** — `WormsRenderer` over the mode's `NoteWormModel` (outgoing
   notes). Reused from Plan 2.
4. **Notes** — `NotationRenderer` over the same model. Reused from Plan 3.

`ArpMode::onMidiIn` feeds NoteOn/NoteOff to the engine (and the engine's echo
feeds the model's output path). `ArpMode::update(now)` ticks the engine + the
model. Param screens render the 4 values with the active one highlighted;
`onEncoder(i, delta)` edits param `i`.

Registration: add `ArpMode` to both platform mains (order per spec:
Monitoring, Arp, Berlin, BPM, Settings, Debug — so Arp is mode index 1).

## 8. Testing

- **Generator** (pure): triad correctness per scale degree; Steps octave-wrap;
  Direction orderings; Octave shift; out-of-scale quantization. Strong TDD.
- **Scheduler/queue** (fake clock + fake MidiOutput): step advance at the
  right tick spacing for each Rate; Gate note-off timing; FIFO ordering;
  loop-while-held; cycle-quantized switch on release; staccato one-cycle;
  Latch replace-at-boundary; Stop/Reset.
- **Scale** (pure): quantize + degreeNote for each scale type.
- **ArpMode** (integration via shell + StubDisplay): inject a NoteOn, tick,
  confirm outgoing notes reach the model and the screens draw.
- **Rendering:** visual in the simulator.

## 9. Plan decomposition (high level — detailed plan next)

Likely split into focused plans/tasks: (1) `Scale` + AppServices scale/root;
(2) the pure Arp **generator**; (3) the `ArpEngine` scheduler + FIFO; (4)
`ArpMode` + the two param screens + worms/notes reuse + register in mains.
Each is independently testable. The exact task breakdown is the writing-plans
step.

## 10. Deferred / out of scope

- Settings UI for scale/root/out-channel (Plan 4) — Arp uses AppServices
  defaults now.
- Candidate params not chosen (ratchet, probability/density, free-Hz rate) —
  later iteration if wanted.
- Per-step accent patterns beyond "Accent = step 1 stronger".
- Berlin mode (separate spec).

## 11. Resolved questions (from brainstorm)

1. Queue = **full FIFO** (PoC-style), reconciled with loop-while-held per §3.
2. Note release (Latch off) = **stop after the current cycle**.
3. Velocity = **mode** (Fixed / Follow-input / Accent).
4. Rate = **1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32**.
5. Random = **no immediate repeats**.
6. Scale+root = **global in AppServices** (default C major), set later by
   Settings.
7. Held note = **loops the cycle** until released.
