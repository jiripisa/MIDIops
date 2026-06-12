# Mode presets (Arp/Berlin slots) — design

Status: approved in brainstorm · Date: 2026-06-12.

Arp and Berlin each gain a fifth screen **presets** with three actions —
**Save / Load / Delete** — over **20 numbered slots**. Berlin presets include
the realized sequence, so a load brings back the exact pattern, not a
re-roll. Builds on the v0.14 `core/Storage.h` layer (one key per slot).

## 1. UX (per the brainstorm decisions)

* Idle screen shows the three actions: **Enc1 press = Save, Enc2 = Load,
  Enc3 = Delete**.
* A press opens the **slot picker**: a 5×4 grid of slots 01–20. Used slots
  are bright, empty ones dim, the selection is framed. Rotating any of
  Enc1–4 moves the selection (wraps); the slot index is remembered across
  picker openings (save → load round-trips stay on the slot).
* **Confirm = pressing the same encoder again.** Pressing a different
  encoder cancels; 5 s without input cancels; leaving the screen cancels.
* **No second confirmation step**: Save overwrites a used slot directly,
  Delete deletes directly (the flow is already deliberate).
* Save and Load close the picker on success; Delete keeps it open (bulk
  cleanup) and refreshes the used marks. Load/Delete on an empty slot do
  nothing but flash `EMPTY`. Feedback (`SAVED 07`, `LOADED 07`, …) shows
  for ~1.5 s.
* **Berlin Load while playing swaps seamlessly**: params + sequence replace
  the current ones, the playhead keeps running (wrapped into the new
  length), playing state untouched.

## 2. Storage

* `Storage` gains `exists(key)` (all three backends + FakeStorage).
* Keys: `arp.s01`…`arp.s20`, `berlin.s01`…`berlin.s20`.
* `core/Presets.{h,cpp}` owns the wire formats (explicit bytes, magic +
  version, whole-blob validation — a corrupt slot loads as empty):
  * **Arp v1** (14 B): `MARP`, ver, steps, rate, gate, direction, octave,
    swing, velocityMode, fixedVelocity, latch.
  * **Berlin v1** (214 B): `MBER`, ver, the 16 `BerlinParams` bytes, seq
    length, then 32 × 6 step bytes (active, note, velocity, accent,
    gateTicks, velJitter).

## 3. Code shape

* `core/app/PresetScreen.{h,cpp}`: one generic `Screen` implementing the
  whole picker state machine against a tiny `PresetOps` interface
  (`presetUsed / savePreset / loadPreset / deletePresetSlot`). Both modes
  embed it; the logic is written and tested once.
* `AppServices::storage()` (default nullptr) exposes the shell's Storage to
  modes; no storage → actions fail gracefully with `ERROR`.
* `BerlinEngine::setSequence(const BerlinSequence&)`: replace `seq_`, wrap
  the playhead, never touch playing state or the sounding gate.
* Modes implement `PresetOps`: Arp saves/loads `ArpParams` (incl. Hold);
  Berlin saves/loads `BerlinParams` + the engine sequence and re-points the
  generator after a load.

## 4. Testing

Round-trip + corruption rejection for both blob formats; picker state
machine (open/rotate-wrap/confirm/cancel/timeout/slot memory; empty-slot
load/delete; delete-stays-open). Mode-level: Arp params survive a
save→change→load cycle; a Berlin preset loaded into a fresh mode restores
the sequence byte-for-byte; Berlin load mid-play keeps playing with the
playhead wrapped. Manuals (EN+CZ) + CLAUDE.md updated.
