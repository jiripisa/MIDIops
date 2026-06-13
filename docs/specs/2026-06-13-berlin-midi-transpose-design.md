# Berlin — diatonic transposition from MIDI input — design

Status: approved in brainstorm · Date: 2026-06-13.

Berlin listens on the global MIDI-in channel and, on an incoming note,
transposes the whole generated stack **diatonically** (in scale degrees) to a
new key centre, quantized to the global scale. This is the genre's "global /
diatonic transposition" technique (spec §10, *Key2Mod / Day 25*): everything
stays in key, intervals are preserved diatonically.

Spec reference: `synthseeker/berlin-school-theory-and-generator-spec-EN.md` §10.

## 1. Input path (no AppShell change)

`AppShell::onMidiIn` already filters channel-voice messages by the global
MIDI-in channel (`midiInChannel_`, 0 = OMNI) and routes them to the active
mode's `onMidiIn`. Berlin currently does not override `onMidiIn`, so incoming
notes are ignored today — adding the override is purely additive. The incoming
note is a **silent control input**: Berlin does not echo it to MIDI out.

## 2. Transposition amount (diatonic, octave-aware)

New `Scale::degreeIndex(uint8_t note) const` → the absolute scale-degree index
of `note` including octaves (signed): quantize the note, find its position in
the interval table (`idx`), and return `octave * degreeCount() + idx`, where
the octave is derived from the note's base root. Differences between two
indices give the signed scale-degree distance.

The "home" reference is **R0 = the scale root in the octave at MIDI 60**, i.e.
`R0 = 60 + root_pc` (60..71) — playing the tonic around middle C means no
transposition. On an incoming NoteOn:

```
degrees = scale.degreeIndex(incomingNote) - scale.degreeIndex(R0)
```

The incoming note is quantized to the scale first (`degreeIndex` does this), so
out-of-scale input still yields a sensible degree. Playing R0 an octave up
gives `+degreeCount()` (one octave up); an octave down gives the negative. No
artificial clamp on `degrees`; `Scale::degreeNote` already clamps the resulting
MIDI note to 0..127. (R0's octave constant is tunable.)

## 3. Application (non-destructive)

`BerlinEngine` gains `int transposeDegrees_` + `setTransposeDegrees(int)`.
- At `emitStep`, the emitted pitch is `scale_->degreeNote(s.note, transposeDegrees_)`,
  and the gate is armed with that **transposed** note, so the matching NoteOff
  (on the next step, gate expiry, `silence()`, or `stop()`) targets the same
  note — no stuck notes when the offset changes mid-gate.
- `seq_` stays "home" (untransposed). The offset is an output/display overlay
  only, so repeated transposition never drifts and the live sculpting ops
  (`applyLive*`), `generate()`, and presets are unaffected.

## 4. Global across the three voices

`BerlinMode` owns the authoritative `transposeDegrees_`. On an incoming NoteOn
it computes `degrees` (against the current `svc_.scale()`) and calls
`setTransposeDegrees(degrees)` on all three engines, so Bass/Mid/High move
together (coherent global transposition). NoteOff is ignored (latched: the last
note's transposition persists). `onEnter` resets the offset to 0 (home). The
offset is ephemeral: it is not persisted and a preset load does not change it.

## 5. Visualization

`BerlinMode::renderRoll` builds, per voice, a temporary transposed copy of the
engine's sequence (each active note → `degreeNote(note, transposeDegrees_)`, 32
steps on the stack) and points the `BerlinRollVoice` at it, so the piano-roll
moves with the audio. `drawBerlinMultiRoll` is unchanged; `soundingNote()` is
already in transposed space (the gate was armed transposed).

## 6. Testing

- `Scale::degreeIndex`: +1 per scale step, +`degreeCount()` per octave, signed
  across the root; round-trips with `degreeNote`.
- `BerlinEngine`: `setTransposeDegrees(n)` shifts every emitted note by n scale
  degrees; the gate-off matches the transposed note; offset 0 = home; negative
  transposes down.
- `BerlinMode::onMidiIn`: a NoteOn a degree above R0 sets the offset on all
  three voices (emitted notes shift); playing R0 returns home; NoteOff is
  ignored; an out-of-scale incoming note is quantized first; `onEnter` resets
  to 0.
- Roll: `renderRoll` after a transpose draws the shifted notes (a transposed
  pitch's lane appears).
- Manuals (EN+CZ) + CLAUDE.md updated (Berlin reacts to MIDI-in as a
  transposition control).

## 7. Out of scope

Chromatic / non-diatonic transposition; momentary (note-held) mode; a UI
toggle (always on in Berlin); passing the incoming note through to output;
persisting the offset.
