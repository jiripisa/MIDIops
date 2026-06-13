# Berlin Lead Voice Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a fourth Berlin voice — **Lead** — a sparse, high-register melody that does call-and-response with High (plays in High's gaps), with its own channel/mute/colour and full UI/preset support.

**Architecture:** The multi-voice machinery is already loop-driven over `BerlinMode::kVoices`, so most of the work is making the few fixed-at-3 spots (preset blob, the per-voice cell renderer, the colour/name tables) count-generic FIRST, then flipping `kVoices` 3→4 and adding the Lead role + call-and-response. Each task keeps the suite green.

**Tech Stack:** portable C++17 `core/` (no platform headers, no exceptions), PlatformIO + Unity (`pio test -e test`), builds `pio run -e native` / `pio run -e teensy41`.

**Spec:** `docs/specs/2026-06-13-berlin-lead-voice-design.md`.

**Task order (each green):** 1 presets count-generic → 2 render count-generic + 4 colours → 3 flip to 4 voices + Lead defaults + test updates → 4 call-and-response → 5 docs.

---

### Task 1: Presets become voice-count-generic (v3)

Make the Berlin preset blob handle any voice count (1..4) via a `count` parameter and bump the version byte to 3, so the format follows `kVoices` when it flips. The exact-size load + version check make older blobs read as empty.

**Files:**
- Modify: `core/Presets.h`
- Modify: `core/Presets.cpp`
- Modify: `core/modes/BerlinMode.cpp` (3 preset call sites)
- Test: `test/test_presets/test_presets.cpp`

- [ ] **Step 1: Update the test** (`test/test_presets/test_presets.cpp`). The existing Berlin tests call the v2 functions with no count. Change them to pass a count and exercise version-3 behaviour. Replace the `test_berlin_preset2_round_trip_three_voices` and `test_berlin_v1_blob_reads_as_empty` calls so every `saveBerlinPreset2`/`loadBerlinPreset2`/`berlinPreset2Usable` call takes a trailing count argument equal to the array size used (`3` in these tests). Concretely:
  - In `test_berlin_preset2_round_trip_three_voices`: `core::berlinPreset2Usable(st, 3)` → `core::berlinPreset2Usable(st, 3, 3)`; `core::saveBerlinPreset2(st, 3, in)` → `core::saveBerlinPreset2(st, 3, in, 3)`; `core::loadBerlinPreset2(st, 3, out)` → `core::loadBerlinPreset2(st, 3, out, 3)`.
  - In `test_berlin_v1_blob_reads_as_empty`: `core::berlinPreset2Usable(st, 0)` → `core::berlinPreset2Usable(st, 0, 3)`; `core::loadBerlinPreset2(st, 0, out)` → `core::loadBerlinPreset2(st, 0, out, 3)`.

- [ ] **Step 2: Run to verify it fails**

Run: `pio test -e test -f test_presets 2>&1 | tail -5`
Expected: compile error — too many arguments to `saveBerlinPreset2` / `berlinPreset2Usable`. You MUST see this first.

- [ ] **Step 3: Update `core/Presets.h`.** Replace the three Berlin declarations and their comment with:

```cpp
// Berlin preset v3: one slot = the whole voice stack (`count` voices, 1..4).
// Bytes: 'M','B','E','R', version 3, then count x (16 params bytes, seq
// length, 32x6 step bytes, channel, mute). A blob of a different voice count
// (different size) or older version fails the load and reads as empty.
struct BerlinVoicePreset {
    BerlinParams   params;
    BerlinSequence seq;
    uint8_t        channel = 1;
    bool           muted   = false;
};

bool saveBerlinPreset2(Storage& st, int slot, const BerlinVoicePreset* v, int count);
bool loadBerlinPreset2(Storage& st, int slot, BerlinVoicePreset* v, int count);
bool berlinPreset2Usable(Storage& st, int slot, int count);
```

- [ ] **Step 4: Update `core/Presets.cpp`.** Replace the `kBerlinBlobLen2` constant (line ~13) with:

```cpp
constexpr int     kBerlinMaxVoices     = 4;
constexpr int     kBerlinBlobMax       = 5 + kBerlinMaxVoices * kBerlinVoiceBlock;  // 849
constexpr uint8_t kBerlinPresetVersion = 3;
```

Then replace the three functions (`saveBerlinPreset2`, `loadBerlinPreset2`, `berlinPreset2Usable`) wholesale with:

```cpp
bool saveBerlinPreset2(Storage& st, int slot, const BerlinVoicePreset* v, int count) {
    if (!validSlot(slot) || count < 1 || count > kBerlinMaxVoices) return false;
    const int len = 5 + count * kBerlinVoiceBlock;
    uint8_t b[kBerlinBlobMax] = {'M', 'B', 'E', 'R', kBerlinPresetVersion};
    for (int i = 0; i < count; ++i)
        encodeBerlinVoice(b + 5 + i * kBerlinVoiceBlock, v[i]);
    char key[24];
    presetKey("berlin", slot, key, sizeof key);
    return st.save(key, b, len);
}

bool loadBerlinPreset2(Storage& st, int slot, BerlinVoicePreset* v, int count) {
    if (!validSlot(slot) || count < 1 || count > kBerlinMaxVoices) return false;
    const int len = 5 + count * kBerlinVoiceBlock;
    char key[24];
    presetKey("berlin", slot, key, sizeof key);
    uint8_t b[kBerlinBlobMax];
    if (!st.load(key, b, len)) return false;   // wrong size (other count) fails here
    if (b[0] != 'M' || b[1] != 'B' || b[2] != 'E' || b[3] != 'R' ||
        b[4] != kBerlinPresetVersion)
        return false;
    BerlinVoicePreset tmp[kBerlinMaxVoices];
    for (int i = 0; i < count; ++i)
        if (!decodeBerlinVoice(b + 5 + i * kBerlinVoiceBlock, tmp[i])) return false;
    for (int i = 0; i < count; ++i) v[i] = tmp[i];   // never a partial apply
    return true;
}

bool berlinPreset2Usable(Storage& st, int slot, int count) {
    if (!validSlot(slot) || count < 1 || count > kBerlinMaxVoices) return false;
    const int len = 5 + count * kBerlinVoiceBlock;
    char key[24];
    presetKey("berlin", slot, key, sizeof key);
    uint8_t b[kBerlinBlobMax];
    if (!st.load(key, b, len)) return false;
    return b[0] == 'M' && b[1] == 'B' && b[2] == 'E' && b[3] == 'R' &&
           b[4] == kBerlinPresetVersion;
}
```

- [ ] **Step 5: Update the 3 call sites in `core/modes/BerlinMode.cpp`** to pass `kVoices`:
  - `presetUsed`: `return st && berlinPreset2Usable(*st, slot, kVoices);`
  - `savePreset`: `return saveBerlinPreset2(*st, slot, v, kVoices);`
  - `loadPreset`: `if (!loadBerlinPreset2(*st, slot, v, kVoices)) return false;`

- [ ] **Step 6: Run to verify it passes**

Run: `pio test -e test 2>&1 | tail -3` → all PASSED (still 3 voices). Then both builds: `pio run -e native 2>&1 | tail -1 && pio run -e teensy41 2>&1 | tail -1` → SUCCESS.

- [ ] **Step 7: Commit**

```bash
git add core/Presets.h core/Presets.cpp core/modes/BerlinMode.cpp test/test_presets/test_presets.cpp
git commit -m "feat(presets): Berlin preset blob is voice-count-generic (v3)"
```

---

### Task 2: Per-voice cell renderer becomes count-generic; 4 voice colours/names

Make `drawBerlinVoiceCell` take an array of value strings + a count (so it renders any number of voice rows), keep the 3-voice layout pixel-identical, and extend the colour/name tables to four (adding Lead = magenta). Still 3 voices after this task; the look is unchanged.

**Files:**
- Modify: `core/render/BerlinLayout.h`
- Modify: `core/modes/BerlinMode.cpp` (StructureScreen::render, CharacterScreen::render)

- [ ] **Step 1: Replace `drawBerlinVoiceCell` in `core/render/BerlinLayout.h`** with a count-generic version:

```cpp
// Per-voice parameter cell (structure / character screens): the parameter name
// on top, then the voice values stacked top->bottom by register (the highest-
// index voice on top). All size 2; the active voice's value is white, the
// others DarkGray. The 3-voice layout is preserved pixel-for-pixel; the row
// pitch tightens for 4 so every row fits the 78px strip. vals[v] is voice v's
// value string (v: 0=Bass ..); activeVoice is 0..count-1.
inline void drawBerlinVoiceCell(Display& d, int col, const char* name,
                                const char* const vals[], int count,
                                int activeVoice) {
    const int x = col * kBerlinCellW;
    d.drawText(x + 4, kBerlinParamTop + 3, name, color::Gray, color::Black, 1);
    const int pitch = (count <= 3) ? 18 : 14;
    const int first = (count <= 3) ? 22 : 18;
    for (int v = 0; v < count; ++v) {
        const uint16_t fg = (v == activeVoice) ? color::White : color::DarkGray;
        const int row = (count - 1) - v;       // highest-index voice on top
        d.drawText(x + 4 + kValueIndent, kBerlinParamTop + first + row * pitch,
                   vals[v], fg, color::Black, 2);
    }
}
```

- [ ] **Step 2: Extend the colour/name tables in `core/render/BerlinLayout.h`** (the `kBerlinVoiceColors[3]` / `kBerlinVoiceNames[3]` near the multi-roll code) to four entries:

```cpp
constexpr uint16_t kBerlinVoiceColors[4] = {
    rgb565(90, 140, 255),    // Bass — blue
    color::Green,            // Mid
    rgb565(255, 150, 40),    // High — orange
    rgb565(230, 70, 200),    // Lead — magenta
};
constexpr const char* kBerlinVoiceNames[4] = {"BASS", "MID", "HIGH", "LEAD"};
```

- [ ] **Step 3: Update `StructureScreen::render` in `core/modes/BerlinMode.cpp`** to build pointer arrays and call the new signature. Replace the four `drawBerlinVoiceCell(...)` calls and add the pointer arrays just before them:

```cpp
    const char* algoP[kVoices]; const char* lenP[kVoices];
    const char* densP[kVoices]; const char* aprmP[kVoices];
    for (int v = 0; v < kVoices; ++v) {
        algoP[v] = algo[v]; lenP[v] = len[v]; densP[v] = dens[v]; aprmP[v] = aprm[v];
    }
    drawBerlinVoiceCell(d, 0, "ALGO",    algoP, kVoices, active);
    drawBerlinVoiceCell(d, 1, "LENGTH",  lenP,  kVoices, active);
    drawBerlinVoiceCell(d, 2, "DENSITY", densP, kVoices, active);
    drawBerlinVoiceCell(d, 3, aprmName,  aprmP, kVoices, active);
```

(The `char algo[kVoices][12]` etc. buffers and the per-voice fill loop above them stay as they are.)

- [ ] **Step 4: Update `CharacterScreen::render` the same way.** Replace its four `drawBerlinVoiceCell(...)` calls and add the pointer arrays before them:

```cpp
    const char* gateP[kVoices]; const char* tensP[kVoices];
    const char* octP[kVoices];  const char* rngP[kVoices];
    for (int v = 0; v < kVoices; ++v) {
        gateP[v] = gate[v]; tensP[v] = tens[v]; octP[v] = oct[v]; rngP[v] = rng[v];
    }
    drawBerlinVoiceCell(d, 0, "GATE",    gateP, kVoices, active);
    drawBerlinVoiceCell(d, 1, "TENSION", tensP, kVoices, active);
    drawBerlinVoiceCell(d, 2, "OCT",     octP,  kVoices, active);
    drawBerlinVoiceCell(d, 3, "RANGE",   rngP,  kVoices, active);
```

- [ ] **Step 5: Run to verify nothing broke**

Run: `pio test -e test 2>&1 | tail -3` → all PASSED (3-voice cell renders identically; existing `test_per_voice_cells_show_all_three_highlighted` still passes). Then both builds clean.

- [ ] **Step 6: Commit**

```bash
git add core/render/BerlinLayout.h core/modes/BerlinMode.cpp
git commit -m "feat(berlin): per-voice cell renderer is count-generic; 4 voice colours/names"
```

---

### Task 3: Flip to four voices + add the Lead role

Now flip `kVoices` to 4, add the `kLead` role and its defaults, drop the now-dead Enc4-mute shortcut, and update/extend the tests. Everything count-driven (mixer, roll, presets, cells) follows automatically.

**Files:**
- Modify: `core/modes/BerlinMode.h` (`kVoices`, `enum VoiceId`, `onVoiceScreenPress`)
- Modify: `core/modes/BerlinMode.cpp` (constructor Lead defaults)
- Test: `test/test_berlin_mode/test_berlin_mode.cpp`

- [ ] **Step 1: Write/adjust the tests first.** Make these edits in `test/test_berlin_mode/test_berlin_mode.cpp`:

  (a) **`test_voices_emit_on_their_channels`** — add channel 4 and make it robust to call-and-response (Task 4): set High sparse and Lead dense and regenerate, so Lead is guaranteed gaps to speak in even after masking; play a full loop so every voice emits. Replace its body with:

```cpp
static void test_voices_emit_on_their_channels() {
    core::AppShell shell;
    core::BerlinMode berlin(shell);
    FakeMidiOutput out; berlin.setMidiOutput(&out);
    berlin.onEnter();
    // High sparse (only its step-0 anchor), Lead dense → after call-and-response
    // masking Lead still speaks in High's gaps. Regenerate so densities apply.
    berlin.params(core::BerlinMode::kHigh).density = 0;
    berlin.params(core::BerlinMode::kLead).density = 100;
    berlin.onRawInput({core::RawInput::Kind::Latch, 3, 0, false});  // prime Latch3
    berlin.onRawInput({core::RawInput::Kind::Latch, 3, 0, true});   // generate
    berlin.onRawInput({core::RawInput::Kind::Latch, 1, 0, false});  // prime Latch1
    berlin.onRawInput({core::RawInput::Kind::Latch, 1, 0, true});   // play all
    for (int i = 0; i < 12 * 16; ++i) berlin.onClockTick();         // a full loop
    bool ch1 = false, ch2 = false, ch3 = false, ch4 = false, other = false;
    for (const auto& ev : out.events) {
        if (!ev.isOn) continue;
        if      (ev.channel == 1) ch1 = true;
        else if (ev.channel == 2) ch2 = true;
        else if (ev.channel == 3) ch3 = true;
        else if (ev.channel == 4) ch4 = true;
        else                      other = true;
    }
    TEST_ASSERT_TRUE(ch1);
    TEST_ASSERT_TRUE(ch2);
    TEST_ASSERT_TRUE(ch3);
    TEST_ASSERT_TRUE(ch4);
    TEST_ASSERT_FALSE(other);
}
```

  (b) **`test_three_voices_tick_and_phase`** — add the Lead playhead assertion. After the existing `kHigh` playhead assert (`...kHigh).playhead())`), add:

```cpp
    TEST_ASSERT_EQUAL_INT(0, berlin.engine(core::BerlinMode::kLead).playhead());
```

  (c) **`test_encoder_press_selects_voice_and_mutes`** — Enc4 now selects Lead (the mute shortcut is gone). Replace the whole function with:

```cpp
// On the per-voice screens each of Enc1/2/3/4 selects a voice directly
// (Bass/Mid/High/Lead). Mute lives only on the voices mixer now. Global
// screens (dynamics/behavior) ignore the press.
static void test_encoder_press_selects_voice() {
    core::AppShell shell;
    core::BerlinMode berlin(shell);
    TEST_ASSERT_EQUAL_INT(core::BerlinMode::kHigh, berlin.editVoice());
    berlin.screen(0).onEncoderSw(1);
    TEST_ASSERT_EQUAL_INT(core::BerlinMode::kBass, berlin.editVoice());
    berlin.screen(1).onEncoderSw(2);
    TEST_ASSERT_EQUAL_INT(core::BerlinMode::kMid, berlin.editVoice());
    berlin.screen(0).onEncoderSw(3);
    TEST_ASSERT_EQUAL_INT(core::BerlinMode::kHigh, berlin.editVoice());
    berlin.screen(1).onEncoderSw(4);
    TEST_ASSERT_EQUAL_INT(core::BerlinMode::kLead, berlin.editVoice());
    // Enc4 selects, it does NOT mute.
    TEST_ASSERT_FALSE(berlin.engine(core::BerlinMode::kLead).muted());
    // Global screens ignore the press.
    berlin.screen(3).onEncoderSw(1);
    TEST_ASSERT_EQUAL_INT(core::BerlinMode::kLead, berlin.editVoice());
}
```

  and in `main()` change `RUN_TEST(test_encoder_press_selects_voice_and_mutes);` to `RUN_TEST(test_encoder_press_selects_voice);`.

  (d) **`test_per_voice_cells_show_all_three_highlighted`** — High and Lead both default to octaveBase 60 ("C4"), so use the unique labels Mid (C3) and Bass (C1) for the colour assertions. Replace its body with:

```cpp
static void test_per_voice_cells_show_all_three_highlighted() {
    core::AppShell shell;
    core::BerlinMode berlin(shell);            // edit voice defaults to High
    core::Screen& character = berlin.screen(1);

    StubDisplay d;
    character.render(d);
    TEST_ASSERT_TRUE(d.drewText("C1"));        // Bass octave shown
    TEST_ASSERT_TRUE(d.drewText("C3"));        // Mid
    TEST_ASSERT_TRUE(d.drewText("C4"));        // High (and Lead share C4)

    // Select Mid (C3 is unique to Mid): active=white, an inactive (Bass C1)=dim.
    character.onEncoderSw(2);
    StubDisplay d2;
    character.render(d2);
    TEST_ASSERT_EQUAL_HEX16(core::color::White,    d2.textColor("C3"));
    TEST_ASSERT_EQUAL_HEX16(core::color::DarkGray, d2.textColor("C1"));
}
```

  (e) **Add two new tests** (place near the other voice tests) and register them in `main()`:

```cpp
// Lead role defaults (spec §2.3): high register, sparse, legato, channel 4.
static void test_lead_voice_defaults() {
    core::AppShell shell;
    core::BerlinMode berlin(shell);
    const core::BerlinParams& p = berlin.params(core::BerlinMode::kLead);
    TEST_ASSERT_EQUAL_INT(60, p.octaveBase);
    TEST_ASSERT_EQUAL_INT(2,  p.octaveRange);
    TEST_ASSERT_EQUAL_INT(30, p.density);
    TEST_ASSERT_EQUAL_INT(85, p.gatePercent);
    TEST_ASSERT_EQUAL_INT(4,  berlin.voiceChannel(core::BerlinMode::kLead));
}

// The voices mixer's fourth cell sets Lead's channel and mute.
static void test_voices_screen_controls_lead() {
    core::AppShell shell;
    core::BerlinMode berlin(shell);
    FakeMidiOutput out; berlin.setMidiOutput(&out);
    berlin.onEnter();
    core::Screen& mixer = berlin.screen(2);
    mixer.onEncoder(4, +2);                                 // Lead channel 4 -> 6
    TEST_ASSERT_EQUAL_INT(6, berlin.voiceChannel(core::BerlinMode::kLead));
    TEST_ASSERT_FALSE(berlin.engine(core::BerlinMode::kLead).muted());
    mixer.onEncoderSw(4);                                   // mute Lead
    TEST_ASSERT_TRUE(berlin.engine(core::BerlinMode::kLead).muted());
}
```

  `RUN_TEST(test_lead_voice_defaults);` and `RUN_TEST(test_voices_screen_controls_lead);` in `main()`.

- [ ] **Step 2: Run to verify failures** — `pio test -e test -f test_berlin_mode 2>&1 | tail -8` → multiple failures/compile errors referencing `kLead` (undeclared) and the new assertions. You MUST see these before implementing.

- [ ] **Step 3: Flip `core/modes/BerlinMode.h`.** Change `static constexpr int kVoices = 3;` to `4`, and the enum to add Lead:

```cpp
    static constexpr int kVoices = 4;
    enum VoiceId { kBass = 0, kMid = 1, kHigh = 2, kLead = 3 };
```

Simplify `onVoiceScreenPress` (the Enc4-mute branch is now dead — Enc4 selects Lead):

```cpp
    // Per-voice screen press: Enc1/2/3/4 select Bass/Mid/High/Lead directly.
    // (Mute lives on the voices mixer screen.)
    void onVoiceScreenPress(int index) {
        if (index >= 1 && index <= kVoices) setEditVoice(index - 1);
    }
```

- [ ] **Step 4: Add Lead defaults to the constructor in `core/modes/BerlinMode.cpp`** (after the `voices_[kHigh]` block, before the `for` loop that pushes params):

```cpp
    voices_[kLead].params.octaveBase  = 60;   // C4 (spans C4–C6 with range 2)
    voices_[kLead].params.octaveRange = 2;
    voices_[kLead].params.density     = 30;   // sparse, lots of rests
    voices_[kLead].params.gatePercent = 85;   // legato
    voices_[kLead].params.length      = 16;
    voices_[kLead].channel            = 4;
    // algorithm stays the default (DrunkardWalk), like Mid/High.
```

- [ ] **Step 5: Run the Berlin tests, then the full suite.**

Run: `pio test -e test -f test_berlin_mode 2>&1 | tail -5` → PASSED. Then `pio test -e test 2>&1 | tail -3`. If any OTHER test fails because it assumed three voices, fix it without weakening intent — likely candidates and the fix:
  - `test_voices_screen_channel_and_mute` (the original mixer test) may assert only three names; if so, it still passes (it doesn't assert "no LEAD"). If it iterates a fixed 3, leave it — it just doesn't touch Lead.
  - The mode-level preset round-trip tests now save/load four voices (via `kVoices`) automatically; their assertions target the edit voice and still hold.
  Report any test you change and why.

Then both builds: `pio run -e native 2>&1 | tail -1 && pio run -e teensy41 2>&1 | tail -1` → SUCCESS, no new warnings.

- [ ] **Step 6: Commit**

```bash
git add core/modes/BerlinMode.h core/modes/BerlinMode.cpp test/test_berlin_mode/test_berlin_mode.cpp
git commit -m "feat(berlin): add the Lead voice (4th) - role defaults, Enc4 selects Lead"
```

---

### Task 4: Call-and-response (Lead plays in High's gaps, at Generate)

After generating, bias Lead into High's rests: deactivate each Lead step whose aligned index collides with an active High step. Runs on Latch3 Generate and on the first-entry generation, before the consonance check.

**Files:**
- Modify: `core/modes/BerlinMode.h` (declare `maskLeadAgainstHigh`)
- Modify: `core/modes/BerlinMode.cpp` (implement + call in `onEnter` and the Latch3 case)
- Test: `test/test_berlin_mode/test_berlin_mode.cpp`

- [ ] **Step 1: Write the failing test** (append + register `RUN_TEST(test_call_response_lead_avoids_high);`)

```cpp
// Call-and-response: after Generate, no Lead active step coincides (by aligned
// index) with an active High step. Forcing both dense guarantees collisions to
// mask: High is all-active, so Lead must end up all rests.
static void test_call_response_lead_avoids_high() {
    core::AppShell shell;
    core::BerlinMode berlin(shell);
    berlin.onEnter();
    berlin.params(core::BerlinMode::kHigh).density = 100;
    berlin.params(core::BerlinMode::kLead).density = 100;
    berlin.onRawInput({core::RawInput::Kind::Latch, 3, 0, false});  // prime
    berlin.onRawInput({core::RawInput::Kind::Latch, 3, 0, true});   // Generate

    const core::BerlinSequence& lead = berlin.engine(core::BerlinMode::kLead).sequence();
    const core::BerlinSequence& high = berlin.engine(core::BerlinMode::kHigh).sequence();
    const int hlen = high.length() < 1 ? 1 : high.length();
    int leadActive = 0;
    for (int i = 0; i < lead.length(); ++i) {
        if (!lead.step(i).active) continue;
        ++leadActive;
        TEST_ASSERT_FALSE(high.step(i % hlen).active);   // never coincides
    }
    TEST_ASSERT_EQUAL_INT(0, leadActive);                // dense High masks all Lead steps
}
```

- [ ] **Step 2: Run to verify it fails** — `pio test -e test -f test_berlin_mode 2>&1 | tail -5` → FAIL (Lead keeps active steps that collide with High). You MUST see this first.

- [ ] **Step 3: Declare in `core/modes/BerlinMode.h`** next to `enforceConsonance();`:

```cpp
    void maskLeadAgainstHigh();   // call-and-response: Lead plays in High's gaps
```

- [ ] **Step 4: Implement in `core/modes/BerlinMode.cpp`** (next to `enforceConsonance`):

```cpp
// Call-and-response (spec §8): deactivate each Lead step whose aligned index
// collides with an active High step, so Lead tends to play in High's gaps.
// Approximate under phasing (alignment drifts over loops), applied at Generate.
void BerlinMode::maskLeadAgainstHigh() {
    BerlinSequence& lead = voices_[kLead].engine.sequenceMut();
    const BerlinSequence& high = voices_[kHigh].engine.sequence();
    const int hlen = high.length() < 1 ? 1 : high.length();
    for (int i = 0; i < lead.length(); ++i)
        if (lead.step(i).active && high.step(i % hlen).active)
            lead.step(i).active = false;
}
```

- [ ] **Step 5: Call it before the consonance pass in both places (`core/modes/BerlinMode.cpp`).**
  - In `onEnter`, change `if (generated) enforceConsonance();` to:

```cpp
    if (generated) { maskLeadAgainstHigh(); enforceConsonance(); }
```

  - In `onRawInput`, the Latch3 (`case 3:`) branch, after the per-voice generate loop and before `enforceConsonance();`, add `maskLeadAgainstHigh();` so it reads:

```cpp
                for (int v = 0; v < kVoices; ++v) {
                    voices_[v].engine.setParams(voices_[v].params);
                    applyGenerator(v);
                    voices_[v].engine.generate();
                }
                maskLeadAgainstHigh();
                enforceConsonance();
```

- [ ] **Step 6: Run to verify it passes** — `pio test -e test -f test_berlin_mode 2>&1 | tail -3` → PASSED. Then full suite + both builds: `pio test -e test 2>&1 | tail -3 && pio run -e native 2>&1 | tail -1 && pio run -e teensy41 2>&1 | tail -1` → all PASSED, both SUCCESS.

- [ ] **Step 7: Commit**

```bash
git add core/modes/BerlinMode.h core/modes/BerlinMode.cpp test/test_berlin_mode/test_berlin_mode.cpp
git commit -m "feat(berlin): Lead call-and-response - plays in High's gaps at Generate"
```

---

### Task 5: Documentation (manuals EN+CZ, CLAUDE.md)

**Files:**
- Modify: `MANUAL.md` (§5.3 Berlin)
- Modify: `MANUAL.cs.md` (same section)
- Modify: `CLAUDE.md` (Berlin bullet)

- [ ] **Step 1: Update `MANUAL.md` §5.3.** Find every place that says Berlin has three voices / Bass/Mid/High and update to four (Bass/Mid/High/Lead). Specifically:
  - The intro sentence listing the voices: add **Lead** — "a sparse, high-register melody that does call-and-response with High (it plays in High's gaps)".
  - The per-voice screen note: "pressing Enc1 / Enc2 / Enc3 / Enc4 selects the voice directly (Bass / Mid / High / Lead); **mute lives on the `voices` mixer**" (the Enc4 mute shortcut is gone).
  - The per-voice cell description: "all four voices' values stacked (Lead on top, then High, Mid, Bass)".
  - The piano-roll description: add "Lead = magenta" to the colour list.
  - The presets line: "a slot stores all four voices; slots saved before the Lead update read as empty".
  Keep the wording style of the surrounding section.

- [ ] **Step 2: Update `MANUAL.cs.md` §5.3** with the faithful Czech equivalent of every change in Step 1 (same positions; informal "ty"; technical terms as in the existing Czech text). E.g. the voice list adds **Lead** ("řídká, vysoká melodie hrající call-and-response s High — v jeho mezerách"); Enc4 selects Lead, mute jen na mixeru; buňky stackují čtyři hodnoty (shora Lead/High/Mid/Bass); roll barva Lead = purpurová; preset ukládá čtyři hlasy, starší sloty se tváří prázdné.

- [ ] **Step 3: Update the Berlin bullet in `CLAUDE.md`** (mode list). Update "three voices — Bass … Mid … High" to "four voices — Bass … Mid … High … Lead" and append: "Lead is a sparse high-register voice that does call-and-response with High (deactivated where it collides with High at Generate, `maskLeadAgainstHigh`). On the per-voice screens Enc1/2/3/4 select Bass/Mid/High/Lead; mute is on the mixer. Presets store all four voices (v3 blob)."

- [ ] **Step 4: Verify** docs-only (suite unchanged green) and the manuals stay structurally identical:

Run: `pio test -e test 2>&1 | tail -3` and `grep -c "^###\|^##" MANUAL.md MANUAL.cs.md` (counts must match).

- [ ] **Step 5: Commit**

```bash
git add MANUAL.md MANUAL.cs.md CLAUDE.md
git commit -m "docs: Berlin Lead voice (manuals EN+CZ, project notes)"
```
