# Arp mode — design DRAFT (for review)

> **STATUS: DRAFT — brainstorm in progress, NOT approved.** Homework prepared
> overnight for review the next morning. Nothing here is implemented; the
> parameter set + queue behaviour need a final sign-off, then this becomes a
> proper spec → plan.

## Confirmed so far (from the brainstorm)

- **Model:** play a note → build a **diatonic triad** from the scale (root +
  scale-3rd + scale-5th), and play it **arpeggiated**.
- **Steps** controls how many notes are played, wrapping into higher octaves
  once the triad is exhausted. Example in C major, playing C1:
  - steps=3 → C1 E1 G1
  - steps=4 → C1 E1 G1 C2
  - steps=5 → C1 E1 G1 C2 E2
- **Chord is always a triad** (3 scale tones); Steps only extends the
  arpeggio upward in octaves.
- **Monophonic:** one active note at a time (last note wins — but see the
  queue rule below for *when* the switch happens).
- **Out-of-scale input note → quantize** to the nearest scale tone, then
  build the triad. The arp never plays outside the scale.
- **Scale type + root key come from Settings** (global), not Arp params.
- **Output channel comes from Settings** (global midi out), not an Arp param.
- **Plays while a note is held** (the clock master always runs); transport
  Stop silences it. No need to press Play first.
- **New `ArpEngine`** (not a bend of the trigger/mapping `ChordEngine`).
  Reuses `Scale` + the clock; `ChordEngine` stays for the future mapping mode.

## NEW tonight — buffer / queue behaviour (carried over from the PoC)

The PoC's `ChordEngine` had a FIFO so a chord never got cut off mid-play.
The Arp must behave the same way:

- **A new played note does NOT switch the arpeggio immediately.** The
  currently-sounding arpeggio finishes its current cycle first, then the arp
  switches to the new note's chord.
- "Finishes" = the running arpeggio completes its current **Steps cycle**
  (one full pass through the generated sequence). The switch happens at that
  cycle boundary — no mid-sequence cut.

**Open question for the morning (pick one):**
- **(A) Single-slot "pending next":** only the *latest* played note is held as
  pending; it takes over at the next cycle boundary. Earlier presses while
  one is already pending are overwritten. (Simple, feels like "queue the next
  change".)
- **(B) Full FIFO (like the PoC):** every played note is enqueued; each plays
  one full cycle in turn before the next. (Matches the PoC literally; can
  build up a backlog if you play fast.)

Recommendation: **(A)** for a live mono arp — you usually want "whatever I
last pressed plays next", not a backlog. But (B) is what the PoC did, so
confirm which feel you want.

A second open question: should releasing the note **stop after the current
cycle** (cycle-quantized note-off, consistent with the switch rule) or stop
immediately? Recommendation: stop after the current cycle for musical
consistency (unless Latch is on — then it keeps going until the next note).

## Parameter proposal

### Core 8 (the two param screens, 4 each)

**Screen "Params 1–4"** (Enc1–Enc4):

| # | Param | Range | Default | Meaning |
|---|-------|-------|---------|---------|
| 1 | **Steps** | 1–16 | 3 | arpeggio length; >3 wraps into higher octaves |
| 2 | **Rate** | 1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32 | 1/16 | step speed, synced to the MIDI clock (24 PPQN) |
| 3 | **Gate** | 10–100 % | 80 % | note length as a fraction of one step |
| 4 | **Direction** | Up, Down, Up-Down, Down-Up, Random | Up | order the Steps sequence is played |

**Screen "Params 5–8"** (Enc1–Enc4):

| # | Param | Range | Default | Meaning |
|---|-------|-------|---------|---------|
| 5 | **Octave** | −2..+2 | 0 | transpose the whole arp by octaves (base shift, distinct from Steps) |
| 6 | **Swing** | 50–75 % | 50 % | delays even-numbered steps for groove |
| 7 | **Velocity** | 1–127 | 100 | fixed output velocity |
| 8 | **Latch** | Off / On | Off | keep arpeggiating after the note is released, until the next note |

### Candidate / optional params to consider (discussion for the morning)

Things that *could* replace or supplement the core 8 if you want — flagged so
you can swap any in:

- **Velocity mode** (Fixed / Follow-input / Accent-pattern) instead of a plain
  fixed Velocity — accents on step 1 give life. Could replace #7.
- **Ratchet / repeats** per step (1–4 retriggers) — adds rhythmic interest.
- **Rate as "free Hz"** instead of clock-synced — probably NOT (we have a
  precise clock; sync is the point).
- **Octave mode** (how Steps wraps: up only / up-down / alternating) — but
  Direction may already cover the feel; likely YAGNI.
- **Probability / density** (skip steps by chance) — generative flavour;
  could overlap with the Berlin mode's territory, so maybe keep it out of Arp.

My recommendation: ship the **core 8** as listed; treat the candidates as a
later iteration unless one of them is something you specifically want now.

## Screens (4) and controls

Per the v1 spec §7.2, Arp has 4 screens, switched with Enc5 rotation:
1. **Params 1–4** — Enc1–Enc4 edit Steps / Rate / Gate / Direction.
2. **Params 5–8** — Enc1–Enc4 edit Octave / Swing / Velocity / Latch.
3. **Worms** — the existing `WormsRenderer` showing the **outgoing** notes
   (fed via the model's output/engine-note path — reuse from Plan 2).
4. **Notes** — the existing `NotationRenderer` showing the outgoing notes
   (reuse from Plan 3).

Transport: Latch1 Play/Pause, Latch2 Stop, Latch3 Reset — Stop silences the
arp; Reset returns the sequence to step 0.

## Sketch of the engine

`ArpEngine` (portable `core/`, unit-testable, no Display):
- Inputs: `noteOn(note)` / `noteOff(note)`, the `Scale` (type+root from
  Settings), the 8 params, and `tick(nowMs)` driven by the clock.
- Holds: current root note, the generated step sequence (derived from
  Steps+Direction+Octave over the diatonic triad), the current step index,
  the pending-note buffer (queue rule above), and in-flight note-off
  scheduling (gate).
- Emits NoteOn/NoteOff to `MidiOutput` and echoes to the shared
  `NoteWormModel` output path so the Worms/Notes screens visualise it.
- Step advance is driven by clock ticks (Rate → ticks-per-step), so it stays
  locked to the master clock; Swing offsets even steps.

The arp's note generation (note + scale → diatonic triad → Steps/Direction/
Octave → ordered note list) is a **pure function**, separately unit-testable
from the scheduling.

## Open questions to settle in the morning

1. Queue behaviour: single-slot pending (A) vs full FIFO (B). *(rec: A)*
2. Note release: stop after current cycle vs immediately (when Latch off).
   *(rec: after current cycle)*
3. Final parameter set: keep the core 8, or swap in any candidate
   (esp. Velocity mode / accents)?
4. Rate values list — is the proposed set (1/4…1/32 + triplets) right, or do
   you want dotted values / a different range?
5. Direction "Random" — allowed to repeat the same step, or no immediate
   repeats?
6. Does `Scale` already exist, or is it built as part of this work? (It's in
   the v1 spec's Settings but not yet implemented — Arp may need to introduce
   a minimal `Scale` now, with the Settings UI coming in Plan 4.)
