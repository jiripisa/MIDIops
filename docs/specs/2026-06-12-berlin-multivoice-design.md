# Multi-voice Berlin (Bass / Mid / High) — design

Status: approved in brainstorm · Date: 2026-06-12.

Berlin grows from one voice to **three voices with distinct roles** per the
spec's High/Mid/Low template (spec §11, §2.3): a root-heavy **Bass** anchor,
a **Mid** pluck figure and a **High** moving melody, with the genre's
signature **note-phasing** (different per-voice lengths drifting against
each other, spec §7). A fourth voice (Lead, call-and-response) is a future
stage and is explicitly out of scope here; the architecture must not block
it (voice array, not hard-coded trio).

Spec reference: `synthseeker/berlin-school-theory-and-generator-spec-EN.md`
(Part 1 §7/§9/§11, Part 2 §2.3/§2.4).

## 1. Core architecture

* `BerlinMode` owns **three voice slots**: each = one `BerlinEngine`
  instance (unchanged — it is already a self-contained sequence + playhead +
  gate unit) + its own `BerlinParams` + a MIDI **channel** (1..16,
  configurable, defaults 1/2/3) + a **mute** flag. Clock ticks fan out to
  all three engines; transport (play/pause/stop) drives all three.
* **Phasing comes free**: per-voice lengths (defaults Bass 16, Mid 16,
  High 15 — the classic 16×15 pair) give each voice its own loop and
  playhead; drifting playheads ARE the phasing.
* **New `BassAnchorGenerator`** (spec §2.4c, §9): root as the skeleton on
  beats 1 and 9 (musical beats 1 and 3), occasional fifth/octave, rare move
  to degree 4/6, short gates — the "heartbeat". The Bass voice always uses
  it; the Algorithm knob applies to Mid/High only (greyed + locked for Bass,
  same dim pattern as Scatter under Degree today).
* **Generate (Latch3)** regenerates all three voices, then runs the spec's
  **vertical consonance check** (§2.4 step 3): where two simultaneously
  sounding steps form a low-weight interval, move one note to the nearest
  consonant scale tone; skipped when Tension is high.
* **Per-role defaults** seeded from spec §2.3: Bass C1–C2, low density,
  short gate; Mid C3–C4; High C4–C5, Drunkard's Walk.
* Mute suppresses a voice's note emission (engine keeps running so unmuting
  re-enters in phase) — "build up, then take away" (spec §11).

## 2. UI — six screens

| Screen | Scope | Enc1 | Enc2 | Enc3 | Enc4 |
|---|---|---|---|---|---|
| `structure` | per voice | Algorithm (Bass: locked) | Length | Density | AlgoPrm (Scatter under Walk / GateLen under Phase — one shared cell) |
| `character` | per voice | Gate | Tension | Oct base | Oct range |
| `voices` | mixer | Bass: rotate = channel, press = mute | Mid: ditto | High: ditto | — |
| `dynamics` | global | Velocity | Humanize | Accent | Resolution |
| `behavior` | global | Behavior | Morph | Evolve rate | — |
| `presets` | | unchanged flow; a slot now stores all three voices | | | |

* On the per-voice screens **pressing any Enc1–4 cycles the edited voice**
  (Bass → Mid → High → Bass); the voice name is shown prominently and the
  selection is shared across both screens.
* Global params (dynamics, behavior, morph, evolve, resolution) apply to
  all voices. Live sculpting (`applyLive*`) targets the edited voice only.
* **Piano-roll**: one roll over the union pitch range of all voices.
  Bass blue, Mid green, High orange; the edited voice fully saturated,
  the others dimmed, a muted voice darkest. Each voice spans the full roll
  width at its own column width (length-normalized) and draws its own
  playhead only across its register band — drifting playheads visualize the
  phasing. Velocity-as-brightness stays.

## 3. Presets

New **v2 blob**: globals + per-voice (params + realized sequence + channel
+ mute). **v1 slots load as empty** (the format change is structural; v1
presets were saved within a day of this design, so the loss is accepted).
Seamless mid-play load keeps working: each engine gets `setSequence` with
its playhead wrapped.

## 4. Implementation stages

* **A — multi-voice core**: voice array in BerlinMode, clock/transport
  fan-out, per-voice channels + mute, `BassAnchorGenerator`, role defaults,
  `voices` mixer screen. (Roll temporarily shows the edited voice only.)
* **B — per-voice UI**: voice cycling on press, screen re-layout
  (AlgoPrm shared cell, Resolution → dynamics), combined colored roll with
  per-voice playheads.
* **C — polish**: vertical consonance check, presets v2, manuals (EN+CZ) +
  CLAUDE.md.

Out of scope: Lead voice (4th), meter support, global diatonic
transposition, per-voice dynamics.

## 5. Testing

Engine-level: BassAnchorGenerator (root on anchors, fifth/octave bias, all
in scale/register); consonance check moves a clashing note to a consonant
scale tone and respects high Tension. Mode-level: three engines tick and
phase independently (16×15 drift); mute silences only its voice and keeps
phase; per-voice channels on emitted notes; voice cycling switches the
edit target; Live ops touch only the edited voice; Generate regenerates
all three. Presets: v2 round-trip (3 voices byte-exact), v1 blob loads as
empty. Renders: mixer screen, voice-name indicator, multi-voice roll
draws all three colors.
