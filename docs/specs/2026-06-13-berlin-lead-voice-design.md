# Berlin — Lead voice (4th voice) — design

Status: approved in brainstorm · Date: 2026-06-13.

Berlin grows from three voices to four by adding a **Lead**: a sparse,
high-register conversational melody that does **call-and-response** with the
High voice (it tends to play in High's gaps) per the Berlin School spec
(§2.3, §8). The existing multi-voice architecture (`voices_` array, per-voice
channel + mute, phasing, diatonic MIDI transpose) scales to four voices; this
adds the voice, its defaults, the call-and-response bias, and the UI/preset
plumbing for a fourth voice.

Spec reference: `synthseeker/berlin-school-theory-and-generator-spec-EN.md`
§2.3 (voice roles), §8 (call-and-response).

## 1. Architecture

* `BerlinMode::kVoices` 3 → 4; new role `kLead = 3` in `enum VoiceId`. Every
  per-voice loop (clock tick, transport, Generate, mute, MIDI transpose,
  renderRoll, presets) already iterates `kVoices`, so they scale automatically.
* Lead uses the **same three algorithms** as Mid/High (Walk / Phase / Degree),
  selectable on `structure` — unlike Bass, which is locked to its anchor
  generator.

## 2. Lead role defaults (spec §2.3)

| Field | Value | Why |
|---|---|---|
| octaveBase | 60 (C4) | high register |
| octaveRange | 2 | C4–C6 span |
| density | 30 | sparse, lots of rests |
| gatePercent | 85 | longer / legato |
| length | 16 | within the 32 cap (resolution is global) |
| channel | 4 | own timbre |
| algorithm | DrunkardWalk | conversational counter-line |

Sequence length stays ≤ `kMaxSteps` (32); "longer phrases" come from low
density + legato gate, not a larger step cap (raising `kMaxSteps` is out of
scope).

## 3. Call-and-response (at Generate)

After all voices are (re)generated — on Latch3 Generate and on the first-entry
generation in `onEnter` — a new step biases Lead into High's gaps: walk Lead's
active steps and, for each index `i`, if High has an active step at
`i % High.length()`, deactivate Lead's step (turn it into a rest). Lead then
"plays in High's gaps" on the aligned grid. With phasing the alignment drifts
over loops, but the generated pattern biases toward complementarity, and Lead
is sparse + high so residual overlaps are only echoes (spec §8). This runs
**before** the existing vertical consonance check, which then operates on the
final note set across all four voices. No runtime suppression (no glitches).

## 4. UI

* **structure / character (per-voice):** `onVoiceScreenPress` maps Enc1/2/3/4
  to selecting Bass/Mid/High/Lead directly. The Enc4-press mute shortcut is
  removed; **mute lives only on the `voices` mixer** now.
* **voices (mixer):** four cells (Bass/Mid/High/Lead). Rotate = that voice's
  MIDI channel, press = mute. Enc4 (previously unused) drives Lead.
* **per-voice cells:** stack all four values, top→bottom by register
  **Lead / High / Mid / Bass** (display row = `kVoices-1 - v`). Row pitch
  tightens to ~14 px so four size-2 rows fit the 78 px strip; the active
  voice's value is white, the others DarkGray.
* **piano-roll:** a fourth colour for Lead — **magenta** `rgb565(230,70,200)`,
  distinct from Bass blue / Mid green / High orange — with its own playhead.
  `kBerlinVoiceColors`/`kBerlinVoiceNames` grow to 4 ("LEAD").

## 5. Presets

Blob grows to **v3** = four voices (each: params + realized sequence +
channel + mute). The v2 (three-voice) blob fails the exact-size load, so old
slots read as empty and get overwritten — same precedent as v1 → v2.

## 6. Out of scope

Raising `kMaxSteps` for genuinely longer (4–8 bar) Lead phrases; runtime hard
alternation; a toggle to disable Lead (it is always present); per-voice
resolution.

## 7. Testing

- Lead defaults present (register/density/gate/channel/algorithm) after
  construction.
- Four voices tick and phase; the mixer's 4th cell sets Lead's channel + mute;
  Enc4 on a per-voice screen selects Lead (no longer mutes).
- Call-and-response: after Generate, no Lead active step coincides (by aligned
  index) with a High active step.
- Vertical consonance still runs across all four voices.
- Roll draws four colours incl. the Lead label/colour.
- Presets v3 round-trip (four voices byte-exact); a v2-sized blob reads as
  empty.
- MIDI diatonic transpose shifts the Lead voice too.
- Manuals (EN+CZ) + CLAUDE.md updated.
