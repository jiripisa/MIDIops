# Multi-voice Berlin — Plan B: per-voice UI + combined colored roll

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Voice cycling on encoder press, the spec'd screen split (per-voice structure/character vs global dynamics/behavior, AlgoPrm shared cell, Resolution on dynamics), and one combined piano-roll showing all three voices in colors with per-voice playheads.

**Architecture:** Builds on Plan A (voices_[3], edit voice, mixer). The screens get re-laid-out; global knobs write through to ALL voices' params (`voices_[0].params` is the canonical copy of global fields, `syncGlobals()` copies them out). A new `drawBerlinMultiRoll` in BerlinLayout.h replaces `drawBerlinPianoRoll` in Berlin screens (the old function stays — tests and the layout header are shared).

**Prerequisite:** Plan A merged (`docs/plans/2026-06-12-berlin-multivoice-plan-a.md`).

**Spec:** `docs/specs/2026-06-12-berlin-multivoice-design.md` §2.

---

### Task 1: voice cycling on encoder press

**Files:**
- Modify: `core/modes/BerlinMode.h` (cycleEditVoice helper; StructureScreen/CharacterScreen get onEncoderSw)
- Modify: `core/modes/BerlinMode.cpp`
- Test: `test/test_berlin_mode/test_berlin_mode.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
// Pressing any Enc1-4 on a per-voice screen cycles the edited voice
// Bass -> Mid -> High -> Bass; the selection is shared across screens.
// Global screens (dynamics/behavior) do not cycle.
static void test_encoder_press_cycles_edit_voice() {
    core::AppShell shell;
    core::BerlinMode berlin(shell);
    TEST_ASSERT_EQUAL_INT(core::BerlinMode::kHigh, berlin.editVoice());
    berlin.screen(0).onEncoderSw(1);                       // structure: High -> Bass
    TEST_ASSERT_EQUAL_INT(core::BerlinMode::kBass, berlin.editVoice());
    berlin.screen(1).onEncoderSw(4);                       // character: Bass -> Mid
    TEST_ASSERT_EQUAL_INT(core::BerlinMode::kMid, berlin.editVoice());
    berlin.screen(3).onEncoderSw(1);                       // dynamics: global, no cycle
    TEST_ASSERT_EQUAL_INT(core::BerlinMode::kMid, berlin.editVoice());
}
```

- [ ] **Step 2: Run to verify failure** — `pio test -e test -f test_berlin_mode 2>&1 | tail -5` → fails (press is a no-op today; High stays).

- [ ] **Step 3: Implement.** In `BerlinMode.h` public section:

```cpp
    void cycleEditVoice() { editVoice_ = (editVoice_ + 1) % kVoices; }
```

In `StructureScreen` and `CharacterScreen` class declarations add:

```cpp
        void onEncoderSw(int index) override;
```

In `BerlinMode.cpp`:

```cpp
void BerlinMode::StructureScreen::onEncoderSw(int /*index*/) { mode_.cycleEditVoice(); }
void BerlinMode::CharacterScreen::onEncoderSw(int /*index*/) { mode_.cycleEditVoice(); }
```

- [ ] **Step 4: Run** — `pio test -e test -f test_berlin_mode 2>&1 | tail -3` → PASSED.

- [ ] **Step 5: Commit**

```bash
git add core/modes/BerlinMode.h core/modes/BerlinMode.cpp test/test_berlin_mode/test_berlin_mode.cpp
git commit -m "feat(berlin): encoder press cycles the edited voice on per-voice screens"
```

---

### Task 2: screen re-layout — structure AlgoPrm, global dynamics/behavior

New knob map (spec §2): structure = ALGO / LENGTH / DENSITY / ALGOPRM (per voice); character unchanged (per voice); dynamics = VEL / HUMAN / ACCENT / RESOL (global); behavior = BEHAVIOR / MORPH / EVOLVE / — (global).

**Files:**
- Modify: `core/modes/BerlinMode.h` (syncGlobals helper)
- Modify: `core/modes/BerlinMode.cpp` (four screen onEncoder/render bodies)
- Test: `test/test_berlin_mode/test_berlin_mode.cpp`

- [ ] **Step 1: Write the failing tests**

```cpp
// Structure Enc3 is now Density, Enc4 the algorithm-specific parameter
// (Scatter under Walk / GateLen under Phase); ALGO and ALGOPRM are locked
// for the Bass voice.
static void test_structure_new_layout_and_bass_locks() {
    core::AppShell shell;
    core::BerlinMode berlin(shell);
    berlin.setEditVoice(core::BerlinMode::kHigh);
    berlin.screen(0).onEncoder(3, +2);                     // density +10
    TEST_ASSERT_EQUAL_INT(60, berlin.params(core::BerlinMode::kHigh).density);
    berlin.screen(0).onEncoder(4, +2);                     // Walk: scatter 3 -> 5
    TEST_ASSERT_EQUAL_INT(5, berlin.params(core::BerlinMode::kHigh).scatter);
    berlin.params(core::BerlinMode::kHigh).algorithm =
        core::BerlinAlgorithm::GatePitchPhasing;
    berlin.screen(0).onEncoder(4, +2);                     // Phase: gateLen 6 -> 8
    TEST_ASSERT_EQUAL_INT(8, berlin.params(core::BerlinMode::kHigh).gateLen);

    berlin.setEditVoice(core::BerlinMode::kBass);
    const auto algoBefore = berlin.params(core::BerlinMode::kBass).algorithm;
    berlin.screen(0).onEncoder(1, +1);                     // ALGO locked for Bass
    TEST_ASSERT_EQUAL_INT(static_cast<int>(algoBefore),
        static_cast<int>(berlin.params(core::BerlinMode::kBass).algorithm));
    berlin.screen(0).onEncoder(4, +2);                     // ALGOPRM locked for Bass
    TEST_ASSERT_EQUAL_INT(3, berlin.params(core::BerlinMode::kBass).scatter);
}

// Dynamics and behavior are global: one knob writes every voice's params.
static void test_dynamics_and_behavior_are_global() {
    core::AppShell shell;
    core::BerlinMode berlin(shell);
    berlin.onEnter();
    berlin.screen(3).onEncoder(1, +10);                    // velocity 100 -> 110
    for (int v = 0; v < core::BerlinMode::kVoices; ++v)
        TEST_ASSERT_EQUAL_INT(110, berlin.params(v).velocityBase);
    berlin.screen(3).onEncoder(4, +1);                     // resolution 8th -> 16th
    for (int v = 0; v < core::BerlinMode::kVoices; ++v)
        TEST_ASSERT_EQUAL_INT(static_cast<int>(core::BerlinResolution::Sixteenth),
                              static_cast<int>(berlin.params(v).resolution));
    berlin.screen(4).onEncoder(1, +1);                     // behavior Live -> Lock
    for (int v = 0; v < core::BerlinMode::kVoices; ++v)
        TEST_ASSERT_EQUAL_INT(static_cast<int>(core::BerlinBehavior::Locked),
                              static_cast<int>(berlin.params(v).behavior));
}
```

- [ ] **Step 2: Run to verify failure** — `pio test -e test -f test_berlin_mode 2>&1 | tail -5` → fails (Enc3 on structure is still Resolution, globals don't propagate).

- [ ] **Step 3: Implement.** In `BerlinMode.h` private section add:

```cpp
    // Global fields (dynamics, resolution, behavior, morph, evolve) are
    // canonical in voices_[0].params; the global screens edit them there and
    // syncGlobals() copies them to every other voice.
    void syncGlobals() {
        const BerlinParams& g = voices_[0].params;
        for (int v = 1; v < kVoices; ++v) {
            BerlinParams& p = voices_[v].params;
            p.velocityBase     = g.velocityBase;
            p.velocityHumanize = g.velocityHumanize;
            p.accent           = g.accent;
            p.resolution       = g.resolution;
            p.behavior         = g.behavior;
            p.morph            = g.morph;
            p.evolveRate       = g.evolveRate;
            voices_[v].engine.setParams(p);
        }
        voices_[0].engine.setParams(voices_[0].params);
    }
```

and change `live()` to read the canonical copy:

```cpp
    bool live() const { return voices_[0].params.behavior == BerlinBehavior::Live; }
```

In `BerlinMode.cpp`, the new `StructureScreen`:

```cpp
void BerlinMode::StructureScreen::onEncoder(int index, int delta) {
    BerlinParams& p = mode_.editParams();
    const bool bass = mode_.editVoice() == kBass;
    switch (index) {
        case 1:  // Algorithm — Mid/High only; applies at the next Generate.
            if (!bass) p.algorithm = cycleEnum(p.algorithm, delta);
            break;
        case 2: { int v = p.length + delta; if (v < 3) v = 3; if (v > 32) v = 32;
                  p.length = static_cast<uint8_t>(v);
                  if (mode_.live()) { mode_.editEngine().setParams(p); mode_.editEngine().applyLiveLength(); }
                } break;
        case 3: { int v = p.density + delta * 5; if (v < 0) v = 0; if (v > 100) v = 100;
                  p.density = static_cast<uint8_t>(v);
                  if (mode_.live()) { mode_.editEngine().setParams(p); mode_.editEngine().applyLiveDensity(); }
                } break;
        case 4:  // AlgoPrm: Scatter under Walk, GateLen under Phase; locked for
                 // Bass and under Degree. Applies at the next Generate.
            if (bass) break;
            if (p.algorithm == BerlinAlgorithm::DrunkardWalk) {
                int v = p.scatter + delta; if (v < 1) v = 1; if (v > 7) v = 7;
                p.scatter = static_cast<uint8_t>(v);
            } else if (p.algorithm == BerlinAlgorithm::GatePitchPhasing) {
                int v = p.gateLen + delta; if (v < 3) v = 3; if (v > 16) v = 16;
                p.gateLen = static_cast<uint8_t>(v);
            }
            break;
    }
}

void BerlinMode::StructureScreen::render(Display& d) const {
    const BerlinParams& p = mode_.voices_[mode_.editVoice_].params;
    const bool bass = mode_.editVoice() == kBass;
    char buf[12];
    drawBerlinParamCell(d, 0, "ALGO", bass ? "Bass" : algoName(p.algorithm), bass);
    snprintf(buf, sizeof buf, "%d", p.length);
    drawBerlinParamCell(d, 1, "LENGTH",  buf);
    snprintf(buf, sizeof buf, "%d%%", p.density);
    drawBerlinParamCell(d, 2, "DENSITY", buf);
    if (!bass && p.algorithm == BerlinAlgorithm::DrunkardWalk) {
        snprintf(buf, sizeof buf, "%d", p.scatter);
        drawBerlinParamCell(d, 3, "SCATTER", buf);
    } else if (!bass && p.algorithm == BerlinAlgorithm::GatePitchPhasing) {
        snprintf(buf, sizeof buf, "%d", p.gateLen);
        drawBerlinParamCell(d, 3, "GATELEN", buf);
    } else {
        drawBerlinParamCell(d, 3, "ALGOPRM", "-", true);   // dimmed: not used
    }
    drawBerlinParamDividers(d);
    mode_.renderRoll(d);                                   // Task 3
}
```

(Until Task 3 lands, keep the old `drawBerlinPianoRoll(...)` call in ALL the render bodies of this task instead of `mode_.renderRoll(d)` so the task compiles standalone — Task 3 swaps them.)

The new `DynamicsScreen` (global; note every case ends with `mode_.syncGlobals()` and a per-voice re-stamp):

```cpp
void BerlinMode::DynamicsScreen::onEncoder(int index, int delta) {
    BerlinParams& g = mode_.voices_[0].params;             // canonical globals
    switch (index) {
        case 1: { int v = g.velocityBase + delta;     if (v < 1) v = 1;   if (v > 126) v = 126;
                  g.velocityBase = static_cast<uint8_t>(v); } break;
        case 2: { int v = g.velocityHumanize + delta; if (v < 0) v = 0;   if (v > 30)  v = 30;
                  g.velocityHumanize = static_cast<uint8_t>(v); } break;
        case 3: { int v = g.accent + delta;           if (v < 0) v = 0;   if (v > 27)  v = 27;
                  g.accent = static_cast<uint8_t>(v); } break;
        case 4:  // Resolution is global (one step grid) and inherently live.
            g.resolution = cycleEnum(g.resolution, delta);
            break;
        default: return;
    }
    mode_.syncGlobals();
    // Velocity knobs re-stamp at once in every behavior; resolution re-stamps
    // the baked gateTicks so the roll widths match. Doing both is harmless.
    for (int v = 0; v < kVoices; ++v) {
        berlinStampVelocities(mode_.voices_[v].engine.sequenceMut(),
                              mode_.voices_[v].params);
        restampGateTicks(mode_.voices_[v].engine, mode_.voices_[v].params);
    }
}

void BerlinMode::DynamicsScreen::render(Display& d) const {
    const BerlinParams& g = mode_.voices_[0].params;
    char buf[12];
    snprintf(buf, sizeof buf, "%d", g.velocityBase);
    drawBerlinParamCell(d, 0, "VEL",   buf);
    snprintf(buf, sizeof buf, "+-%d", g.velocityHumanize);
    drawBerlinParamCell(d, 1, "HUMAN", buf);
    snprintf(buf, sizeof buf, "+%d", g.accent);
    drawBerlinParamCell(d, 2, "ACCENT", buf);
    drawBerlinParamCell(d, 3, "RESOL",
                        g.resolution == BerlinResolution::Sixteenth ? "16th" : "8th");
    drawBerlinParamDividers(d);
    mode_.renderRoll(d);
}
```

The new `BehaviorScreen`:

```cpp
void BerlinMode::BehaviorScreen::onEncoder(int index, int delta) {
    BerlinParams& g = mode_.voices_[0].params;
    switch (index) {
        case 1: g.behavior = cycleEnum(g.behavior, delta); break;
        case 2: { int v = g.morph + delta * 5; if (v < 0) v = 0; if (v > 100) v = 100;
                  g.morph = static_cast<uint8_t>(v); } break;
        case 3:
            if (g.behavior == BerlinBehavior::Evolve) {
                int v = g.evolveRate + delta; if (v < 1) v = 1; if (v > 8) v = 8;
                g.evolveRate = static_cast<uint8_t>(v);
            }
            break;
        default: return;
    }
    mode_.syncGlobals();
}

void BerlinMode::BehaviorScreen::render(Display& d) const {
    const BerlinParams& g = mode_.voices_[0].params;
    char buf[12];
    drawBerlinParamCell(d, 0, "BEHAVIOR", behaviorName(g.behavior));
    snprintf(buf, sizeof buf, "%d%%", g.morph);
    drawBerlinParamCell(d, 1, "MORPH",    buf);
    snprintf(buf, sizeof buf, "%d", g.evolveRate);
    drawBerlinParamCell(d, 2, "EVOLVE",   buf, g.behavior != BerlinBehavior::Evolve);
    drawBerlinParamCell(d, 3, "-", "-", true);             // unused
    drawBerlinParamDividers(d);
    mode_.renderRoll(d);
}
```

The screens reference `mode_.voices_` and `mode_.editVoice_` directly — they are nested classes of BerlinMode, so private access is fine. `restampGateTicks` and `algoName`/`behaviorName` already exist at file scope.

- [ ] **Step 4: Fix pre-existing tests that used the OLD knob map.** `grep -n "screen(0).onEncoder(3" test/test_berlin_mode/test_berlin_mode.cpp` — the old "Enc3 = Resolution" tests move to `screen(3).onEncoder(4, ...)`; the old "Enc4 = Density" structure tests move to Enc3. The old dynamics "Enc4 = Scatter" test moves to `screen(0).onEncoder(4, ...)` with the edit voice on High. The old behavior "Enc4 = GateLen" test moves to structure Enc4 under Phase.

- [ ] **Step 5: Run** — `pio test -e test -f test_berlin_mode 2>&1 | tail -3` → PASSED, then the full suite `pio test -e test 2>&1 | tail -3`.

- [ ] **Step 6: Commit**

```bash
git add core/modes/BerlinMode.h core/modes/BerlinMode.cpp test/test_berlin_mode/test_berlin_mode.cpp
git commit -m "feat(berlin): per-voice structure/character, global dynamics/behavior, AlgoPrm cell"
```

---

### Task 3: combined colored piano-roll

**Files:**
- Modify: `core/render/BerlinLayout.h` (voice colors/names + `BerlinRollVoice` + `drawBerlinMultiRoll`)
- Modify: `core/modes/BerlinMode.h` (+`renderRoll`, `fillRollVoices`)
- Modify: `core/modes/BerlinMode.cpp` (all five Berlin screens call `mode_.renderRoll(d)`)
- Test: `test/test_berlin_mode/test_berlin_mode.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
// The combined roll labels the edited voice; rendering any param screen
// draws it (StubDisplay sees the voice name drawn by the roll).
static void test_param_screens_draw_multi_roll_with_voice_label() {
    core::AppShell shell;
    core::BerlinMode berlin(shell);
    berlin.onEnter();
    StubDisplay d;
    berlin.screen(0).render(d);
    TEST_ASSERT_TRUE(d.drewText("HIGH"));                  // edit-voice label
    berlin.screen(0).onEncoderSw(1);                       // cycle to Bass
    StubDisplay d2;
    berlin.screen(1).render(d2);
    TEST_ASSERT_TRUE(d2.drewText("BASS"));
}
```

- [ ] **Step 2: Run to verify failure** — `pio test -e test -f test_berlin_mode 2>&1 | tail -5` → fails (no label drawn).

- [ ] **Step 3: Implement the renderer.** Append to `core/render/BerlinLayout.h`:

```cpp
// Voice identity for the multi-voice roll: index 0 Bass, 1 Mid, 2 High.
constexpr uint16_t kBerlinVoiceColors[3] = {
    rgb565(90, 140, 255),    // Bass — blue
    color::Green,            // Mid
    rgb565(255, 150, 40),    // High — orange
};
constexpr const char* kBerlinVoiceNames[3] = {"BASS", "MID", "HIGH"};

struct BerlinRollVoice {
    const BerlinSequence* seq = nullptr;
    int         playhead     = 0;
    int         soundingNote = -1;
    uint16_t    color        = 0;
    bool        muted        = false;
    bool        edited       = false;
    const char* name         = "";
};

// One roll over the union register of all voices. Each voice spans the full
// width at its own column width (length-normalized), so phasing shows as
// playheads drifting apart; a voice's playhead line is drawn only across its
// own pitch band. Brightness still encodes velocity; the edited voice is
// fully saturated, others dimmed, muted voices darkest. The edited voice's
// name is labelled top-right in its color.
inline void drawBerlinMultiRoll(Display& d, const BerlinRollVoice* vs, int n) {
    d.fillRect(0, kBerlinRollTop, 320, kBerlinRollH, color::Black);
    if (n < 1) return;

    // Union pitch range over all voices' active steps (C3..C5 fallback),
    // octave-snapped, at least 2 octaves — same rules as the single roll.
    int mn = 127, mx = 0;
    bool any = false;
    for (int v = 0; v < n; ++v) {
        const BerlinSequence& s = *vs[v].seq;
        for (int i = 0; i < s.length(); ++i) {
            if (!s.step(i).active) continue;
            if (s.step(i).note < mn) mn = s.step(i).note;
            if (s.step(i).note > mx) mx = s.step(i).note;
            any = true;
        }
    }
    if (!any) { mn = 48; mx = 72; }
    int lo = mn - (mn % 12);
    int hi = mx + (11 - (mx % 12));
    while (hi - lo < 23) { hi += 12; if (hi - lo < 23) lo -= 12; }
    if (lo < 0)   lo = 0;
    if (hi > 127) hi = 127;

    const int nSemis = hi - lo + 1;
    auto yTop = [&](int note) {
        return kBerlinRollTop + (hi - note) * kBerlinRollH / nSemis;
    };
    const int kbW   = kBerlinKbW;
    const int rollX = kbW;
    const int rollW = 320 - kbW;

    // Keyboard + lane lines (a key is marked used/sounding if ANY voice does).
    const uint16_t cWhite = color::LightGray;
    const uint16_t cBlack = rgb565(40, 40, 40);
    const uint16_t cDot   = rgb565(110, 110, 110);
    const uint16_t cLine  = rgb565(30, 30, 30);
    auto usedByAny = [&](int note) {
        for (int v = 0; v < n; ++v) {
            const BerlinSequence& s = *vs[v].seq;
            for (int i = 0; i < s.length(); ++i)
                if (s.step(i).active && s.step(i).note == note) return true;
        }
        return false;
    };
    auto soundingByAny = [&](int note) {
        for (int v = 0; v < n; ++v)
            if (vs[v].soundingNote == note) return true;
        return false;
    };
    for (int nt = lo; nt <= hi; ++nt) {
        const int y0 = yTop(nt);
        const int y1 = yTop(nt - 1);
        int h = y1 - y0;
        if (h < 1) h = 1;
        if (soundingByAny(nt)) {
            d.fillRect(0, y0, kbW, h, color::Gray);
        } else if (berlinIsBlackKey(nt)) {
            d.fillRect(0, y0, kbW, h, cWhite);
            d.fillRect(0, y0, kbW * 6 / 10, h, cBlack);
        } else {
            d.fillRect(0, y0, kbW, h, cWhite);
        }
        if (!soundingByAny(nt) && usedByAny(nt)) {
            int ds = h - 2; if (ds > 4) ds = 4; if (ds < 2) ds = 2;
            d.fillRect(kbW - ds - 3, y0 + (h - ds) / 2, ds, ds, cDot);
        }
        d.fillRect(rollX, y0, rollW, 1, cLine);
    }
    d.fillRect(kbW - 1, kBerlinRollTop, 1, kBerlinRollH, color::DarkGray);

    // Per-voice playhead lines (restricted to the voice's own pitch band) and
    // note blocks. Saturation: edited 255, others 140, muted 70 (of t).
    for (int v = 0; v < n; ++v) {
        const BerlinRollVoice& rv = vs[v];
        const BerlinSequence& s = *rv.seq;
        const int len  = s.length() < 1 ? 1 : s.length();
        const int colW = rollW / len;
        const int sat  = rv.muted ? 70 : (rv.edited ? 255 : 140);

        int bandLo = 127, bandHi = 0;
        for (int i = 0; i < len; ++i) {
            if (!s.step(i).active) continue;
            if (s.step(i).note < bandLo) bandLo = s.step(i).note;
            if (s.step(i).note > bandHi) bandHi = s.step(i).note;
        }
        if (bandLo > bandHi) { bandLo = lo; bandHi = hi; }
        const int bandY0 = yTop(bandHi);
        const int bandY1 = yTop(bandLo - 1);
        if (rv.playhead >= 0 && rv.playhead < len) {
            d.fillRect(rollX + rv.playhead * colW, bandY0, colW,
                       bandY1 - bandY0, scaleRgb565(rv.color, 60));
        }

        for (int i = 0; i < len; ++i) {
            const BerlinStep& st = s.step(i);
            if (!st.active) continue;
            if (st.note < lo || st.note > hi) continue;
            const int y0 = yTop(st.note);
            const int y1 = yTop(st.note - 1);
            int bh = y1 - y0 - 1; if (bh < 2) bh = 2;
            int w = colW * st.gateTicks / 12;
            if (w < 3) w = 3;
            if (w > colW - 1) w = colW - 1;
            const int vt = st.velocity;
            int t = 25 + (vt * vt * 230) / (126 * 126);    // quadratic velocity map
            t = t * sat / 255;
            d.fillRect(rollX + i * colW + 1, y0, w, bh, scaleRgb565(rv.color, t));
        }
    }

    // Edited voice label, top-right of the roll, in the voice's color.
    for (int v = 0; v < n; ++v) {
        if (!vs[v].edited) continue;
        int len = 0; while (vs[v].name[len] != '\0') ++len;
        d.drawText(320 - len * 6 - 4, kBerlinRollTop + 3, vs[v].name,
                   vs[v].color, color::Black, 1);
    }
}
```

In `BerlinMode.h` public section:

```cpp
    void renderRoll(Display& d) const;
```

In `BerlinMode.cpp`:

```cpp
void BerlinMode::renderRoll(Display& d) const {
    BerlinRollVoice rv[kVoices];
    for (int v = 0; v < kVoices; ++v) {
        const BerlinEngine& e = voices_[v].engine;
        rv[v].seq          = &e.sequence();
        rv[v].playhead     = e.playhead();
        rv[v].soundingNote = e.soundingNote();
        rv[v].color        = kBerlinVoiceColors[v];
        rv[v].muted        = e.muted();
        rv[v].edited       = (v == editVoice_);
        rv[v].name         = kBerlinVoiceNames[v];
    }
    drawBerlinMultiRoll(d, rv, kVoices);
}
```

Replace the trailing `drawBerlinPianoRoll(...)` call in ALL five Berlin screen `render()` bodies (structure, character, voices, dynamics, behavior) with `mode_.renderRoll(d);`. Remove the now-duplicate `kVoiceNames` table from Task A4 in BerlinMode.cpp and use `kBerlinVoiceNames` from the layout header instead (also in `VoicesScreen::render`).

- [ ] **Step 4: Run** — `pio test -e test -f test_berlin_mode 2>&1 | tail -3` → PASSED; then the full suite + both builds:

`pio test -e test 2>&1 | tail -3 && pio run -e native 2>&1 | tail -2 && pio run -e teensy41 2>&1 | tail -2`

- [ ] **Step 5: Sim visual check** — run `.pio/build/native/program`: three colored voices in one roll, Bass low blue, Mid green, High orange; playheads drift apart (Mid wraps early); pressing `2` on structure cycles the label/saturation; muting on `voices` darkens that voice.

- [ ] **Step 6: Commit**

```bash
git add core/render/BerlinLayout.h core/modes/BerlinMode.h core/modes/BerlinMode.cpp test/test_berlin_mode/test_berlin_mode.cpp
git commit -m "feat(berlin): combined colored piano-roll - per-voice playheads, edited-voice label"
```
