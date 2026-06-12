# Multi-voice Berlin — Plan C: consonance check, presets v2, docs

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Generate runs the spec's vertical consonance check across the voice stack; Berlin preset slots store all three voices (v2 blob, old v1 slots read as empty); manuals and CLAUDE.md document the multi-voice mode.

**Architecture:** `berlinEnforceConsonance` joins the other helpers in BerlinGen; the v2 preset format reuses the per-voice encode/decode of v1 three times plus channel + mute. The single-voice v1 functions are deleted (their only callers were BerlinMode and tests).

**Prerequisite:** Plans A and B merged.

**Spec:** `docs/specs/2026-06-12-berlin-multivoice-design.md` §1 (consonance), §3 (presets); Berlin spec §2.4 step 3.

---

### Task 1: vertical consonance check

**Files:**
- Modify: `core/BerlinGen.h` (declaration)
- Modify: `core/BerlinGen.cpp` (implementation)
- Modify: `core/modes/BerlinMode.cpp` (call after Latch3 generate)
- Test: `test/test_berlin_generator/test_berlin_generator.cpp`

- [ ] **Step 1: Write the failing tests**

```cpp
// Spec §2.4 step 3: simultaneously sounding notes at interval class 1/6/11
// get the HIGHER note moved to the nearest in-scale tone that clears the
// clash. High tension (>60) keeps the grit: nothing moves.
static void test_consonance_check_moves_clashing_high_note() {
    core::Scale scale(core::Scale::Type::Minor, 0);       // C minor: C D Eb F G Ab Bb
    core::BerlinSequence a, b;
    a.setLength(4); b.setLength(4);
    for (int i = 0; i < 4; ++i) {
        a.step(i).active = true; a.step(i).note = 48;      // C3 root, all steps
        b.step(i).active = false;
    }
    b.step(0).active = true; b.step(0).note = 53;          // F3 vs C3: consonant
    b.step(1).active = true; b.step(1).note = 49;          // Db3 vs C3: interval
                                                           // class 1 = clash
    core::BerlinSequence* seqs[2] = {&a, &b};
    core::berlinEnforceConsonance(seqs, 2, scale, /*tension=*/30);

    TEST_ASSERT_EQUAL_INT(53, b.step(0).note);             // consonant: untouched
    // The clashing Db moved to an in-scale tone that no longer clashes with C.
    TEST_ASSERT_TRUE(scale.contains(b.step(1).note));
    int ic = (b.step(1).note - 48) % 12; if (ic < 0) ic += 12;
    TEST_ASSERT_TRUE(ic != 1 && ic != 6 && ic != 11);
}

static void test_consonance_check_skipped_at_high_tension() {
    core::Scale scale(core::Scale::Type::Minor, 0);
    core::BerlinSequence a, b;
    a.setLength(2); b.setLength(2);
    a.step(0).active = true; a.step(0).note = 48;
    b.step(0).active = true; b.step(0).note = 49;          // clash
    core::BerlinSequence* seqs[2] = {&a, &b};
    core::berlinEnforceConsonance(seqs, 2, scale, /*tension=*/80);
    TEST_ASSERT_EQUAL_INT(49, b.step(0).note);             // untouched
}

// Phasing alignment: voices of different lengths clash where columns
// coincide MOD length — a 2-step voice against a 3-step voice meets the
// clash at columns 1, 3, 5... the fix clears them all in one pass.
static void test_consonance_check_respects_phasing_alignment() {
    core::Scale scale(core::Scale::Type::Minor, 0);
    core::BerlinSequence a, b;
    a.setLength(3);
    a.step(1).active = true; a.step(1).note = 48;          // C on column 1 (mod 3)
    b.setLength(2);
    b.step(1).active = true; b.step(1).note = 49;          // Db on column 1 (mod 2)
    core::BerlinSequence* seqs[2] = {&a, &b};
    core::berlinEnforceConsonance(seqs, 2, scale, 30);
    int ic = (b.step(1).note - 48) % 12; if (ic < 0) ic += 12;
    TEST_ASSERT_TRUE(ic != 1 && ic != 6 && ic != 11);
}
```

- [ ] **Step 2: Run to verify failure** — `pio test -e test -f test_berlin_generator 2>&1 | tail -5` → compile error (`berlinEnforceConsonance` undeclared).

- [ ] **Step 3: Implement.** Declaration in `core/BerlinGen.h` (after `berlinDegreeWeightedNote`):

```cpp
// Spec §2.4 step 3 — vertical consonance across the phasing stack. Walks the
// first kMaxSteps time columns; wherever two voices' steps coincide
// (column % length) at an interval class of 1, 6 or 11 semitones, the HIGHER
// note is moved to the nearest in-scale tone (searching outward 1..6
// semitones) that clears the clash in that column. One pass, pragmatic per
// spec; skipped entirely when tension > 60 ("or leave it if Tension is high").
void berlinEnforceConsonance(BerlinSequence** seqs, int n, const Scale& scale,
                             int tensionPercent);
```

Implementation in `core/BerlinGen.cpp` (add `#include "core/BerlinSequence.h"` if not present):

```cpp
namespace {

bool berlinClash(int a, int b) {
    int ic = (a - b) % 12;
    if (ic < 0) ic += 12;
    return ic == 1 || ic == 6 || ic == 11;
}

} // namespace

void berlinEnforceConsonance(BerlinSequence** seqs, int n, const Scale& scale,
                             int tensionPercent) {
    if (tensionPercent > 60 || n < 2) return;
    for (int col = 0; col < BerlinSequence::kMaxSteps; ++col) {
        for (int a = 0; a < n; ++a) {
            BerlinStep& sa = seqs[a]->step(col % seqs[a]->length());
            if (!sa.active) continue;
            for (int b = a + 1; b < n; ++b) {
                BerlinStep& sb = seqs[b]->step(col % seqs[b]->length());
                if (!sb.active) continue;
                if (!berlinClash(sa.note, sb.note)) continue;
                BerlinStep& hiStep = sa.note >= sb.note ? sa : sb;
                const BerlinStep& loStep = sa.note >= sb.note ? sb : sa;
                for (int off = 1; off <= 6; ++off) {
                    bool fixed = false;
                    for (int sgn = -1; sgn <= 1; sgn += 2) {
                        const int cand = static_cast<int>(hiStep.note) + sgn * off;
                        if (cand < 0 || cand > 127) continue;
                        if (!scale.contains(static_cast<uint8_t>(cand))) continue;
                        if (berlinClash(cand, loStep.note)) continue;
                        hiStep.note = static_cast<uint8_t>(cand);
                        fixed = true;
                        break;
                    }
                    if (fixed) break;
                }
            }
        }
    }
}
```

Call it in `BerlinMode::onRawInput`, Latch3 case, after the generate loop (file already includes BerlinGen.h):

```cpp
        case 3:
            if (flip) {
                for (int v = 0; v < kVoices; ++v) {
                    voices_[v].engine.setParams(voices_[v].params);
                    applyGenerator(v);
                    voices_[v].engine.generate();
                }
                // Vertical consonance across the stack (spec §2.4 step 3);
                // the melodic voices' highest tension decides the skip.
                int tension = 0;
                for (int v = 0; v < kVoices; ++v)
                    if (voices_[v].params.tension > tension)
                        tension = voices_[v].params.tension;
                BerlinSequence* seqs[kVoices];
                for (int v = 0; v < kVoices; ++v)
                    seqs[v] = &voices_[v].engine.sequenceMut();
                berlinEnforceConsonance(seqs, kVoices, scale_, tension);
            }
            break;
```

- [ ] **Step 4: Run** — `pio test -e test -f test_berlin_generator -f test_berlin_mode 2>&1 | tail -5` → PASSED.

- [ ] **Step 5: Commit**

```bash
git add core/BerlinGen.h core/BerlinGen.cpp core/modes/BerlinMode.cpp test/test_berlin_generator/test_berlin_generator.cpp
git commit -m "feat(berlin): vertical consonance check across the voice stack at Generate"
```

---

### Task 2: presets v2 (three voices per slot)

**Files:**
- Modify: `core/Presets.h` (BerlinVoicePreset + v2 functions; delete v1 Berlin functions)
- Modify: `core/Presets.cpp`
- Modify: `core/modes/BerlinMode.cpp` (PresetOps)
- Test: `test/test_presets/test_presets.cpp`, `test/test_berlin_mode/test_berlin_mode.cpp`

**v2 wire format**, key `berlin.sNN`, fixed 638 bytes: `'M','B','E','R'`, version `2`, then 3× voice block (211 B each): 16 params bytes (same order as v1: algorithm, length, resolution, density, gatePercent, tension, octaveBase, octaveRange, velocityBase, velocityHumanize, accent, scatter, gateLen, behavior, morph, evolveRate), seq length, 32 × 6 step bytes (active, note, velocity, accent, gateTicks, velJitter), channel, mute. Whole-blob validation; any failure = slot behaves empty. The old 214-byte v1 blob fails the exact-size load, so **v1 slots read as empty** and are overwritten on the next save.

- [ ] **Step 1: Rewrite the Berlin part of `test/test_presets/test_presets.cpp`.** Replace `test_berlin_preset_round_trip_includes_sequence` with:

```cpp
static void test_berlin_preset2_round_trip_three_voices() {
    FakeStorage st;
    core::BerlinVoicePreset in[3];
    for (int v = 0; v < 3; ++v) {
        in[v].params.length  = static_cast<uint8_t>(14 + v);
        in[v].params.density = static_cast<uint8_t>(30 + v * 10);
        in[v].channel        = static_cast<uint8_t>(4 + v);
        in[v].muted          = (v == 1);
        in[v].seq.setLength(14 + v);
        for (int i = 0; i < in[v].seq.length(); ++i) {
            core::BerlinStep& s = in[v].seq.step(i);
            s.active = (i % 2) == 0;
            s.note = static_cast<uint8_t>(36 + v * 12 + i);
            s.velocity = static_cast<uint8_t>(70 + i);
            s.gateTicks = 6;
            s.velJitter = static_cast<int8_t>(i - 5);
        }
    }
    TEST_ASSERT_FALSE(core::berlinPreset2Usable(st, 3));
    TEST_ASSERT_TRUE(core::saveBerlinPreset2(st, 3, in));
    TEST_ASSERT_TRUE(core::berlinPreset2Usable(st, 3));
    core::BerlinVoicePreset out[3];
    TEST_ASSERT_TRUE(core::loadBerlinPreset2(st, 3, out));
    for (int v = 0; v < 3; ++v) {
        TEST_ASSERT_EQUAL_INT(in[v].params.length, out[v].params.length);
        TEST_ASSERT_EQUAL_INT(in[v].params.density, out[v].params.density);
        TEST_ASSERT_EQUAL_INT(in[v].channel, out[v].channel);
        TEST_ASSERT_EQUAL_INT(in[v].muted ? 1 : 0, out[v].muted ? 1 : 0);
        TEST_ASSERT_EQUAL_INT(in[v].seq.length(), out[v].seq.length());
        for (int i = 0; i < in[v].seq.length(); ++i) {
            TEST_ASSERT_EQUAL_INT(in[v].seq.step(i).note, out[v].seq.step(i).note);
            TEST_ASSERT_EQUAL_INT(in[v].seq.step(i).velocity, out[v].seq.step(i).velocity);
            TEST_ASSERT_EQUAL_INT(in[v].seq.step(i).velJitter, out[v].seq.step(i).velJitter);
        }
    }
}

// A v1-sized blob under the same key reads as EMPTY, never as garbage.
static void test_berlin_v1_blob_reads_as_empty() {
    FakeStorage st;
    st.data["berlin.s01"] = std::vector<uint8_t>(214, 0);  // v1-sized junk
    TEST_ASSERT_FALSE(core::berlinPreset2Usable(st, 0));
    core::BerlinVoicePreset out[3];
    TEST_ASSERT_FALSE(core::loadBerlinPreset2(st, 0, out));
}
```

Update `main()` registrations accordingly.

- [ ] **Step 2: Run to verify failure** — `pio test -e test -f test_presets 2>&1 | tail -5` → compile error (`BerlinVoicePreset` undeclared).

- [ ] **Step 3: Implement in `core/Presets.h`.** Replace the v1 Berlin declarations (`saveBerlinPreset`/`loadBerlinPreset`) with:

```cpp
// Berlin preset v2: one slot = the whole three-voice stack. 638 bytes —
// 'M','B','E','R', version 2, then 3x (16 params bytes, seq length, 32x6
// step bytes, channel, mute). A v1 (214-byte) blob fails the exact-size
// load, so old single-voice slots read as empty and get overwritten.
struct BerlinVoicePreset {
    BerlinParams   params;
    BerlinSequence seq;
    uint8_t        channel = 1;
    bool           muted   = false;
};

bool saveBerlinPreset2(Storage& st, int slot, const BerlinVoicePreset v[3]);
bool loadBerlinPreset2(Storage& st, int slot, BerlinVoicePreset v[3]);
bool berlinPreset2Usable(Storage& st, int slot);   // size+magic+version probe
```

In `core/Presets.cpp`, replace the two v1 Berlin functions with (the anonymous-namespace constants gain `kBerlinVoiceBlock` and the v2 length; `kBerlinBlobLen` for v1 is deleted):

```cpp
constexpr int kBerlinVoiceBlock = 16 + 1 + core::BerlinSequence::kMaxSteps * 6 + 2;  // 211
constexpr int kBerlinBlobLen2   = 5 + 3 * kBerlinVoiceBlock;                          // 638
```

```cpp
namespace {

void encodeBerlinVoice(uint8_t* b, const core::BerlinVoicePreset& v) {
    int o = 0;
    const core::BerlinParams& p = v.params;
    b[o++] = static_cast<uint8_t>(p.algorithm);
    b[o++] = p.length;
    b[o++] = static_cast<uint8_t>(p.resolution);
    b[o++] = p.density;
    b[o++] = p.gatePercent;
    b[o++] = p.tension;
    b[o++] = p.octaveBase;
    b[o++] = p.octaveRange;
    b[o++] = p.velocityBase;
    b[o++] = p.velocityHumanize;
    b[o++] = p.accent;
    b[o++] = p.scatter;
    b[o++] = p.gateLen;
    b[o++] = static_cast<uint8_t>(p.behavior);
    b[o++] = p.morph;
    b[o++] = p.evolveRate;
    b[o++] = static_cast<uint8_t>(v.seq.length());
    for (int i = 0; i < core::BerlinSequence::kMaxSteps; ++i) {
        const core::BerlinStep& s = v.seq.step(i);
        b[o++] = s.active ? 1 : 0;
        b[o++] = s.note;
        b[o++] = s.velocity;
        b[o++] = s.accent ? 1 : 0;
        b[o++] = static_cast<uint8_t>(s.gateTicks > 255 ? 255 : s.gateTicks);
        b[o++] = static_cast<uint8_t>(s.velJitter);
    }
    b[o++] = v.channel;
    b[o++] = v.muted ? 1 : 0;
}

// Returns false on any out-of-range field (whole-blob rejection).
bool decodeBerlinVoice(const uint8_t* b, core::BerlinVoicePreset& v) {
    using namespace core;
    int o = 0;
    const uint8_t algorithm  = b[o++];
    BerlinParams p;
    p.length                 = b[o++];
    const uint8_t resolution = b[o++];
    p.density                = b[o++];
    p.gatePercent            = b[o++];
    p.tension                = b[o++];
    p.octaveBase             = b[o++];
    p.octaveRange            = b[o++];
    p.velocityBase           = b[o++];
    p.velocityHumanize       = b[o++];
    p.accent                 = b[o++];
    p.scatter                = b[o++];
    p.gateLen                = b[o++];
    const uint8_t behavior   = b[o++];
    p.morph                  = b[o++];
    p.evolveRate             = b[o++];
    const uint8_t seqLen     = b[o++];
    if (algorithm >= static_cast<uint8_t>(BerlinAlgorithm::kCount)) return false;
    if (p.length < 3 || p.length > BerlinSequence::kMaxSteps) return false;
    if (resolution >= static_cast<uint8_t>(BerlinResolution::kCount)) return false;
    if (p.density > 100) return false;
    if (p.gatePercent < 40 || p.gatePercent > 99) return false;
    if (p.tension > 100) return false;
    if (p.octaveBase < 24 || p.octaveBase > 72) return false;
    if (p.octaveRange < 1 || p.octaveRange > 3) return false;
    if (p.velocityBase < 1 || p.velocityBase > 126) return false;
    if (p.velocityHumanize > 30) return false;
    if (p.accent > 27) return false;
    if (p.scatter < 1 || p.scatter > 7) return false;
    if (p.gateLen < 3 || p.gateLen > 16) return false;
    if (behavior >= static_cast<uint8_t>(BerlinBehavior::kCount)) return false;
    if (p.morph > 100) return false;
    if (p.evolveRate < 1 || p.evolveRate > 8) return false;
    if (seqLen < 1 || seqLen > BerlinSequence::kMaxSteps) return false;
    const uint8_t* steps = b + o;
    for (int i = 0; i < BerlinSequence::kMaxSteps; ++i) {
        const uint8_t* s = steps + i * 6;
        if (s[0] > 1 || s[1] > 127 || s[2] > 127 || s[3] > 1) return false;
    }
    p.algorithm  = static_cast<BerlinAlgorithm>(algorithm);
    p.resolution = static_cast<BerlinResolution>(resolution);
    p.behavior   = static_cast<BerlinBehavior>(behavior);
    v.params = p;
    v.seq.setLength(seqLen);
    for (int i = 0; i < BerlinSequence::kMaxSteps; ++i) {
        const uint8_t* s = steps + i * 6;
        BerlinStep& step = v.seq.step(i);
        step.active    = s[0] != 0;
        step.note      = s[1];
        step.velocity  = s[2];
        step.accent    = s[3] != 0;
        step.gateTicks = s[4];
        step.velJitter = static_cast<int8_t>(s[5]);
    }
    o += BerlinSequence::kMaxSteps * 6;
    const uint8_t channel = b[o++];
    const uint8_t muted   = b[o++];
    if (channel < 1 || channel > 16) return false;
    if (muted > 1) return false;
    v.channel = channel;
    v.muted   = muted != 0;
    return true;
}

} // namespace
```

```cpp
bool saveBerlinPreset2(Storage& st, int slot, const BerlinVoicePreset v[3]) {
    if (!validSlot(slot)) return false;
    uint8_t b[kBerlinBlobLen2] = {'M', 'B', 'E', 'R', 2};
    for (int i = 0; i < 3; ++i)
        encodeBerlinVoice(b + 5 + i * kBerlinVoiceBlock, v[i]);
    char key[24];
    presetKey("berlin", slot, key, sizeof key);
    return st.save(key, b, kBerlinBlobLen2);
}

bool loadBerlinPreset2(Storage& st, int slot, BerlinVoicePreset v[3]) {
    if (!validSlot(slot)) return false;
    char key[24];
    presetKey("berlin", slot, key, sizeof key);
    uint8_t b[kBerlinBlobLen2];
    if (!st.load(key, b, kBerlinBlobLen2)) return false;   // v1 size fails here
    if (b[0] != 'M' || b[1] != 'B' || b[2] != 'E' || b[3] != 'R' || b[4] != 2)
        return false;
    BerlinVoicePreset tmp[3];
    for (int i = 0; i < 3; ++i)
        if (!decodeBerlinVoice(b + 5 + i * kBerlinVoiceBlock, tmp[i])) return false;
    for (int i = 0; i < 3; ++i) v[i] = tmp[i];             // never a partial apply
    return true;
}

bool berlinPreset2Usable(Storage& st, int slot) {
    if (!validSlot(slot)) return false;
    char key[24];
    presetKey("berlin", slot, key, sizeof key);
    uint8_t b[kBerlinBlobLen2];
    if (!st.load(key, b, kBerlinBlobLen2)) return false;
    return b[0] == 'M' && b[1] == 'B' && b[2] == 'E' && b[3] == 'R' && b[4] == 2;
}
```

Note `kPresetVersion` stays 1 for the Arp blob; the Berlin v2 writes a literal `2`.

- [ ] **Step 4: Rewire `BerlinMode` PresetOps in `core/modes/BerlinMode.cpp`:**

```cpp
bool BerlinMode::presetUsed(int slot) {
    Storage* st = svc_.storage();
    return st && berlinPreset2Usable(*st, slot);
}

bool BerlinMode::savePreset(int slot) {
    Storage* st = svc_.storage();
    if (!st) return false;
    BerlinVoicePreset v[kVoices];
    for (int i = 0; i < kVoices; ++i) {
        v[i].params  = voices_[i].params;
        v[i].seq     = voices_[i].engine.sequence();
        v[i].channel = voices_[i].channel;
        v[i].muted   = voices_[i].engine.muted();
    }
    return saveBerlinPreset2(*st, slot, v);
}

bool BerlinMode::loadPreset(int slot) {
    Storage* st = svc_.storage();
    if (!st) return false;
    BerlinVoicePreset v[kVoices];
    if (!loadBerlinPreset2(*st, slot, v)) return false;
    for (int i = 0; i < kVoices; ++i) {
        voices_[i].params  = v[i].params;
        voices_[i].channel = v[i].channel;
        applyGenerator(i);
        voices_[i].engine.setParams(v[i].params);
        voices_[i].engine.setOutChannel(v[i].channel);
        voices_[i].engine.setMuted(v[i].muted);
        voices_[i].engine.setSequence(v[i].seq);   // mid-play: playhead wraps
    }
    syncGlobals();   // the loaded voice-0 globals become canonical everywhere
    return true;
}
```

- [ ] **Step 5: Update the two Berlin preset tests in `test/test_berlin_mode/test_berlin_mode.cpp`** — they still pass conceptually (save → mutate → load restores; mid-play load keeps playing), but `screen(5)` is the presets screen and the sequence comparison should target the edit voice via `berlin.engine().sequence()` as before. Verify both still assert what they did; adjust only the screen index if Plan A Task 4 did not already.

- [ ] **Step 6: Run** — `pio test -e test 2>&1 | tail -3` → all PASSED.

- [ ] **Step 7: Commit**

```bash
git add core/Presets.h core/Presets.cpp core/modes/BerlinMode.cpp test/test_presets/test_presets.cpp test/test_berlin_mode/test_berlin_mode.cpp
git commit -m "feat(berlin): presets v2 - one slot stores the whole three-voice stack"
```

---

### Task 3: manuals + CLAUDE.md

**Files:**
- Modify: `MANUAL.md` §5.3 (Berlin), §3 if needed
- Modify: `MANUAL.cs.md` (same sections, structurally identical)
- Modify: `CLAUDE.md` (Berlin bullet, control scheme note, milestones)

- [ ] **Step 1: MANUAL.md §5.3.** Update the intro (three voices, roles, colors), the screens line to `structure · character · voices · dynamics · behavior · presets`, re-split the parameter tables per the new knob map (structure: ALGO/LENGTH/DENSITY/ALGOPRM per voice with the Bass lock noted; character per voice; voices: per-voice channel rotate + mute press; dynamics global + RESOL; behavior global, Enc4 unused), document voice cycling ("pressing any Enc1–4 on structure/character cycles the edited voice; its name shows at the top right of the roll in the voice's colour"), the combined roll (colors, per-voice playheads, phasing), and presets ("a slot stores all three voices — params, sequences, channels, mutes; slots saved before the multi-voice update appear empty"). Keep every range/default consistent with the plan tables.

- [ ] **Step 2: MANUAL.cs.md** — faithful Czech translation, same structure, same tables.

- [ ] **Step 3: CLAUDE.md** — Berlin bullet gains "three voices (Bass/Mid/High) on per-voice MIDI channels with mute, phasing via per-voice lengths"; the control scheme section notes the per-voice screens' press = voice cycling; "Where future milestones plug in" gains "Lead voice (4th, call-and-response) per spec §2.3".

- [ ] **Step 4: Full suite + builds** — `pio test -e test 2>&1 | tail -3 && pio run -e native 2>&1 | tail -2 && pio run -e teensy41 2>&1 | tail -2` → all green.

- [ ] **Step 5: Commit**

```bash
git add MANUAL.md MANUAL.cs.md CLAUDE.md
git commit -m "docs: multi-voice Berlin - manuals (EN+CZ) and project notes"
```
