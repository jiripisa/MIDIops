# Berlin mode — design

Status: for review · Date: 2026-06-10 · Part of the v1 series.

A new mode, **Berlin**, that *generates* and *plays* a single-voice MIDI
sequence in the Berlin School style, advancing on the unified 24-PPQN clock
tick stream (Plan 5b). The user generates a sequence, drives playback with
play/pause + stop, and regenerates with a reset button. The bottom of the
display permanently visualizes the generated sequence (piano-roll); the top
shows parameters, screen by screen.

**Source of truth for generation rules:**
`/Users/jpisa/Development/Claude/synthseeker/berlin-school-theory-and-generator-spec-EN.md`
(also referenced from the project `CLAUDE.md`). Part 2 of that document — data
tables, parameter ranges/defaults, the "Generate" algorithm — is the basis for
everything generative here.

---

## 1. Scope (v1)

- **Single voice.** One generated sequence, output on the global
  `midiOutChannel`. (The source doc is multi-voice — bass/mid/high/lead — but
  v1 is one voice. Note-phasing, which "requires at least 2 sequencers," is
  still available *within one voice* via the gate/pitch split technique.)
- **Three generation algorithms** behind one pluggable interface, implemented
  incrementally: **Drunkard's Walk**, **Gate/Pitch Phasing**, **Degree-Weighted**.
- **Scale + Root come from global Settings** (`AppServices::scale()`), exactly
  as `ArpMode` already consumes them — single source of truth, no Berlin-local
  key. Changing the scale in Settings affects Arp and Berlin alike.
- **Tempo/clock** come from the global clock (internal master or followed
  external clock, Plan 5b) — Berlin has no tempo parameter of its own.

Out of scope for v1 (see §12): multi-voice, vertical consonance correction
(monophonic → no simultaneous intervals), global diatonic transposition by
degree progression, persistence to flash.

## 2. Architecture (all in `core/`, portable C++17, no platform headers)

### 2.1 Data model
```cpp
struct BerlinStep {
    bool     active   = false;  // false = rest
    uint8_t  note     = 0;      // absolute MIDI note (already scale-quantized)
    uint8_t  velocity = 0;      // 1..127
    bool     accent   = false;  // for visualization + velocity boost
    uint16_t gateTicks = 0;     // note-on duration in 24-PPQN ticks
};

class BerlinSequence {            // fixed-capacity, no heap
    static constexpr int kMaxSteps = 32;   // Walk/Degree materialize ≤ Length
                                           // (≤16) steps here; Phasing is
                                           // computed on the fly (see §4.2),
                                           // not stored as its full lcm().
    BerlinStep steps_[kMaxSteps];
    int        length_ = 16;      // realized pattern length
    // accessors: step(i), length(), set...
};
```

### 2.2 Engine
```cpp
class BerlinEngine {
public:
    void setOutput(MidiOutput*);
    void setScale(const Scale&);          // pushed from svc_.scale() each update
    void setParams(const BerlinParams&);  // pushed from the mode
    void setOutChannel(uint8_t);

    void onClockTick();    // advance one 24-PPQN tick (gate-off + step boundary)
    void play();           // run
    void pause();          // hold playhead
    void stop();           // rewind to step 0 + all-notes-off
    void generate();       // (re)generate using current params + Morph
    bool isPlaying() const;
    int  playhead() const;            // current step, for the viz
    const BerlinSequence& sequence() const;   // for the viz
private:
    BerlinSequence seq_;
    // tick scheduler mirrors ArpEngine: stepTicks_, gateTicks_, noteAge_,
    // a sounding-note flag; advance a step every resolution-ticks.
};
```
The tick scheduler is the **same shape as the (already shipped) tick-driven
`ArpEngine`**: count ticks, fire the step note at the boundary, close the gate
when `noteAge_ >= step.gateTicks`. No swing (Berlin is "clockwork").

### 2.3 Generators (pluggable, seedable, deterministic)
```cpp
class SequenceGenerator {
public:
    virtual ~SequenceGenerator() = default;
    // Fill `out` from params + scale, using `rng`. Pure function of inputs +
    // rng state → unit-testable with a fixed seed.
    virtual void generate(BerlinSequence& out,
                          const BerlinParams&, const Scale&,
                          std::mt19937& rng) = 0;
};
```
`std::mt19937` is portable C++17 stdlib (no platform header) → allowed in
`core/`. The engine owns one `std::mt19937`; tests seed it explicitly so
generation is reproducible. The three concrete generators are §4.

### 2.4 Render split — "bottom persists, top changes"
`BerlinMode::render()` draws the **piano-roll in the bottom region itself**
(so it survives screen switches), then delegates the **top region** to the
active screen, which draws only its 1×4 parameter row. Each Berlin screen is a
`Screen` that renders into the top strip; the mode renders the viz. (Exact
region split + the `Display` calls are settled in the plan; the design
contract is: *mode owns the viz region, screen owns the param region*.)

## 3. Generation model (shared by all algorithms)

- **Note pool** = the global scale's degrees laid out across
  `[octaveBase … octaveBase + octaveRange]`. Everything is **quantized into
  the scale** — "I literally can't play anything out of key."
- **Rhythm / density.** `Density%` sets how many of `Length` steps are active
  (rest = the others). Rests are mandatory ("leave some space"). Beats
  (steps 1/5/9/13 in a 16-grid) are favored for activity; back-beats are where
  variation lives.
- **Resolution.** 8th = 12 ticks/step, 16th = 6 ticks/step (24 PPQN). Default
  8th ("I prefer the echo and step nature of the eighth note").
- **Gate.** `gateTicks = stepTicks × Gate% / 100`, min 1. Short = pluck (norm).
- **Velocity / accent.** `velocity = base ± humanize`, capped ≤126. Accent
  (`+Accent`) on accent steps and on root hits ("returning to the root
  self-accents"). Accent flag also brightens the viz.
- **Degree weights + Tension.** Pitch selection is biased by the degree-weight
  table (root 1.00, fifth 0.90, … tritone-ish low). `Tension%` spreads the
  weights flat (more tension) or sharpens toward root/fifth (less).
- **Out-of-key.** With a small probability (~2%, fixed for v1) a generator may
  place one out-of-scale note as tension, then return to consonance.

## 4. The three algorithms

### 4.1 Drunkard's Walk (the doc's flagship, default)
- Start on the root. Each successive active step moves from the previous note
  by **±rand(0…Scatter) semitones**, then quantizes to the scale → a
  meandering contour, not leaps. Bias the choice by the degree weights /
  Tension so it gravitates to root/fifth.
- Density distributes rests in **clusters** (wobble), not uniform per-step
  randomness ("pleasing clusters of slow/fast hits and rests").
- Exposes the contextual **Scatter** parameter (1–7 semitones).

### 4.2 Gate/Pitch Phasing (deterministic "sounds-random-but-isn't")
- Maintain **two lists of different lengths**: a PITCH list of length
  `Length` (root-heavy, degree-weighted) and a GATE list of length
  `GateLen` (booleans, `Density%` active). The realized step *i* plays
  `pitch[i % Length]` gated by `gate[i % GateLen]`; the pattern realigns every
  `lcm(Length, GateLen)` steps → a long evolving figure with recognizable
  repetition. Recommended pairs: 8×6, {3,5,7}×8, "prefer even × odd."
- The engine advances a running step counter and computes each step **on the
  fly** (`pitch[i % Length]` gated by `gate[i % GateLen]`) rather than
  materializing the full `lcm(Length, GateLen)` realignment (which can reach
  240 steps for 16×15). The pitch and gate lists themselves (each ≤16) are
  what gets stored/regenerated. The piano-roll renders a **bounded window** of
  the realized phasing pattern (the upcoming `kMaxSteps` steps) with the
  playhead. Exposes the contextual **GateLen** parameter.

### 4.3 Degree-Weighted (simple, controllable)
- Each active step independently picks a scale **degree** from the weight
  table (spread by Tension), root-anchored (start and periodically return to
  the root). No ±scatter contour — independent weighted picks. The simplest of
  the three; good "safe" generator, especially with minor pentatonic.

## 5. Morph (regeneration intensity)

The engine keeps the current sequence as a base. `Generate()` (= the Reset
button) produces the next sequence governed by **Morph% (0–100)**:
- **100%** — full fresh generation (new RNG draw for every step).
- **< 100%** — keep the base; for each step, with probability `Morph%`,
  re-roll that step; otherwise keep it. **0%** ≈ identical (only a step or two
  change). This matches the user's intent: lower = closer to the previous
  sequence, 100% = completely new.

Default **100%** (Reset = a brand-new sequence); dial down for subtle variation.

## 6. Behavior (lifecycle) — a parameter, implemented incrementally

`Behavior ∈ { Locked, Evolve, Live }` selects *when* (re)generation happens:
- **Locked** (default, v1 first): the sequence loops identically; parameter
  edits stage silently and apply on the next Reset/Generate.
- **Evolve**: like Locked, but every **EvolveRate** loops the engine auto-varies
  1–2 steps in place (the doc's "change one or two notes every 3rd–4th
  repeat") for organic drift. Reset still does the Morph regenerate.
- **Live**: turning a *structural* knob (Algorithm, Length, Resolution,
  Density, Octave) regenerates immediately; performance knobs (Gate, Velocity,
  Accent) apply without regenerating.

Implemented in order Locked → Evolve → Live (see §11).

## 7. Parameters

Scale + Root are **not** here — they come from Settings. Tempo is global.
Four parameter screens, four cells each (Enc1–Enc4); Enc5 switches screens.

| Screen | Enc1 | Enc2 | Enc3 | Enc4 |
|---|---|---|---|---|
| **1 — Structure** | Algorithm | Length | Resolution | Density |
| **2 — Character** | Gate | Tension | Octave base | Octave range |
| **3 — Dynamics** | Velocity base | Velocity humanize | Accent | *contextual* |
| **4 — Behavior** | Behavior | Morph | Evolve rate | — |

The **contextual** cell (Screen 3, Enc4) shows the algorithm-specific knob:
**Scatter** when Algorithm = Drunkard's Walk, **GateLen** when Algorithm =
Gate/Pitch Phasing, empty for Degree-Weighted.

### Ranges & defaults (from the source doc Part 2)
| Param | Range | Default |
|---|---|---|
| Algorithm | Walk / Phasing / Degree | Walk |
| Length | 3–16 steps | 16 |
| Resolution | 8th / 16th | 8th |
| Density | 0–100% | 50% |
| Gate | 40–99% | 55% |
| Tension | 0–100% | 30% |
| Octave base | C1–C5 | C3 |
| Octave range | 1–3 | 2 |
| Velocity base | 1–126 | 100 |
| Velocity humanize | 0–±30 | ±20 |
| Accent | 0–+27 | +20 |
| Scatter (Walk) | 1–7 semitones | 3 |
| GateLen (Phasing) | 3–16 | 6 |
| Behavior | Locked / Evolve / Live | Locked |
| Morph | 0–100% | 100% |
| Evolve rate | 1–8 loops | 4 |

`BerlinParams` is a plain struct holding all of the above; the mode owns one,
edits it from encoder input, and pushes it to the engine each `update()`.

## 8. Visualization — piano-roll (bottom region, persistent)

- **X = step** (1…length), **Y = pitch** (note height within the voice's
  register), drawn as filled blocks; **block width ≈ gate**; **rest = empty
  column**; **accent = brighter block**. A **playhead** marker tracks the
  current step left→right as it plays.
- Rendered by `BerlinMode` (not the screen) so it stays put across screen
  switches; only the top parameter strip changes. Uses the existing RGB565
  `Display` drawing primitives (filled rects). The voice colour follows the
  output-channel palette (consistent with the monitor/arp worms).
- No scrolling — it is a static piano-roll of the *current* sequence with a
  moving playhead (simpler than the worm model; the sequence is finite and
  known).

## 9. Transport (latches; `capturesTransport()` = true)

Like `ArpMode`, Berlin captures the three latches:
- **Latch1 = Play/Pause** — ON = running, OFF = paused (playhead holds).
- **Latch2 = Stop** — edge on flip: rewind to step 1, all-notes-off.
- **Latch3 = Reset/Generate** — edge on flip: `generate()` (Morph-governed).

(Berlin sends no global MIDI Start/Stop — playback is local, same stance as
Arp. Transport latches drive the engine only.)

## 10. Integration

- **`AppServices::scale()`** — read each `update()`, pushed to the engine
  (`setScale`). No new AppServices members needed.
- **`midiOutChannel()`** — pushed to the engine (`setOutChannel`), same as Arp.
- **Clock** — Berlin overrides `onClockTick()` (Mode hook from Plan 5b) →
  `engine_.onClockTick()`. AppShell already routes internal-drained and
  external-followed ticks to the active mode.
- **Registration** — add `BerlinMode` to both mains in spec order
  (Monitoring, Arp, **Berlin**, BPM, Settings, Debug).

## 11. Plan decomposition

- **Plan A — Berlin v1 core.** `BerlinSequence` + `BerlinEngine` (tick
  scheduler, transport, generate) + the **Drunkard's Walk** generator +
  **Locked** behavior + **piano-roll** visualization + transport latches +
  Structure/Character/Behavior screens + register in mains. Produces the full
  generate→play→pause→stop→reset loop on both platforms. Most logic is
  host-unit-testable (engine + generator with a fixed seed); the viz is
  verified in the sim.
- **Plan B — more algorithms.** **Gate/Pitch Phasing** + **Degree-Weighted**
  generators, the contextual Scatter/GateLen cell, and the **Dynamics** screen
  (velocity/humanize/accent), with the engine honouring per-step velocity/accent.
- **Plan C — richer behavior.** **Evolve** (auto-variation every N loops) and
  **Live** (regenerate-on-structural-edit) behaviors; polish Morph.

Each plan yields working, testable firmware on its own.

## 12. Deferred / out of scope

- Multi-voice (bass/mid/high/lead) and true cross-voice note-phasing.
- Vertical consonance correction (monophonic v1 has no simultaneous notes).
- Global diatonic transposition following a degree progression (1–7–6–5 …).
- Odd meters / accent maps beyond 4/4 (the grid is 4/4 for v1).
- Persistence of generated sequences / parameters to flash.

## 13. Testing

- **Generators** (host unit tests, fixed `std::mt19937` seed): Drunkard's Walk
  stays in scale, respects Scatter bounds, honours Density (active-step count),
  anchors on the root; Phasing realizes `pitch[i%P]` gated by `gate[i%G]` and
  realigns at `lcm`; Degree-Weighted picks in-scale degrees. Morph: 0% ≈ base
  (≤ a couple of steps changed), 100% independent of base.
- **Engine** (host): `onClockTick` fires step notes at the right tick spacing
  per resolution, gate closes after `gateTicks`, transport (play/pause/stop)
  moves/holds/rewinds the playhead, `generate()` swaps the sequence.
- **Mode** (host): latches drive transport; scale/out-channel pushed from
  services; `onClockTick` advances the engine via the AppShell tick routing.
- **Visualization**: verified live in the simulator (piano-roll + playhead;
  persistence across screen switches).

## 14. Resolved decisions (from brainstorming)

1. Voices = **single** for v1 (phasing via gate/pitch split within one voice).
2. Algorithms = **all three** via a pluggable interface, built incrementally.
3. Lifecycle = a **parameter** (Locked / Evolve / Live), built incrementally.
4. Morph = **Reset regenerates** with intensity (0% ≈ same … 100% = fresh).
5. Scale + Root = **from global Settings**, not Berlin-local.
6. **4 parameters per screen** (Enc1–4); Enc5 switches screens.
7. Visualization = **piano-roll**, drawn by the mode, persists across screens.
8. Transport = **3 latches** (Play/Pause, Stop, Reset/Generate), captured.
