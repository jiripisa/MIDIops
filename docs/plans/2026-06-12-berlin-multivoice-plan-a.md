# Multi-voice Berlin — Plan A: multi-voice core

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** BerlinMode runs three voices (Bass/Mid/High) — three `BerlinEngine` instances with per-role params, per-voice MIDI channels and mute — plus a new root-heavy `BassAnchorGenerator` and a `voices` mixer screen.

**Architecture:** The engine is untouched except for a mute flag; the mode grows a `Voice` array (engine + params + channel) and fans clock/transport out to all of them. Phasing comes from per-voice lengths (Bass 16 / Mid 15 / High 16). Screens keep operating on a single "edit voice" (default High); per-voice editing UI and the combined roll come in Plan B.

**Tech Stack:** C++17 core (no platform headers), PlatformIO, Unity tests (`pio test -e test`).

**Spec:** `docs/specs/2026-06-12-berlin-multivoice-design.md` + the Berlin School spec §2.3/§2.4/§9.

**Voice defaults (used throughout):**

| Voice | index | octaveBase | octaveRange | length | density | gate | channel | generator |
|---|---|---|---|---|---|---|---|---|
| Bass | 0 (`kBass`) | 24 (C1) | 1 | 16 | 30 | 50 | 1 | BassAnchor (always) |
| Mid  | 1 (`kMid`)  | 48 (C3) | 1 | 15 | 50 | 55 | 2 | by `params.algorithm` |
| High | 2 (`kHigh`) | 60 (C4) | 1 | 16 | 50 | 55 | 3 | by `params.algorithm` |

Mid 15 × High/Bass 16 = the classic 16×15 phasing pair. `editVoice_` defaults to `kHigh` (the melodic voice — closest to today's single voice, minimizes test churn).

---

### Task 1: BerlinEngine mute

**Files:**
- Modify: `core/BerlinEngine.h` (public API + member)
- Modify: `core/BerlinEngine.cpp:10-14` (`emit`)
- Test: `test/test_berlin_engine/test_berlin_engine.cpp`

- [ ] **Step 1: Write the failing test** (append to test_berlin_engine.cpp; register in `main()` next to the other `RUN_TEST`s)

```cpp
// Mute suppresses NoteOns but the engine keeps running (playhead advances),
// the sounding note is silenced on mute, and unmuting re-enters in phase.
static void test_engine_mute_keeps_running() {
    core::Scale scale(core::Scale::Type::Minor, 0);
    core::DrunkardWalkGenerator gen;
    FakeMidiOutput out;
    core::BerlinEngine e;
    core::BerlinParams p;
    p.density = 100;                       // every step active: deterministic NoteOns
    e.setOutput(&out);
    e.setScale(&scale);
    e.setGenerator(&gen);
    e.setParams(p);
    e.seed(42);
    e.generate();
    e.play();
    TEST_ASSERT_FALSE(e.muted());

    e.setMuted(true);                      // silences the sounding note at once
    TEST_ASSERT_FALSE(out.events.empty());
    TEST_ASSERT_FALSE(out.events.back().isOn);

    const size_t evCount = out.events.size();
    const int phBefore = e.playhead();
    for (int i = 0; i < 12 * 4; ++i) e.onClockTick();
    TEST_ASSERT_TRUE(e.playhead() != phBefore);            // still running
    for (size_t i = evCount; i < out.events.size(); ++i)
        TEST_ASSERT_FALSE(out.events[i].isOn);             // no NoteOn while muted

    e.setMuted(false);
    const size_t evCount2 = out.events.size();
    for (int i = 0; i < 12; ++i) e.onClockTick();          // next step sounds again
    bool gotOn = false;
    for (size_t i = evCount2; i < out.events.size(); ++i)
        if (out.events[i].isOn) gotOn = true;
    TEST_ASSERT_TRUE(gotOn);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `pio test -e test -f test_berlin_engine 2>&1 | tail -5`
Expected: compile error — `no member named 'setMuted'`.

- [ ] **Step 3: Implement.** In `core/BerlinEngine.h`, after `void setGenerator(...)`:

```cpp
    // Mute suppresses NoteOns only; the engine keeps ticking so unmuting
    // re-enters in phase ("build up, then take away"). NoteOffs always pass,
    // and muting silences the currently sounding note immediately.
    void setMuted(bool m) {
        if (m && !muted_) silence();
        muted_ = m;
    }
    bool muted() const { return muted_; }
```

and add the member next to `playing_`:

```cpp
    bool  muted_     = false;
```

In `core/BerlinEngine.cpp`, guard `emit`:

```cpp
void BerlinEngine::emit(bool isOn, uint8_t note, uint8_t velocity) {
    if (!out_) return;
    if (isOn && muted_) return;            // mute: NoteOns suppressed, offs pass
    if (isOn) out_->sendNoteOn(outChannel_, note, velocity);
    else      out_->sendNoteOff(outChannel_, note);
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `pio test -e test -f test_berlin_engine 2>&1 | tail -3`
Expected: PASSED.

- [ ] **Step 5: Commit**

```bash
git add core/BerlinEngine.h core/BerlinEngine.cpp test/test_berlin_engine/test_berlin_engine.cpp
git commit -m "feat(berlin): engine mute - NoteOns suppressed, playhead keeps running"
```

---

### Task 2: BassAnchorGenerator

**Files:**
- Create: `core/BassAnchorGenerator.h`
- Create: `core/BassAnchorGenerator.cpp`
- Test: `test/test_berlin_generator/test_berlin_generator.cpp`

- [ ] **Step 1: Write the failing test** (append + register in `main()`; the file already includes `core/BerlinGen.h`, `core/Scale.h` etc. — add `#include "core/BassAnchorGenerator.h"`)

```cpp
// Bass anchor (spec §2.4c/§9): root skeleton on steps 0 and length/2, all
// notes in scale and register, and at least half of the active notes sit on
// the root pitch class ("root-heavy heartbeat").
static void test_bass_anchor_generator_is_root_heavy() {
    core::Scale scale(core::Scale::Type::Minor, 0);
    core::BerlinParams p;
    p.length = 16; p.density = 40; p.octaveBase = 24; p.octaveRange = 1;
    core::BerlinRng rng; rng.seed(42);
    core::BassAnchorGenerator gen;
    core::BerlinSequence seq;
    gen.generate(seq, p, scale, rng);

    int lo = 0, hi = 0;
    core::berlinRegister(p, lo, hi);
    const uint8_t root = core::berlinBaseRoot(scale, p);

    TEST_ASSERT_EQUAL_INT(16, seq.length());
    TEST_ASSERT_TRUE(seq.step(0).active);                  // beat 1 anchor
    TEST_ASSERT_EQUAL_INT(root, seq.step(0).note);
    TEST_ASSERT_TRUE(seq.step(0).accent);
    TEST_ASSERT_TRUE(seq.step(8).active);                  // beat 3 anchor
    TEST_ASSERT_EQUAL_INT(root, seq.step(8).note);

    int active = 0, onRootPc = 0;
    for (int i = 0; i < seq.length(); ++i) {
        if (!seq.step(i).active) continue;
        ++active;
        TEST_ASSERT_TRUE(seq.step(i).note >= lo && seq.step(i).note <= hi);
        TEST_ASSERT_TRUE(scale.contains(seq.step(i).note));
        if (seq.step(i).note % 12 == scale.root()) ++onRootPc;
    }
    TEST_ASSERT_TRUE(onRootPc * 2 >= active);              // root-heavy
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `pio test -e test -f test_berlin_generator 2>&1 | tail -5`
Expected: compile error — `BassAnchorGenerator.h` not found.

- [ ] **Step 3: Implement.** Create `core/BassAnchorGenerator.h`:

```cpp
#pragma once

#include "core/SequenceGenerator.h"

namespace core {

// Root-heavy bass anchor per the Berlin spec (§2.4c, §9): the root is the
// skeleton on beats 1 and 3 (steps 0 and length/2), other active steps lean
// on root/fifth/octave with a rare excursion to degree 4 or 6 — the
// "heartbeat". The Bass voice always uses it; the Algorithm knob never
// applies to Bass.
class BassAnchorGenerator : public SequenceGenerator {
public:
    void generate(BerlinSequence& out, const BerlinParams& p,
                  const Scale& scale, BerlinRng& rng) override;
};

} // namespace core
```

Create `core/BassAnchorGenerator.cpp`:

```cpp
#include "core/BassAnchorGenerator.h"

#include "core/BerlinGen.h"
#include "core/Scale.h"

namespace core {

void BassAnchorGenerator::generate(BerlinSequence& out, const BerlinParams& p,
                                   const Scale& scale, BerlinRng& rng) {
    const int length = p.length < 1 ? 1 : (p.length > BerlinSequence::kMaxSteps
                                               ? BerlinSequence::kMaxSteps : p.length);
    out.clear();
    out.setLength(length);

    int lo = 0, hi = 0;
    berlinRegister(p, lo, hi);
    const int gate = berlinGateTicks(p);
    const uint8_t baseRoot = berlinBaseRoot(scale, p);
    const int half = length / 2;          // "beat 3" of a 16-step bar

    for (int i = 0; i < length; ++i) {
        const bool anchor = (i == 0) || (length >= 8 && i == half);
        const bool active = anchor || rng.chance(p.density);
        if (!active) { out.step(i) = BerlinStep{}; continue; }

        int note = baseRoot;
        if (!anchor) {
            // Pitch palette per spec §9: mostly root, then fifth/octave,
            // rarely degree 4 or 6. Quantized to scale, folded into register.
            const int r = rng.range(0, 99);
            if      (r < 55) note = baseRoot;                              // root
            else if (r < 75) note = baseRoot + 7;                          // fifth
            else if (r < 90) note = baseRoot + 12;                         // octave
            else             note = baseRoot + (rng.chance(50) ? 5 : 9);   // deg 4/6
            if (note > 127) note = 127;
            note = scale.quantize(static_cast<uint8_t>(note));
            note = berlinFoldIntoRegister(note, lo, hi);
        }

        BerlinStep s;
        s.active    = true;
        s.note      = static_cast<uint8_t>(note);
        s.accent    = anchor;
        s.gateTicks = static_cast<uint16_t>(gate);
        s.velJitter = berlinDrawJitter(rng);
        s.velocity  = berlinVelocityFor(p, s.accent, s.velJitter);
        out.step(i) = s;
    }
}

} // namespace core
```

- [ ] **Step 4: Run test to verify it passes**

Run: `pio test -e test -f test_berlin_generator 2>&1 | tail -3`
Expected: PASSED. If `onRootPc * 2 >= active` fails for seed 42, the weights are wrong — do NOT loosen the assertion; check the `r < 55` branch runs for the majority.

- [ ] **Step 5: Commit**

```bash
git add core/BassAnchorGenerator.h core/BassAnchorGenerator.cpp test/test_berlin_generator/test_berlin_generator.cpp
git commit -m "feat(berlin): BassAnchorGenerator - root-heavy heartbeat per spec"
```

---

### Task 3: BerlinMode voice array

The core refactor. `engine_` + `params_` become `voices_[3]`; everything fans out. Existing screens keep compiling by switching to `editParams()`/`editEngine()`.

**Files:**
- Modify: `core/modes/BerlinMode.h` (members + accessors)
- Modify: `core/modes/BerlinMode.cpp` (constructor, applyGenerator, onEnter, update, onTransport, onRawInput, PresetOps, screens' member refs)
- Test: `test/test_berlin_mode/test_berlin_mode.cpp`

- [ ] **Step 1: Write the failing tests** (append + register)

```cpp
// Three voices tick together and phase: Mid is 15 steps against 16, so after
// 16 steps Bass/High wrap to 0 while Mid has drifted to 1.
static void test_three_voices_tick_and_phase() {
    core::AppShell shell;
    core::BerlinMode berlin(shell);
    FakeMidiOutput out; berlin.setMidiOutput(&out);
    berlin.onEnter();
    berlin.onRawInput({core::RawInput::Kind::Latch, 1, 0, false});  // prime
    berlin.onRawInput({core::RawInput::Kind::Latch, 1, 0, true});   // play all
    TEST_ASSERT_TRUE(berlin.engine(core::BerlinMode::kBass).isPlaying());
    TEST_ASSERT_TRUE(berlin.engine(core::BerlinMode::kMid).isPlaying());
    TEST_ASSERT_TRUE(berlin.engine(core::BerlinMode::kHigh).isPlaying());
    for (int i = 0; i < 12 * 16; ++i) berlin.onClockTick();         // 16 steps
    TEST_ASSERT_EQUAL_INT(0, berlin.engine(core::BerlinMode::kBass).playhead());
    TEST_ASSERT_EQUAL_INT(1, berlin.engine(core::BerlinMode::kMid).playhead());
    TEST_ASSERT_EQUAL_INT(0, berlin.engine(core::BerlinMode::kHigh).playhead());
}

// Each voice emits on its own channel (defaults 1/2/3): play() emits every
// voice's step 0 (Bass anchor + the melodic step-0 root are always active).
static void test_voices_emit_on_their_channels() {
    core::AppShell shell;
    core::BerlinMode berlin(shell);
    FakeMidiOutput out; berlin.setMidiOutput(&out);
    berlin.onEnter();
    berlin.onRawInput({core::RawInput::Kind::Latch, 1, 0, false});
    berlin.onRawInput({core::RawInput::Kind::Latch, 1, 0, true});
    bool ch1 = false, ch2 = false, ch3 = false, other = false;
    for (const auto& ev : out.events) {
        if (!ev.isOn) continue;
        if      (ev.channel == 1) ch1 = true;
        else if (ev.channel == 2) ch2 = true;
        else if (ev.channel == 3) ch3 = true;
        else                      other = true;
    }
    TEST_ASSERT_TRUE(ch1);
    TEST_ASSERT_TRUE(ch2);
    TEST_ASSERT_TRUE(ch3);
    TEST_ASSERT_FALSE(other);
}

// The edit-voice accessors target one voice; setEditVoice switches them.
static void test_edit_voice_targets_one_voice() {
    core::AppShell shell;
    core::BerlinMode berlin(shell);
    TEST_ASSERT_EQUAL_INT(core::BerlinMode::kHigh, berlin.editVoice());
    berlin.params().density = 77;                       // edits High only
    TEST_ASSERT_EQUAL_INT(77, berlin.params(core::BerlinMode::kHigh).density);
    TEST_ASSERT_EQUAL_INT(30, berlin.params(core::BerlinMode::kBass).density);
    berlin.setEditVoice(core::BerlinMode::kBass);
    TEST_ASSERT_EQUAL_INT(30, berlin.params().density);
}
```

- [ ] **Step 2: Run to verify failure**

Run: `pio test -e test -f test_berlin_mode 2>&1 | tail -5`
Expected: compile errors — `kBass`, `engine(int)` etc. undeclared.

- [ ] **Step 3: Rewrite `core/modes/BerlinMode.h`.** Replace the private state block and the public inspectors (the screens and PresetOps declarations stay; `VoicesScreen` comes in Task 4):

```cpp
// in the includes block, add:
#include "core/BassAnchorGenerator.h"
```

```cpp
// PUBLIC section — replace the old inspectors and add voice API:
    static constexpr int kVoices = 3;
    enum VoiceId { kBass = 0, kMid = 1, kHigh = 2 };

    struct Voice {
        BerlinEngine engine;
        BerlinParams params;
        uint8_t      channel = 1;
    };

    // Live sculpting: when Behavior == Live, apply the edited parameter to the
    // existing sequence in place (no regeneration, playhead untouched).
    bool live() const { return voices_[editVoice_].params.behavior == BerlinBehavior::Live; }

    // Edit-voice accessors (no-arg = the voice the screens currently edit).
    int  editVoice() const { return editVoice_; }
    void setEditVoice(int v) { if (v >= 0 && v < kVoices) editVoice_ = v; }
    BerlinParams&         params()        { return voices_[editVoice_].params; }
    BerlinParams&         params(int v)   { return voices_[v].params; }
    const BerlinEngine&   engine() const  { return voices_[editVoice_].engine; }
    const BerlinEngine&   engine(int v) const { return voices_[v].engine; }
    uint8_t               voiceChannel(int v) const { return voices_[v].channel; }
```

```cpp
// PRIVATE section — replace `BerlinEngine engine_; BerlinParams params_{};` with:
    Voice                 voices_[kVoices];
    int                   editVoice_ = kHigh;
    BassAnchorGenerator   bassGen_;

    // Convenience for the screens: the voice being edited.
    BerlinParams& editParams() { return voices_[editVoice_].params; }
    BerlinEngine& editEngine() { return voices_[editVoice_].engine; }

    void applyGenerator(int v);   // point voice v's engine at its generator
    void applyGeneratorAll() { for (int v = 0; v < kVoices; ++v) applyGenerator(v); }
```

`setMidiOutput` becomes:

```cpp
    void setMidiOutput(MidiOutput* o) {
        for (int v = 0; v < kVoices; ++v) voices_[v].engine.setOutput(o);
    }
```

`onClockTick` and `onExit` become:

```cpp
    void onClockTick() override {
        for (int v = 0; v < kVoices; ++v) voices_[v].engine.onClockTick();
    }
    void onExit() override {              // silence every voice on leave
        for (int v = 0; v < kVoices; ++v) voices_[v].engine.stop();
    }
```

- [ ] **Step 4: Rewrite the mode-level functions in `core/modes/BerlinMode.cpp`.** Add `#include "core/BassAnchorGenerator.h"` is NOT needed (header pulls it). Constructor + applyGenerator:

```cpp
BerlinMode::BerlinMode(AppServices& svc) : svc_(svc) {
    // Role defaults per spec §2.3 (see the table in the plan header).
    voices_[kBass].params.octaveBase  = 24;   // C1
    voices_[kBass].params.octaveRange = 1;
    voices_[kBass].params.density     = 30;
    voices_[kBass].params.gatePercent = 50;
    voices_[kBass].params.length      = 16;
    voices_[kBass].channel            = 1;

    voices_[kMid].params.octaveBase   = 48;   // C3
    voices_[kMid].params.octaveRange  = 1;
    voices_[kMid].params.length       = 15;   // phases 15x16 against High/Bass
    voices_[kMid].channel             = 2;

    voices_[kHigh].params.octaveBase  = 60;   // C4
    voices_[kHigh].params.octaveRange = 1;
    voices_[kHigh].params.length      = 16;
    voices_[kHigh].channel            = 3;

    for (int v = 0; v < kVoices; ++v) {
        applyGenerator(v);
        voices_[v].engine.setParams(voices_[v].params);
        voices_[v].engine.setOutChannel(voices_[v].channel);
    }
}

void BerlinMode::applyGenerator(int v) {
    if (v == kBass) { voices_[v].engine.setGenerator(&bassGen_); return; }
    switch (voices_[v].params.algorithm) {
        case BerlinAlgorithm::DegreeWeighted:   voices_[v].engine.setGenerator(&degreeGen_);  break;
        case BerlinAlgorithm::GatePitchPhasing: voices_[v].engine.setGenerator(&phasingGen_); break;
        case BerlinAlgorithm::DrunkardWalk:
        default:                                voices_[v].engine.setGenerator(&walkGen_);    break;
    }
}
```

`onEnter` / `update`:

```cpp
void BerlinMode::onEnter() {
    scale_ = svc_.scale();
    for (int v = 0; v < kVoices; ++v) {
        Voice& vc = voices_[v];
        vc.engine.setScale(&scale_);
        vc.engine.setParams(vc.params);
        vc.engine.setOutChannel(vc.channel);
        applyGenerator(v);
        if (!vc.engine.sequence().step(0).active) vc.engine.generate();
    }
    for (int i = 0; i < 4; ++i) latchSynced_[i] = false;  // re-absorb first delivery per index
}

void BerlinMode::update(uint32_t /*nowMs*/) {
    scale_ = svc_.scale();
    for (int v = 0; v < kVoices; ++v) {
        Voice& vc = voices_[v];
        vc.engine.setScale(&scale_);
        vc.engine.setParams(vc.params);
        vc.engine.setOutChannel(vc.channel);
        applyGenerator(v);
    }
}
```

`onTransport` and the latch cases in `onRawInput` loop all voices (the click/absorb logic above the switch is unchanged):

```cpp
void BerlinMode::onTransport(Transport t) {
    for (int v = 0; v < kVoices; ++v) {
        BerlinEngine& e = voices_[v].engine;
        switch (t) {
            case Transport::Play:  e.play();  break;
            case Transport::Pause: e.silence(); e.pause(); break;
            case Transport::Reset:
            case Transport::Stop:  e.stop();  break;
        }
    }
}
```

```cpp
    // onRawInput switch — the three cases become:
    switch (in.index) {
        case 1:
            if (flip) {
                const bool playing = voices_[0].engine.isPlaying();  // all in sync
                for (int v = 0; v < kVoices; ++v) {
                    if (playing) voices_[v].engine.pause();
                    else         voices_[v].engine.play();
                }
                svc_.notifyLocalTransport(playing ? Transport::Pause : Transport::Play);
            }
            break;
        case 2:
            if (flip) {
                for (int v = 0; v < kVoices; ++v) voices_[v].engine.stop();
                svc_.notifyLocalTransport(Transport::Stop);
            }
            break;
        case 3:
            if (flip) {
                for (int v = 0; v < kVoices; ++v) {
                    voices_[v].engine.setParams(voices_[v].params);
                    applyGenerator(v);
                    voices_[v].engine.generate();
                }
            }
            break;
    }
```

Pause must also silence Mid/Bass even when Latch1 drives it — note `pause()` leaves a note ringing by design (gate finishes); that matches today's single-voice behavior, leave as is.

PresetOps (transitional until Plan C makes the v2 blob — keep compiling against the edit voice):

```cpp
bool BerlinMode::savePreset(int slot) {
    Storage* st = svc_.storage();
    return st && saveBerlinPreset(*st, slot, editParams(), editEngine().sequence());
}

bool BerlinMode::loadPreset(int slot) {
    Storage* st = svc_.storage();
    if (!st) return false;
    BerlinParams   p;
    BerlinSequence seq;
    if (!loadBerlinPreset(*st, slot, p, seq)) return false;
    editParams() = p;
    applyGenerator(editVoice_);
    editEngine().setParams(p);
    editEngine().setSequence(seq);
    return true;
}
```

(`presetUsed`/`deletePresetSlot` unchanged.)

Screens: in every `StructureScreen`/`CharacterScreen`/`DynamicsScreen`/`BehaviorScreen` method, replace `mode_.params_` with `mode_.editParams()` and `mode_.engine_` with `mode_.editEngine()` (the const renders already use `mode_.engine()`). `restampGateTicks` calls take `mode_.editEngine()`.

- [ ] **Step 5: Run the Berlin tests; fix the pre-existing ones**

Run: `pio test -e test -f test_berlin_mode -f test_berlin_engine -f test_berlin_generator 2>&1 | tail -8`

The new tests must PASS. Some pre-existing `test_berlin_mode` tests will fail because the edit voice is now High with octaveBase 60 / octaveRange 1 / the screens index shift comes in Task 4. Fix them by these rules (do not weaken assertions):
- Tests that assert notes in register: High's register is now `lo=60, hi=71` (`berlinRegister` with base 60, range 1). Update expected bounds.
- Tests that set `berlin.params().octaveBase`/`octaveRange` explicitly keep working — prefer that over changing expectations.
- `test_algorithm_dispatch`: it must now assert dispatch on the EDIT voice (High); add a final check that the Bass voice's generator never changes: after cycling ALGO, `berlin.engine(core::BerlinMode::kBass)` regenerated via Latch3 still has `seq.step(0).note == berlinBaseRoot(scale, bassParams)` (root anchor).

Expected after fixes: all three Berlin suites PASSED.

- [ ] **Step 6: Run the FULL suite and both builds**

Run: `pio test -e test 2>&1 | tail -3 && pio run -e native 2>&1 | tail -2 && pio run -e teensy41 2>&1 | tail -2`
Expected: all PASSED, both SUCCESS.

- [ ] **Step 7: Commit**

```bash
git add core/modes/BerlinMode.h core/modes/BerlinMode.cpp test/test_berlin_mode/test_berlin_mode.cpp
git commit -m "feat(berlin): three voices (Bass/Mid/High) - engine array, role defaults, per-voice channels"
```

---

### Task 4: `voices` mixer screen + new screen order

**Files:**
- Modify: `core/modes/BerlinMode.h` (VoicesScreen class + member, screenCount 6)
- Modify: `core/modes/BerlinMode.cpp` (screen() mapping + VoicesScreen impl)
- Test: `test/test_berlin_mode/test_berlin_mode.cpp`

New screen order: 0 `structure` · 1 `character` · 2 `voices` · 3 `dynamics` · 4 `behavior` · 5 `presets`.

- [ ] **Step 1: Write the failing tests**

```cpp
static void test_berlin_screen_order_with_voices() {
    core::AppShell shell;
    core::BerlinMode berlin(shell);
    TEST_ASSERT_EQUAL_INT(6, berlin.screenCount());
    TEST_ASSERT_EQUAL_STRING("structure", berlin.screen(0).name());
    TEST_ASSERT_EQUAL_STRING("character", berlin.screen(1).name());
    TEST_ASSERT_EQUAL_STRING("voices",    berlin.screen(2).name());
    TEST_ASSERT_EQUAL_STRING("dynamics",  berlin.screen(3).name());
    TEST_ASSERT_EQUAL_STRING("behavior",  berlin.screen(4).name());
    TEST_ASSERT_EQUAL_STRING("presets",   berlin.screen(5).name());
}

// Mixer: rotate = per-voice channel (clamped 1..16), press = mute toggle.
static void test_voices_screen_channel_and_mute() {
    core::AppShell shell;
    core::BerlinMode berlin(shell);
    FakeMidiOutput out; berlin.setMidiOutput(&out);
    berlin.onEnter();
    core::Screen& mixer = berlin.screen(2);

    mixer.onEncoder(2, +3);                                // Mid: 2 -> 5
    TEST_ASSERT_EQUAL_INT(5, berlin.voiceChannel(core::BerlinMode::kMid));
    mixer.onEncoder(1, -5);                                // Bass clamps at 1
    TEST_ASSERT_EQUAL_INT(1, berlin.voiceChannel(core::BerlinMode::kBass));

    TEST_ASSERT_FALSE(berlin.engine(core::BerlinMode::kHigh).muted());
    mixer.onEncoderSw(3);                                  // mute High
    TEST_ASSERT_TRUE(berlin.engine(core::BerlinMode::kHigh).muted());
    mixer.onEncoderSw(3);                                  // unmute
    TEST_ASSERT_FALSE(berlin.engine(core::BerlinMode::kHigh).muted());

    StubDisplay d;
    mixer.render(d);
    TEST_ASSERT_TRUE(d.drewText("BASS"));
    TEST_ASSERT_TRUE(d.drewText("MID"));
    TEST_ASSERT_TRUE(d.drewText("HIGH"));
}
```

Add `#include "support/StubDisplay.h"` to test_berlin_mode.cpp if it is not already there.

- [ ] **Step 2: Run to verify failure** — `pio test -e test -f test_berlin_mode 2>&1 | tail -5` → compile/assert failures (screenCount 5, no "voices").

- [ ] **Step 3: Implement.** In `BerlinMode.h`, set `screenCount()` to 6 and add the nested class + member (next to the other screens):

```cpp
    // Mixer: one cell per voice — rotate sets the voice's MIDI channel,
    // press toggles its mute (the engine keeps running so unmuting re-enters
    // in phase). Enc4 unused.
    class VoicesScreen : public Screen {
    public:
        explicit VoicesScreen(BerlinMode& m) : mode_(m) {}
        const char* name() const override { return "voices"; }
        void onEncoder(int index, int delta) override;
        void onEncoderSw(int index) override;
        void render(Display& d) const override;
    private:
        BerlinMode& mode_;
    };
    VoicesScreen     voicesScreen_{*this};
```

In `BerlinMode.cpp`, the new `screen()` mapping:

```cpp
Screen& BerlinMode::screen(int i) {
    if (i == 1) return characterScreen_;
    if (i == 2) return voicesScreen_;
    if (i == 3) return dynamicsScreen_;
    if (i == 4) return behaviorScreen_;
    if (i == 5) return presetScreen_;
    return structureScreen_;
}
```

and the implementation (with the voice-name table near the top of the file):

```cpp
static const char* kVoiceNames[core::BerlinMode::kVoices] = {"BASS", "MID", "HIGH"};

void BerlinMode::VoicesScreen::onEncoder(int index, int delta) {
    if (index < 1 || index > kVoices || delta == 0) return;
    Voice& v = mode_.voices_[index - 1];
    int ch = v.channel + delta;
    if (ch < 1)  ch = 1;
    if (ch > 16) ch = 16;
    v.channel = static_cast<uint8_t>(ch);
    v.engine.setOutChannel(v.channel);
}

void BerlinMode::VoicesScreen::onEncoderSw(int index) {
    if (index < 1 || index > kVoices) return;
    BerlinEngine& e = mode_.voices_[index - 1].engine;
    e.setMuted(!e.muted());
}

void BerlinMode::VoicesScreen::render(Display& d) const {
    char buf[12];
    for (int v = 0; v < kVoices; ++v) {
        const bool muted = mode_.voices_[v].engine.muted();
        snprintf(buf, sizeof buf, "CH %d", mode_.voices_[v].channel);
        drawBerlinParamCell(d, v, kVoiceNames[v], buf, muted);
        if (muted)
            d.drawText(v * kBerlinCellW + 4, kBerlinParamTop + 52, "MUTED",
                       color::Gray, color::Black, 1);
    }
    drawBerlinParamDividers(d);
    drawBerlinPianoRoll(d, mode_.engine().sequence(), mode_.engine().playhead(),
                        mode_.engine().soundingNote(), color::Green);
}
```

Note: `kVoiceNames` is declared `static` at file scope — if the compiler rejects `core::BerlinMode::kVoices` there, declare it as `static const char* kVoiceNames[3]`.

- [ ] **Step 4: Fix the preset-screen index in existing tests.** In `test/test_berlin_mode/test_berlin_mode.cpp`, the two preset tests use `berlin.screen(4)` for presets — change to `berlin.screen(5)`. Grep for any other index uses and remap per the new order (dynamics tests `screen(2)` → `screen(3)`, behavior `screen(3)` → `screen(4)`):

Run: `grep -n "\.screen(" test/test_berlin_mode/test_berlin_mode.cpp`

- [ ] **Step 5: Run tests** — `pio test -e test -f test_berlin_mode 2>&1 | tail -3` → PASSED.

- [ ] **Step 6: Full suite + builds** — `pio test -e test 2>&1 | tail -3 && pio run -e native 2>&1 | tail -2 && pio run -e teensy41 2>&1 | tail -2` → all green.

- [ ] **Step 7: Commit**

```bash
git add core/modes/BerlinMode.h core/modes/BerlinMode.cpp test/test_berlin_mode/test_berlin_mode.cpp
git commit -m "feat(berlin): voices mixer screen - per-voice channel + mute, 6-screen order"
```

---

### Task 5: mute-through-the-mode test + sim smoke check

**Files:**
- Test: `test/test_berlin_mode/test_berlin_mode.cpp`

- [ ] **Step 1: Write the test** (mute keeps phase through the mode path)

```cpp
// Muting a voice from the mixer silences only that voice; it keeps phase and
// resumes exactly where the others are.
static void test_mute_keeps_phase_other_voices_sound() {
    core::AppShell shell;
    core::BerlinMode berlin(shell);
    FakeMidiOutput out; berlin.setMidiOutput(&out);
    berlin.onEnter();
    berlin.onRawInput({core::RawInput::Kind::Latch, 1, 0, false});
    berlin.onRawInput({core::RawInput::Kind::Latch, 1, 0, true});
    berlin.screen(2).onEncoderSw(1);                        // mute Bass
    const size_t mark = out.events.size();
    for (int i = 0; i < 12 * 8; ++i) berlin.onClockTick();
    bool bassOn = false, otherOn = false;
    for (size_t i = mark; i < out.events.size(); ++i) {
        if (!out.events[i].isOn) continue;
        if (out.events[i].channel == 1) bassOn = true; else otherOn = true;
    }
    TEST_ASSERT_FALSE(bassOn);
    TEST_ASSERT_TRUE(otherOn);
    // Muted engine kept running in phase with the others.
    TEST_ASSERT_EQUAL_INT(berlin.engine(core::BerlinMode::kHigh).playhead(),
                          berlin.engine(core::BerlinMode::kBass).playhead());
}
```

- [ ] **Step 2: Run** — `pio test -e test -f test_berlin_mode 2>&1 | tail -3` → PASSED (the implementation exists; this is a regression guard).

- [ ] **Step 3: Sim smoke check** — `pio run -e native` then run `.pio/build/native/program` manually: Berlin boots, Space plays — three voices sound on channels 1/2/3 (check the receiving synth/DAW), screen `voices` (rotate Enc5) mutes/unmutes voices with presses `2`/`5`/`8`.

- [ ] **Step 4: Commit**

```bash
git add test/test_berlin_mode/test_berlin_mode.cpp
git commit -m "test(berlin): mute keeps phase, other voices keep sounding"
```
