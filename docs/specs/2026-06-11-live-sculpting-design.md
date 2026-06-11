# Berlin Live = in-place sculpting — design

Status: approved in brainstorm · Date: 2026-06-11.

Redefines the Berlin **Live** behavior. Today a structural knob turn does a
full fresh regeneration (`generateFull`) and rewinds the playhead — turning a
knob five detents means five brand-new patterns. The new Live **sculpts the
existing sequence in place**: only the touched parameter's effect is applied,
everything else (and the playhead) stays. A full regeneration happens only on
**Generate** (Latch3, Morph-governed), as in the other behaviors.

## 1. Per-parameter live edits (Behavior == Live)

| Knob | Live effect on the existing sequence |
|---|---|
| **Density** | Add/remove notes to hit the new target count (`round(density% × length)`, min 1). Adding: random inactive steps get a fresh note (degree-weighted — in scale, in register, Tension-aware) with velocity/gate from current params. Removing: random active steps deactivate; **step 0 (root anchor) is never removed**. Untouched steps stay identical. |
| **Octave base** | Transpose every active note by the knob's ±12 per detent, then fold into the new register. Melody contour identical. |
| **Octave range** | Fold notes into the new register by octaves (narrowing pulls outliers in; widening leaves notes as they are — the extra room is used by future notes). |
| **Length** | Shorten: truncate (keep the first N steps); the playhead wraps (`playhead %= length`) instead of resetting. Lengthen: keep all existing steps, generate fresh content only for the appended tail (density-gated, degree-weighted). |
| **Resolution** | Already live by construction (the engine reads the step grid from current params every tick, in every behavior); just stop regenerating and re-stamp the baked per-step `gateTicks` so the piano-roll widths match. |
| **Tension** | Re-pitch keeping the rhythm: every active step except step 0 gets a new degree-weighted note under the new Tension; the active pattern, gate and velocities stay. Accent flags are recomputed from the new pitches. |
| **Gate** | Already live (previous change) — engine derives gate from current params each step. |
| **Algorithm, Scatter, GateLen** | **No immediate action** — they parameterize *how a sequence is created*, so they apply at the next Generate (Latch3). (Today they trigger a full regen; that goes away.) |

**The playhead is never reset by a live edit.** Latch3 Generate keeps today's
semantics (full regeneration with Morph intensity, rewind to step 0).

## 2. Mechanism

- New in-place edit operations on `BerlinEngine` (it owns `seq_`, `params_`,
  `scale_`, `rng_`): `applyLiveDensity()`, `applyLiveOctaveBase(int deltaSemis)`,
  `applyLiveOctaveRange()`, `applyLiveLength()`, `applyLiveTension()`. Each
  reads the already-pushed `params_`, edits `seq_` in place using the shared
  `BerlinGen` helpers (`berlinBaseRoot`, `berlinDegreeWeightedNote`,
  `berlinFoldIntoRegister`, `berlinFinalizeVelocity`, `berlinGateTicks`), and
  wraps the playhead when the length shrank. Fully unit-testable with a seeded
  RNG.
- `BerlinMode`'s screens call the matching operation when
  `behavior == Live` (after updating the param and pushing `setParams`).
  `liveRegen()` and `BerlinEngine::generateFull()` lose their last callers and
  are **removed**.
- Non-Live behaviors are unchanged: edits stage until Generate — except
  Resolution and Gate, which are inherently live in every behavior already
  (the engine reads them per tick).

## 3. Out of scope

- Live morphing of Algorithm/Scatter/GateLen (apply on Generate).
- Preserving exact note identities across a Density down→up round trip
  (removed notes are forgotten; re-adding rolls fresh ones).
- Velocity re-rolls during Tension re-pitch (velocities keep their values;
  only accent flags refresh).

## 4. Testing

Engine-level (seeded): density up adds exactly the delta and leaves other
steps byte-identical; density down removes only active steps, never step 0;
octave-base transposes by ±12 exactly; range-narrow folds every note in
register and in scale; length-shorten keeps the prefix and wraps the playhead;
length-extend keeps the prefix and fills only the tail; tension re-pitch keeps
the rhythm/gate/velocity and changes pitches; **no operation resets the
playhead**. Mode-level: each knob triggers its operation only under Live;
Locked stays staged; Algorithm/Scatter/GateLen no longer regenerate under
Live. Manuals (EN+CZ) updated.
