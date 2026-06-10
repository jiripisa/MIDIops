# Berlin mode — Plan C (Evolve + Live behaviors)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the **Behavior** parameter fully functional. Today only **Locked** works (the sequence loops identically; params apply on the next Generate). This plan adds **Evolve** (every N loops the engine auto-varies 1–2 steps in place) and **Live** (editing a structural parameter regenerates immediately).

**Architecture:** Builds on Plan A/B (merged as v0.7). Evolve lives entirely in `BerlinEngine` (it already has the generator, scale, rng, and params); it detects loop completion in `onClockTick` and splices 1–2 steps from a fresh candidate. Live lives in `BerlinMode`: structural parameter edits call a `liveRegen()` helper that regenerates immediately when Behavior == Live. No new files; everything in `core/`.

**Tech Stack:** C++17, PlatformIO (envs `teensy41`, `native`, `test`), Unity. `pio test -e test`, single suite `pio test -e test -f <dir>`. Build-only: `pio run -e native`, `pio run -e teensy41`.

Spec: `docs/specs/2026-06-10-berlin-mode-design.md` §6 (Behavior). Relevant code: `core/BerlinEngine.{h,cpp}`, `core/modes/BerlinMode.{h,cpp}`, `core/BerlinTypes.h` (`BerlinBehavior { Locked, Evolve, Live, kCount }`, `evolveRate`).

Conventions: 2-space indent, `core::` namespace, English. `-Wmisleading-indentation` taken seriously (one statement per line).

---

## Design notes (read before implementing)

- **Evolve** triggers at a loop boundary (the playhead wrapping to 0). On every `evolveRate`-th completed loop, the engine generates a fresh candidate with the current params and copies **1–2 random steps** from it into the live sequence ("change one or two notes every 3rd–4th repeat"). This keeps the groove recognizable while drifting. Reset/Generate (Latch3) still does the full Morph regenerate.
- **Live** regenerates immediately when a **structural** parameter changes — one that alters the generated pattern: **Algorithm, Length, Resolution, Density, Tension, Octave base, Octave range, Scatter, GateLen**. **Performance** params (Gate, Velocity, Humanize, Accent) and **meta** params (Behavior, Morph, EvolveRate) do NOT auto-regenerate in Live — they take effect on the next manual Generate (a known v1 limitation: velocity/gate are baked into the sequence at generation time, so changing them live without re-rolling the notes would require an engine rework that is out of scope here).
- Locked behaves exactly as today (no auto-variation, no auto-regen).

---

## File structure (Plan C)

- Modify `core/BerlinEngine.{h,cpp}` — loop counter + `evolve()` + boundary detection in `onClockTick`.
- Modify `core/modes/BerlinMode.{h,cpp}` — `liveRegen()` + structural-edit hooks in the screens' `onEncoder`.
- Tests: `test/test_berlin_engine/` (Evolve), `test/test_berlin_mode/` (Live).

---

### Task 1: Evolve behavior in BerlinEngine

**Files:** Modify `core/BerlinEngine.{h,cpp}`; Test `test/test_berlin_engine/test_berlin_engine.cpp` (extend).

- [ ] **Step 1: Add state + a method to `core/BerlinEngine.h`.** In the private section add:
```cpp
    void evolve();           // splice 1-2 steps from a fresh candidate (Evolve behavior)
    int  loopCount_ = 0;     // completed loops since the last generate()/stop()
```
Add a public test inspector next to `playhead()`:
```cpp
    int loopCount() const { return loopCount_; }
```

- [ ] **Step 2: Add the failing test** to `test/test_berlin_engine/test_berlin_engine.cpp` (it already includes `core/BerlinEngine.h`, `core/DrunkardWalkGenerator.h`, `core/Scale.h`, `support/FakeMidiOutput.h`; register in `main()`):
```cpp
static int seqNoteDiff(const core::BerlinSequence& a, const core::BerlinSequence& b) {
    int d = 0; int n = a.length() < b.length() ? a.length() : b.length();
    for (int i = 0; i < n; ++i)
        if (a.step(i).active != b.step(i).active || a.step(i).note != b.step(i).note) ++d;
    return d;
}

static void clocksB(core::BerlinEngine& e, int n) { for (int i = 0; i < n; ++i) e.onClockTick(); }

static void test_evolve_varies_a_few_steps_each_n_loops() {
    core::BerlinEngine e; core::DrunkardWalkGenerator gen; core::Scale sc(core::Scale::Type::Minor, 0);
    e.setGenerator(&gen); e.setScale(&sc); e.seed(31);
    core::BerlinParams p;
    p.length = 4; p.density = 100; p.resolution = core::BerlinResolution::Sixteenth; // 6 ticks/step
    p.behavior = core::BerlinBehavior::Evolve; p.evolveRate = 1;
    e.setParams(p);
    e.generate();
    core::BerlinSequence base = e.sequence();

    e.play();
    const int ticksPerLoop = 4 * 6;             // length * stepTicks(16th)
    clocksB(e, ticksPerLoop);                    // complete exactly one loop → evolve fires
    TEST_ASSERT_EQUAL_INT(1, e.loopCount());
    const int diff = seqNoteDiff(base, e.sequence());
    TEST_ASSERT_TRUE(diff >= 1 && diff <= 2);    // 1-2 steps changed, not the whole pattern
}

static void test_locked_never_auto_varies() {
    core::BerlinEngine e; core::DrunkardWalkGenerator gen; core::Scale sc(core::Scale::Type::Minor, 0);
    e.setGenerator(&gen); e.setScale(&sc); e.seed(31);
    core::BerlinParams p;
    p.length = 4; p.density = 100; p.resolution = core::BerlinResolution::Sixteenth;
    p.behavior = core::BerlinBehavior::Locked;
    e.setParams(p);
    e.generate();
    core::BerlinSequence base = e.sequence();
    e.play();
    clocksB(e, 4 * 6 * 5);                        // five loops
    TEST_ASSERT_EQUAL_INT(0, seqNoteDiff(base, e.sequence()));   // identical — Locked never drifts
}
```

- [ ] **Step 3: Run to verify FAIL** — `pio test -e test -f test_berlin_engine`.

- [ ] **Step 4: Implement in `core/BerlinEngine.cpp`.**
  - Replace the boundary block in `onClockTick()`:
```cpp
    if (stepTicks_ >= stepLenTicks()) {
        const int len = seq_.length() < 1 ? 1 : seq_.length();
        int next = playhead_ + 1;
        if (next >= len) {
            next = 0;
            ++loopCount_;
            if (params_.behavior == BerlinBehavior::Evolve && generator_ && scale_
                && params_.evolveRate > 0 && (loopCount_ % params_.evolveRate) == 0) {
                evolve();
            }
        }
        playhead_ = next;
        emitStep(playhead_);
    }
```
  - Add `evolve()` (place near `generate()`):
```cpp
void BerlinEngine::evolve() {
    BerlinSequence cand;
    generator_->generate(cand, params_, *scale_, rng_);
    const int len = seq_.length();
    const int changes = 1 + rng_.range(0, 1);          // 1 or 2 steps
    for (int c = 0; c < changes; ++c) {
        const int idx = rng_.range(0, len - 1);
        if (idx >= 0 && idx < cand.length()) seq_.step(idx) = cand.step(idx);
    }
}
```
  - Reset `loopCount_` in BOTH `generate()` and `stop()` — add `loopCount_ = 0;` next to the existing `playhead_ = 0;` lines in each.

- [ ] **Step 5: Run** `pio test -e test -f test_berlin_engine` → PASS. Then `pio test -e test` → PASS.
- [ ] **Step 6: Commit** `git add core/BerlinEngine.h core/BerlinEngine.cpp test/test_berlin_engine && git commit -m "feat(berlin): Evolve behavior — auto-vary 1-2 steps every N loops"`

---

### Task 2: Live behavior in BerlinMode

**Files:** Modify `core/modes/BerlinMode.{h,cpp}`; Test `test/test_berlin_mode/test_berlin_mode.cpp` (extend).

When Behavior == Live, editing a structural parameter regenerates immediately. The screens' `onEncoder` already update `params_`; a structural case then calls `mode_.liveRegen()`, which regenerates with the just-updated params.

- [ ] **Step 1: Add `liveRegen()` to `core/modes/BerlinMode.h`** — a public method (screens call it through their `mode_` reference):
```cpp
    void liveRegen();   // regenerate immediately if Behavior == Live (structural edit)
```

- [ ] **Step 2: Implement in `core/modes/BerlinMode.cpp`:**
```cpp
void BerlinMode::liveRegen() {
    if (params_.behavior != BerlinBehavior::Live) return;
    engine_.setScale(&scale_);
    engine_.setParams(params_);
    applyGenerator();
    engine_.generate();
}
```
(`scale_` is the mode's local copy, refreshed each `update()`/`onEnter()`; `applyGenerator()` selects the generator for the current `params_.algorithm`.)

- [ ] **Step 3: Call `liveRegen()` from the STRUCTURAL encoder cases** in `core/modes/BerlinMode.cpp`. READ the four screens' `onEncoder` and add `mode_.liveRegen();` at the end of these cases ONLY:
  - **StructureScreen** — all four (Enc1 Algorithm, Enc2 Length, Enc3 Resolution, Enc4 Density): simplest to call `mode_.liveRegen();` once after the `switch`.
  - **CharacterScreen** — Enc2 Tension, Enc3 OctaveBase, Enc4 OctaveRange (NOT Enc1 Gate): call `mode_.liveRegen();` inside those three cases.
  - **DynamicsScreen** — Enc4 contextual (Scatter/GateLen) ONLY (NOT Enc1 Velocity / Enc2 Humanize / Enc3 Accent): call `mode_.liveRegen();` inside the Enc4 case.
  - **BehaviorScreen** — none (Behavior/Morph/EvolveRate never auto-regen).
  Use a clean form, e.g. a local `bool structural = false;` set in the structural cases, then `if (structural) mode_.liveRegen();` after the switch — or call `mode_.liveRegen();` directly in each structural case. Keep `-Wmisleading-indentation` clean.

- [ ] **Step 4: Add tests** to `test/test_berlin_mode/test_berlin_mode.cpp`:
```cpp
static void test_live_regenerates_on_structural_edit() {
    core::AppShell shell;
    core::BerlinMode berlin(shell);
    FakeMidiOutput out; berlin.setMidiOutput(&out);
    berlin.onEnter();
    berlin.params().behavior = core::BerlinBehavior::Live;

    // Structural edit (Length on Structure screen, Enc2) regenerates → length follows.
    berlin.params().length = 16;
    core::Screen& structure = berlin.screen(0);
    structure.onEncoder(2, -1);                       // length 16 → 15, Live regen
    TEST_ASSERT_EQUAL_INT(15, berlin.engine().sequence().length());
    structure.onEncoder(2, -1);                       // 15 → 14
    TEST_ASSERT_EQUAL_INT(14, berlin.engine().sequence().length());
}

static void test_live_ignores_performance_edit_and_locked_never_regens() {
    core::AppShell shell;
    core::BerlinMode berlin(shell);
    FakeMidiOutput out; berlin.setMidiOutput(&out);
    berlin.onEnter();

    // Live + a PERFORMANCE edit (Velocity, Dynamics Enc1) must NOT regenerate.
    berlin.params().behavior = core::BerlinBehavior::Live;
    core::BerlinSequence before = berlin.engine().sequence();
    berlin.screen(2).onEncoder(1, +1);                // velocity base +1 (performance)
    // sequence unchanged (no regen): compare lengths + step 0 note.
    TEST_ASSERT_EQUAL_INT(before.length(), berlin.engine().sequence().length());
    TEST_ASSERT_EQUAL_UINT8(before.step(0).note, berlin.engine().sequence().step(0).note);

    // Locked + a structural edit must NOT regenerate either.
    berlin.params().behavior = core::BerlinBehavior::Locked;
    core::BerlinSequence base2 = berlin.engine().sequence();
    berlin.screen(0).onEncoder(4, +1);                // density +5 (structural), but Locked
    TEST_ASSERT_EQUAL_INT(base2.length(), berlin.engine().sequence().length());
    TEST_ASSERT_EQUAL_UINT8(base2.step(0).note, berlin.engine().sequence().step(0).note);
}
```
Register both in `main()`. (Note: step 0 is always the root anchor, so its note is stable across a regen with the same scale/octave — the performance/Locked tests rely on the FULL sequence being identical; if a regen DID fire, later steps would change. To make the assertion robust, also compare a mid-sequence step: add `TEST_ASSERT_EQUAL_UINT8(before.step(2).note, berlin.engine().sequence().step(2).note);` etc. Prefer comparing several steps or a small `seqNoteDiff`-style helper.)

- [ ] **Step 5: Run** `pio test -e test -f test_berlin_mode` → PASS. Then `pio test -e test` → PASS. Build `pio run -e native` SUCCESS.
- [ ] **Step 6: Commit** `git add core/modes/BerlinMode.h core/modes/BerlinMode.cpp test/test_berlin_mode && git commit -m "feat(berlin): Live behavior — regenerate immediately on structural param edit"`

---

### Task 3: Build both platforms + verify

**Files:** none expected.

- [ ] **Step 1: Build** `pio run -e teensy41` SUCCESS, `pio run -e native` SUCCESS, `pio test -e test` PASS. WARNING-CLEAN.
- [ ] **Step 2 (controller/human): simulator verification.** `make sim` → Berlin → Behavior screen (4th):
  - **Locked:** sequence loops identically; params apply only on Generate (Latch3). (Unchanged.)
  - **Evolve** (set EvolveRate, e.g. 2): while playing, the pattern drifts — 1–2 steps change every N loops — while staying recognizable; the piano-roll shows the occasional changed step. Generate still rolls a whole new pattern.
  - **Live:** turning a structural knob (Algorithm / Length / Resolution / Density on Structure; Tension / Octave on Character; the contextual Scatter/GateLen on Dynamics) regenerates immediately — you hear the new pattern as you turn. Turning a performance knob (Gate / Velocity / Humanize / Accent) does NOT re-roll the notes (applies on the next Generate).

---

## Self-review notes

- **Spec coverage (§6):** Evolve (auto-vary every N loops) → Task 1; Live (regenerate on structural edit) → Task 2; Locked unchanged (guarded by `test_locked_never_auto_varies` + the Locked branch of the Live test).
- **Evolve cadence:** `loopCount_ % evolveRate == 0` fires on loops N, 2N, 3N…; `evolveRate > 0` guards against divide-by-zero (the param clamps to 1..8 in the editor, but the guard is defensive). `loopCount_` resets on generate()/stop() so a fresh pattern starts its drift clock over.
- **Live structural vs performance split:** structural = Algorithm/Length/Resolution/Density/Tension/OctaveBase/OctaveRange/Scatter/GateLen (call `liveRegen()`); performance = Gate/Velocity/Humanize/Accent and meta = Behavior/Morph/EvolveRate (no call). Documented limitation: performance params are baked at generation, so they apply on the next Generate, not live.
- **Determinism:** Evolve uses the engine's seeded `rng_`; the test seeds it and asserts a bounded 1–2-step change. Live regen is deterministic given the rng state.
- **Type consistency:** `BerlinBehavior` (BerlinTypes.h) read by the engine (Task 1) and mode (Task 2); `evolveRate` consumed by the engine; `liveRegen()` (Task 2) called by the structural screen cases; `loopCount()` inspector used by the Evolve test.
