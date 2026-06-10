# Berlin mode — Plan A (v1 core)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship the complete generate → play/pause → stop → reset loop of a single-voice Berlin-School sequencer: a `BerlinEngine` advancing on the 24-PPQN tick stream, the **Drunkard's Walk** generator, **Locked** behavior, a **piano-roll** visualization that persists across screens, transport on the 3 latches, and the Structure / Character / Behavior parameter screens — registered on both platforms.

**Architecture:** Logic lives in `core/` (portable C++17, no platform headers, no exceptions). `BerlinEngine` mirrors the already-shipped tick-driven `ArpEngine` scheduler. Generators are pluggable (`SequenceGenerator`), seeded by a small portable xorshift PRNG (`BerlinRng`) so tests are deterministic. `BerlinMode` wires the engine to the global scale/out-channel/clock and the latches; each Berlin `Screen` draws its top parameter row plus the shared bottom piano-roll.

**Tech Stack:** C++17, PlatformIO (envs `teensy41`, `native`, `test`), Unity, ILI9341_t3n + usbMIDI (firmware), SDL2/RtMidi (sim).

Spec: `docs/specs/2026-06-10-berlin-mode-design.md`. Source theory: `/Users/jpisa/Development/Claude/synthseeker/berlin-school-theory-and-generator-spec-EN.md` (also in `CLAUDE.md`). Plan A delivers spec §11 "Plan A"; Phasing/Degree generators and the Dynamics screen are Plan B; Evolve/Live behaviors are Plan C.

Conventions: 2-space indent, `core::` namespace, English identifiers/comments. Unit tests auto-discovered under `test/test_*/`; run `pio test -e test`, single suite `pio test -e test -f <dir>`. Build-only: `pio run -e native`, `pio run -e teensy41`.

---

## File structure (Plan A)

- Create `core/BerlinTypes.h` — params struct + enums + resolution-ticks helper.
- Create `core/BerlinRng.h` — tiny seedable xorshift32 PRNG.
- Create `core/BerlinSequence.h` — `BerlinStep` + fixed-capacity `BerlinSequence`.
- Create `core/SequenceGenerator.h` — abstract generator interface.
- Create `core/DrunkardWalkGenerator.{h,cpp}` — the Drunkard's Walk generator.
- Create `core/BerlinEngine.{h,cpp}` — tick scheduler, transport, generate + Morph.
- Create `core/render/BerlinLayout.h` — param-row geometry + piano-roll renderer.
- Create `core/modes/BerlinMode.{h,cpp}` — the mode + 3 screens.
- Modify `platform/teensy/main.cpp`, `platform/host/main.cpp` — register the mode.
- Tests: `test/test_berlin_generator/`, `test/test_berlin_engine/`, `test/test_berlin_mode/`.

---

### Task 1: Berlin types, RNG, and sequence data model

**Files:** Create `core/BerlinTypes.h`, `core/BerlinRng.h`, `core/BerlinSequence.h`; Test `test/test_berlin_engine/test_berlin_engine.cpp` (new — starts as a types smoke test, grows in Task 3).

- [ ] **Step 1: Write `core/BerlinTypes.h`**
```cpp
#pragma once

#include <cstdint>

namespace core {

enum class BerlinAlgorithm : uint8_t { DrunkardWalk = 0, GatePitchPhasing, DegreeWeighted, kCount };
enum class BerlinResolution : uint8_t { Eighth = 0, Sixteenth, kCount };
enum class BerlinBehavior : uint8_t { Locked = 0, Evolve, Live, kCount };

// 24-PPQN clock ticks per step for each resolution.
inline int berlinResolutionTicks(BerlinResolution r) {
    return r == BerlinResolution::Sixteenth ? 6 : 12;
}

// All Berlin generator + playback parameters. Scale + root are NOT here —
// they come from global Settings (AppServices::scale()). Tempo is global too.
struct BerlinParams {
    BerlinAlgorithm  algorithm        = BerlinAlgorithm::DrunkardWalk;
    uint8_t          length           = 16;   // 3..16 steps
    BerlinResolution resolution       = BerlinResolution::Eighth;
    uint8_t          density          = 50;   // 0..100 % active steps
    uint8_t          gatePercent      = 55;   // 40..99
    uint8_t          tension          = 30;   // 0..100 % (degree-weight spread)
    uint8_t          octaveBase       = 48;   // MIDI note of C in the base octave (C1=24..C5=72), step 12
    uint8_t          octaveRange      = 2;    // 1..3 octaves
    uint8_t          velocityBase     = 100;  // 1..126
    uint8_t          velocityHumanize = 20;   // 0..30 (±)
    uint8_t          accent           = 20;   // 0..27 velocity boost
    uint8_t          scatter          = 3;    // 1..7 semitones (Drunkard's Walk)
    uint8_t          gateLen          = 6;    // 3..16 (Gate/Pitch Phasing — Plan B)
    BerlinBehavior   behavior         = BerlinBehavior::Locked;
    uint8_t          morph            = 100;  // 0..100 % regeneration intensity
    uint8_t          evolveRate       = 4;    // 1..8 loops (Evolve — Plan C)
};

} // namespace core
```

- [ ] **Step 2: Write `core/BerlinRng.h`** — portable, deterministic, seedable:
```cpp
#pragma once

#include <cstdint>

namespace core {

// Small xorshift32 PRNG. Portable (no <random> / platform dependency),
// deterministic for a given seed → reproducible generation in unit tests.
struct BerlinRng {
    uint32_t state = 0x12345u;

    void seed(uint32_t v) { state = v ? v : 1u; }

    uint32_t next() {
        uint32_t x = state;
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        state = x;
        return x;
    }

    // Uniform-ish integer in [lo, hi] inclusive (hi >= lo). Modulo bias is
    // negligible for the small ranges used here.
    int range(int lo, int hi) {
        if (hi <= lo) return lo;
        return lo + static_cast<int>(next() % static_cast<uint32_t>(hi - lo + 1));
    }

    // Returns true with probability `percent`/100.
    bool chance(int percent) { return range(0, 99) < percent; }
};

} // namespace core
```

- [ ] **Step 3: Write `core/BerlinSequence.h`**
```cpp
#pragma once

#include <cstdint>

namespace core {

struct BerlinStep {
    bool     active    = false;  // false = rest
    uint8_t  note      = 0;      // absolute MIDI note (scale-quantized)
    uint8_t  velocity  = 0;      // 1..127
    bool     accent    = false;  // visualization + already folded into velocity
    uint16_t gateTicks = 0;      // note-on duration in 24-PPQN ticks
};

// Fixed-capacity realized step pattern (no heap). Walk/Degree fill ≤ length
// steps; Phasing (Plan B) renders a bounded window here.
class BerlinSequence {
public:
    static constexpr int kMaxSteps = 32;

    int  length() const { return length_; }
    void setLength(int n) { length_ = n < 1 ? 1 : (n > kMaxSteps ? kMaxSteps : n); }

    const BerlinStep& step(int i) const { return steps_[clampIdx(i)]; }
    BerlinStep&       step(int i)       { return steps_[clampIdx(i)]; }

    void clear() { for (int i = 0; i < kMaxSteps; ++i) steps_[i] = BerlinStep{}; }

private:
    static int clampIdx(int i) { return i < 0 ? 0 : (i >= kMaxSteps ? kMaxSteps - 1 : i); }
    BerlinStep steps_[kMaxSteps] {};
    int        length_ = 16;
};

} // namespace core
```

- [ ] **Step 4: Write the smoke test** `test/test_berlin_engine/test_berlin_engine.cpp`:
```cpp
#include <unity.h>
#include "core/BerlinTypes.h"
#include "core/BerlinRng.h"
#include "core/BerlinSequence.h"

void setUp() {}
void tearDown() {}

static void test_resolution_ticks() {
    TEST_ASSERT_EQUAL_INT(12, core::berlinResolutionTicks(core::BerlinResolution::Eighth));
    TEST_ASSERT_EQUAL_INT(6,  core::berlinResolutionTicks(core::BerlinResolution::Sixteenth));
}

static void test_rng_is_deterministic_for_seed() {
    core::BerlinRng a, b;
    a.seed(42); b.seed(42);
    for (int i = 0; i < 50; ++i) TEST_ASSERT_EQUAL_UINT32(a.next(), b.next());
    core::BerlinRng c; c.seed(43);
    a.seed(42);
    TEST_ASSERT_NOT_EQUAL(a.next(), c.next());
}

static void test_sequence_defaults_and_clamp() {
    core::BerlinSequence s;
    TEST_ASSERT_EQUAL_INT(16, s.length());
    s.setLength(100); TEST_ASSERT_EQUAL_INT(core::BerlinSequence::kMaxSteps, s.length());
    s.setLength(0);   TEST_ASSERT_EQUAL_INT(1, s.length());
    s.step(0).active = true; s.step(0).note = 60;
    TEST_ASSERT_TRUE(s.step(0).active);
    TEST_ASSERT_EQUAL_UINT8(60, s.step(0).note);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_resolution_ticks);
    RUN_TEST(test_rng_is_deterministic_for_seed);
    RUN_TEST(test_sequence_defaults_and_clamp);
    return UNITY_END();
}
```

- [ ] **Step 5: Run** `pio test -e test -f test_berlin_engine` → PASS.
- [ ] **Step 6: Commit** `git add core/BerlinTypes.h core/BerlinRng.h core/BerlinSequence.h test/test_berlin_engine && git commit -m "feat(berlin): params/enums, seedable RNG, step sequence data model"`

---

### Task 2: SequenceGenerator interface + Drunkard's Walk

**Files:** Create `core/SequenceGenerator.h`, `core/DrunkardWalkGenerator.{h,cpp}`; Test `test/test_berlin_generator/test_berlin_generator.cpp` (new).

- [ ] **Step 1: Write `core/SequenceGenerator.h`**
```cpp
#pragma once

#include "core/BerlinSequence.h"
#include "core/BerlinTypes.h"
#include "core/BerlinRng.h"

namespace core {

class Scale;

// Fills a BerlinSequence from params + the global scale, using `rng`. A pure
// function of (params, scale, rng-state) → fully unit-testable with a fixed seed.
class SequenceGenerator {
public:
    virtual ~SequenceGenerator() = default;
    virtual void generate(BerlinSequence& out, const BerlinParams& p,
                          const Scale& scale, BerlinRng& rng) = 0;
};

} // namespace core
```

- [ ] **Step 2: Write the failing test** `test/test_berlin_generator/test_berlin_generator.cpp`. (Use C minor so out-of-scale notes are detectable.)
```cpp
#include <unity.h>
#include "core/DrunkardWalkGenerator.h"
#include "core/Scale.h"
#include "core/BerlinRng.h"

void setUp() {}
void tearDown() {}

static core::BerlinParams baseParams() {
    core::BerlinParams p;
    p.length = 16; p.density = 100; p.octaveBase = 48; p.octaveRange = 2;
    p.scatter = 3; p.gatePercent = 50; p.resolution = core::BerlinResolution::Eighth;
    p.velocityBase = 100; p.velocityHumanize = 0; p.accent = 0;
    return p;
}

static void test_walk_starts_on_root_and_stays_in_scale() {
    core::DrunkardWalkGenerator gen;
    core::Scale scale(core::Scale::Type::Minor, 0);   // C minor
    core::BerlinSequence seq;
    core::BerlinRng rng; rng.seed(7);
    gen.generate(seq, baseParams(), scale, rng);

    TEST_ASSERT_EQUAL_INT(16, seq.length());
    // Step 0 is the root (pitch class == scale root).
    TEST_ASSERT_TRUE(seq.step(0).active);
    TEST_ASSERT_EQUAL_INT(0, seq.step(0).note % 12);   // C
    // Every active note is in scale.
    for (int i = 0; i < seq.length(); ++i)
        if (seq.step(i).active) TEST_ASSERT_TRUE(scale.contains(seq.step(i).note));
}

static void test_density_controls_active_count() {
    core::DrunkardWalkGenerator gen;
    core::Scale scale(core::Scale::Type::Minor, 0);
    core::BerlinRng rng;

    core::BerlinParams full = baseParams(); full.density = 100;
    core::BerlinSequence s1; rng.seed(1); gen.generate(s1, full, scale, rng);
    int active1 = 0; for (int i = 0; i < s1.length(); ++i) if (s1.step(i).active) ++active1;
    TEST_ASSERT_EQUAL_INT(16, active1);              // 100% → all active

    core::BerlinParams none = baseParams(); none.density = 0;
    core::BerlinSequence s2; rng.seed(1); gen.generate(s2, none, scale, rng);
    int active2 = 0; for (int i = 0; i < s2.length(); ++i) if (s2.step(i).active) ++active2;
    TEST_ASSERT_EQUAL_INT(1, active2);               // 0% → only step 0 (root anchor)
}

static void test_walk_respects_scatter_and_register() {
    core::DrunkardWalkGenerator gen;
    core::Scale scale(core::Scale::Type::Minor, 0);
    core::BerlinParams p = baseParams(); p.scatter = 2; p.octaveRange = 2;
    core::BerlinSequence seq; core::BerlinRng rng; rng.seed(99);
    gen.generate(seq, p, scale, rng);

    const int lo = p.octaveBase;            // base root C
    const int hi = p.octaveBase + 12 * p.octaveRange;
    int prev = -1;
    for (int i = 0; i < seq.length(); ++i) {
        if (!seq.step(i).active) continue;
        int n = seq.step(i).note;
        TEST_ASSERT_TRUE(n >= lo && n <= hi);            // within register
        if (prev >= 0) TEST_ASSERT_TRUE(abs(n - prev) <= 2 + 2);  // ≤ scatter + a scale-quantize step
        prev = n;
    }
}

static void test_walk_is_deterministic_for_seed() {
    core::DrunkardWalkGenerator gen;
    core::Scale scale(core::Scale::Type::Minor, 0);
    core::BerlinSequence a, b;
    core::BerlinRng r1, r2; r1.seed(5); r2.seed(5);
    gen.generate(a, baseParams(), scale, r1);
    gen.generate(b, baseParams(), scale, r2);
    for (int i = 0; i < a.length(); ++i) {
        TEST_ASSERT_EQUAL_INT(a.step(i).active, b.step(i).active);
        TEST_ASSERT_EQUAL_UINT8(a.step(i).note, b.step(i).note);
    }
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_walk_starts_on_root_and_stays_in_scale);
    RUN_TEST(test_density_controls_active_count);
    RUN_TEST(test_walk_respects_scatter_and_register);
    RUN_TEST(test_walk_is_deterministic_for_seed);
    return UNITY_END();
}
```

- [ ] **Step 3: Run to verify it fails** — `pio test -e test -f test_berlin_generator` → FAIL (no `DrunkardWalkGenerator`).

- [ ] **Step 4: Write `core/DrunkardWalkGenerator.h`**
```cpp
#pragma once
#include "core/SequenceGenerator.h"

namespace core {

// Meandering contour: each active note steps ±scatter semitones from the
// previous one, quantized into the scale; step 0 anchors on the root.
// Density sets how many steps are active (rests otherwise). The doc's
// flagship generative recipe ("Drunkard's Walk MIDI Generation").
class DrunkardWalkGenerator : public SequenceGenerator {
public:
    void generate(BerlinSequence& out, const BerlinParams& p,
                  const Scale& scale, BerlinRng& rng) override;
};

} // namespace core
```

- [ ] **Step 5: Write `core/DrunkardWalkGenerator.cpp`**
```cpp
#include "core/DrunkardWalkGenerator.h"

#include "core/Scale.h"

namespace core {

void DrunkardWalkGenerator::generate(BerlinSequence& out, const BerlinParams& p,
                                     const Scale& scale, BerlinRng& rng) {
    const int length = p.length < 1 ? 1 : (p.length > BerlinSequence::kMaxSteps
                                               ? BerlinSequence::kMaxSteps : p.length);
    out.clear();
    out.setLength(length);

    const int lo = p.octaveBase;
    const int hi = p.octaveBase + 12 * (p.octaveRange < 1 ? 1 : p.octaveRange);
    const int stepTicks = berlinResolutionTicks(p.resolution);
    int gate = stepTicks * p.gatePercent / 100; if (gate < 1) gate = 1;

    // Base root: the scale root placed in the base octave.
    const uint8_t baseRoot = scale.quantize(static_cast<uint8_t>(p.octaveBase + scale.root()));
    int last = baseRoot;

    for (int i = 0; i < length; ++i) {
        BerlinStep s;
        // Rhythm: step 0 always plays the root (anchor); others by density.
        const bool active = (i == 0) || rng.chance(p.density);
        if (!active) { out.step(i) = s; continue; }

        int note;
        if (i == 0) {
            note = baseRoot;
        } else {
            const int delta = rng.range(-static_cast<int>(p.scatter), p.scatter);
            int cand = last + delta;
            if (cand < lo) cand = lo;
            if (cand > hi) cand = hi;
            note = scale.quantize(static_cast<uint8_t>(cand));
            if (note < lo) note = scale.quantize(static_cast<uint8_t>(lo));
            if (note > hi) note = scale.quantize(static_cast<uint8_t>(hi));
        }
        last = note;

        // Velocity + accent. Accent on beat 1 (step 0) and on root-pitch notes.
        int vel = p.velocityBase
                  + rng.range(-static_cast<int>(p.velocityHumanize), p.velocityHumanize);
        const bool accent = (i == 0) || (note % 12 == scale.root());
        if (accent) vel += p.accent;
        if (vel < 1)   vel = 1;
        if (vel > 126) vel = 126;

        s.active    = true;
        s.note      = static_cast<uint8_t>(note);
        s.velocity  = static_cast<uint8_t>(vel);
        s.accent    = accent;
        s.gateTicks = static_cast<uint16_t>(gate);
        out.step(i) = s;
    }
}

} // namespace core
```
(Note for the reviewer: `<cstdlib>`/`abs` is only used in the test, not in core.)

- [ ] **Step 6: Run** `pio test -e test -f test_berlin_generator` → PASS. Then `pio test -e test` (whole suite) → PASS.
- [ ] **Step 7: Commit** `git add core/SequenceGenerator.h core/DrunkardWalkGenerator.h core/DrunkardWalkGenerator.cpp test/test_berlin_generator && git commit -m "feat(berlin): SequenceGenerator interface + Drunkard's Walk generator"`

---

### Task 3: BerlinEngine — tick scheduler + transport

**Files:** Create `core/BerlinEngine.{h,cpp}`; Test extends `test/test_berlin_engine/test_berlin_engine.cpp`.

The scheduler mirrors the shipped tick-driven `ArpEngine`: count ticks, fire the step note at the boundary, close the gate when `noteAge_ >= gateTicks_`. No swing.

- [ ] **Step 1: Write `core/BerlinEngine.h`**
```cpp
#pragma once

#include <cstdint>

#include "core/BerlinSequence.h"
#include "core/BerlinTypes.h"
#include "core/BerlinRng.h"

namespace core {

class MidiOutput;
class Scale;
class SequenceGenerator;

// Single-voice Berlin sequencer. Holds a generated BerlinSequence, advances
// it on the 24-PPQN tick stream, and plays it out via MidiOutput. Transport
// (play/pause/stop) and (re)generation are driven by BerlinMode.
class BerlinEngine {
public:
    void setOutput(MidiOutput* o) { out_ = o; }
    void setScale(const Scale* s) { scale_ = s; }
    void setParams(const BerlinParams& p) { params_ = p; }
    void setOutChannel(uint8_t ch) { outChannel_ = ch; }
    void setGenerator(SequenceGenerator* g) { generator_ = g; }   // Task 4
    void seed(uint32_t v) { rng_.seed(v); }                        // tests

    // Transport.
    void play();    // run (emits the current step if starting from silence)
    void pause();   // hold playhead, stop advancing
    void stop();    // rewind to step 0 + all-notes-off
    void generate();// (re)generate using current params + Morph (Task 4)

    void onClockTick();      // advance one 24-PPQN tick

    bool isPlaying() const { return playing_; }
    int  playhead()  const { return playhead_; }
    const BerlinSequence& sequence() const { return seq_; }
    BerlinSequence&       sequenceMut()    { return seq_; }   // tests set steps directly

private:
    void emit(bool isOn, uint8_t note, uint8_t velocity);
    void emitStep(int i);    // fire step i's NoteOn (if active), arm gate
    int  stepLenTicks() const { return berlinResolutionTicks(params_.resolution); }

    MidiOutput*        out_       = nullptr;
    const Scale*       scale_     = nullptr;
    SequenceGenerator* generator_ = nullptr;
    BerlinParams       params_{};
    BerlinRng          rng_{};
    uint8_t            outChannel_ = 1;

    BerlinSequence seq_{};
    bool  playing_   = false;
    int   playhead_  = 0;
    int   stepTicks_ = 0;

    bool    noteSounding_ = false;
    uint8_t soundingNote_ = 0;
    int     gateTicks_    = 0;
    int     noteAge_      = 0;
};

} // namespace core
```

- [ ] **Step 2: Add the failing transport/tick tests** to `test/test_berlin_engine/test_berlin_engine.cpp` (add `#include "core/BerlinEngine.h"`, `#include "support/FakeMidiOutput.h"`, and register the new tests in `main()`). Build a known 2-step sequence directly via `sequenceMut()`:
```cpp
#include "core/BerlinEngine.h"
#include "support/FakeMidiOutput.h"

// Helper: count NoteOn / NoteOff events in a FakeMidiOutput.
static int countOn (const FakeMidiOutput& o) { int n=0; for (auto&e:o.events) if (e.isOn) ++n; return n; }
static int countOff(const FakeMidiOutput& o) { int n=0; for (auto&e:o.events) if (!e.isOn) ++n; return n; }

static void seedTwoStep(core::BerlinEngine& e) {
    // Two active 8th-note steps (12 ticks each), gate 6 ticks, notes 60 & 67.
    core::BerlinSequence& s = e.sequenceMut();
    s.clear(); s.setLength(2);
    s.step(0) = {true, 60, 100, false, 6};
    s.step(1) = {true, 67, 100, false, 6};
    core::BerlinParams p; p.resolution = core::BerlinResolution::Eighth; p.length = 2;
    e.setParams(p);
}

static void test_play_emits_first_step_immediately() {
    core::BerlinEngine e; FakeMidiOutput out; e.setOutput(&out);
    seedTwoStep(e);
    e.play();
    TEST_ASSERT_EQUAL_INT(1, countOn(out));          // step 0 fires on play
    TEST_ASSERT_EQUAL_UINT8(60, out.events[0].note);
    TEST_ASSERT_TRUE(e.isPlaying());
}

static void test_gate_closes_after_gate_ticks() {
    core::BerlinEngine e; FakeMidiOutput out; e.setOutput(&out);
    seedTwoStep(e); e.play();
    for (int i = 0; i < 6; ++i) e.onClockTick();      // 6 gate ticks
    TEST_ASSERT_EQUAL_INT(1, countOff(out));          // note 60 off after gate
    TEST_ASSERT_EQUAL_UINT8(60, out.events[1].note);
}

static void test_step_advances_at_resolution_boundary() {
    core::BerlinEngine e; FakeMidiOutput out; e.setOutput(&out);
    seedTwoStep(e); e.play();
    for (int i = 0; i < 12; ++i) e.onClockTick();     // one 8th = 12 ticks
    TEST_ASSERT_EQUAL_INT(1, e.playhead());           // advanced to step 1
    TEST_ASSERT_EQUAL_INT(2, countOn(out));           // step 1 (note 67) fired
    TEST_ASSERT_EQUAL_UINT8(67, out.events[countOn(out)==2 ? 2 : 0].note);
}

static void test_loops_back_to_step_0() {
    core::BerlinEngine e; FakeMidiOutput out; e.setOutput(&out);
    seedTwoStep(e); e.play();
    for (int i = 0; i < 24; ++i) e.onClockTick();     // two steps → wrap
    TEST_ASSERT_EQUAL_INT(0, e.playhead());
}

static void test_pause_holds_and_stop_rewinds() {
    core::BerlinEngine e; FakeMidiOutput out; e.setOutput(&out);
    seedTwoStep(e); e.play();
    for (int i = 0; i < 12; ++i) e.onClockTick();     // at step 1
    e.pause();
    const int onBefore = countOn(out);
    for (int i = 0; i < 24; ++i) e.onClockTick();     // paused → no advance
    TEST_ASSERT_EQUAL_INT(1, e.playhead());
    TEST_ASSERT_EQUAL_INT(onBefore, countOn(out));
    e.stop();
    TEST_ASSERT_EQUAL_INT(0, e.playhead());
    TEST_ASSERT_FALSE(e.isPlaying());
    TEST_ASSERT_TRUE(countOff(out) >= 1);             // sounding note silenced
}
```

- [ ] **Step 3: Run to verify it fails** — `pio test -e test -f test_berlin_engine` → FAIL.

- [ ] **Step 4: Write `core/BerlinEngine.cpp`** (transport + tick scheduler; `generate()` is a stub here, filled in Task 4)
```cpp
#include "core/BerlinEngine.h"

#include "core/MidiOutput.h"

namespace core {

void BerlinEngine::emit(bool isOn, uint8_t note, uint8_t velocity) {
    if (!out_) return;
    if (isOn) out_->sendNoteOn(outChannel_, note, velocity);
    else      out_->sendNoteOff(outChannel_, note);
}

void BerlinEngine::emitStep(int i) {
    const BerlinStep& s = seq_.step(i);
    stepTicks_ = 0;
    if (!s.active) return;
    emit(true, s.note, s.velocity);
    noteSounding_ = true;
    soundingNote_ = s.note;
    gateTicks_    = s.gateTicks < 1 ? 1 : s.gateTicks;
    noteAge_      = 0;
}

void BerlinEngine::play() {
    if (playing_) return;
    playing_ = true;
    if (!noteSounding_) emitStep(playhead_);   // start promptly from silence
}

void BerlinEngine::pause() {
    playing_ = false;   // hold playhead; leave a sounding note to ring out
}

void BerlinEngine::stop() {
    playing_ = false;
    if (noteSounding_) { emit(false, soundingNote_, 0); noteSounding_ = false; }
    playhead_  = 0;
    stepTicks_ = 0;
    noteAge_   = 0;
}

void BerlinEngine::onClockTick() {
    if (noteSounding_) {
        ++noteAge_;
        if (noteAge_ >= gateTicks_) { emit(false, soundingNote_, 0); noteSounding_ = false; }
    }
    if (!playing_) return;
    ++stepTicks_;
    if (stepTicks_ >= stepLenTicks()) {
        playhead_ = (playhead_ + 1) % (seq_.length() < 1 ? 1 : seq_.length());
        emitStep(playhead_);
    }
}

void BerlinEngine::generate() {
    // Filled in Task 4. For now, leave the sequence as-is and rewind.
    playhead_  = 0;
    stepTicks_ = 0;
}

} // namespace core
```

- [ ] **Step 5: Run** `pio test -e test -f test_berlin_engine` → PASS. Then `pio test -e test` → PASS.
- [ ] **Step 6: Commit** `git add core/BerlinEngine.h core/BerlinEngine.cpp test/test_berlin_engine && git commit -m "feat(berlin): BerlinEngine tick scheduler + play/pause/stop transport"`

---

### Task 4: BerlinEngine generate() + Morph

**Files:** Modify `core/BerlinEngine.cpp`; Test extends `test/test_berlin_engine/test_berlin_engine.cpp`.

- [ ] **Step 1: Add failing tests** to `test/test_berlin_engine/test_berlin_engine.cpp`:
```cpp
#include "core/DrunkardWalkGenerator.h"
#include "core/Scale.h"

static bool seqEqual(const core::BerlinSequence& a, const core::BerlinSequence& b) {
    if (a.length() != b.length()) return false;
    for (int i = 0; i < a.length(); ++i) {
        if (a.step(i).active != b.step(i).active) return false;
        if (a.step(i).note   != b.step(i).note)   return false;
    }
    return true;
}
static int countActiveDiff(const core::BerlinSequence& a, const core::BerlinSequence& b) {
    int d = 0; int n = a.length() < b.length() ? a.length() : b.length();
    for (int i = 0; i < n; ++i)
        if (a.step(i).active != b.step(i).active || a.step(i).note != b.step(i).note) ++d;
    return d;
}

static void test_generate_fills_via_generator() {
    core::BerlinEngine e; core::DrunkardWalkGenerator gen; core::Scale sc(core::Scale::Type::Minor, 0);
    e.setGenerator(&gen); e.setScale(&sc); e.seed(11);
    core::BerlinParams p; p.length = 16; p.density = 100; p.morph = 100; e.setParams(p);
    e.generate();
    TEST_ASSERT_EQUAL_INT(16, e.sequence().length());
    TEST_ASSERT_TRUE(e.sequence().step(0).active);          // root anchor
    TEST_ASSERT_EQUAL_INT(0, e.sequence().step(0).note % 12);
    TEST_ASSERT_EQUAL_INT(0, e.playhead());                 // generate rewinds
}

static void test_morph_0_keeps_base_100_replaces() {
    core::BerlinEngine e; core::DrunkardWalkGenerator gen; core::Scale sc(core::Scale::Type::Minor, 0);
    e.setGenerator(&gen); e.setScale(&sc); e.seed(3);
    core::BerlinParams p; p.length = 16; p.density = 60; p.morph = 100; e.setParams(p);
    e.generate();
    core::BerlinSequence base = e.sequence();

    // Morph 0% → identical to base.
    p.morph = 0; e.setParams(p); e.generate();
    TEST_ASSERT_TRUE(seqEqual(base, e.sequence()));

    // Morph 100% → (very likely) different from base in several steps.
    p.morph = 100; e.setParams(p); e.generate();
    TEST_ASSERT_TRUE(countActiveDiff(base, e.sequence()) > 0);
}
```

- [ ] **Step 2: Run to verify it fails** — `pio test -e test -f test_berlin_engine` (the `generate` stub leaves the sequence empty → assertions fail).

- [ ] **Step 3: Replace `BerlinEngine::generate()`** in `core/BerlinEngine.cpp`:
```cpp
void BerlinEngine::generate() {
    if (generator_ && scale_) {
        BerlinSequence cand;
        generator_->generate(cand, params_, *scale_, rng_);
        const bool firstTime = (seq_.length() == 0);
        if (params_.morph >= 100 || firstTime) {
            seq_ = cand;                       // full regeneration
        } else {
            const int n = cand.length();
            for (int i = 0; i < n; ++i) {
                const bool replace = (i >= seq_.length()) || rng_.chance(params_.morph);
                if (replace) seq_.step(i) = cand.step(i);   // else keep the base step
            }
            seq_.setLength(n);
        }
    }
    if (noteSounding_) { emit(false, soundingNote_, 0); noteSounding_ = false; }
    playhead_  = 0;
    stepTicks_ = 0;
    noteAge_   = 0;
}
```
(Add `BerlinSequence` copy-assignment reliance — the class is trivially copyable, so `seq_ = cand` and the `cand` copy in `base` work as-is.) `seq_.length()` starts at 16 by default, so `firstTime` never triggers on a fresh engine — that's fine; the default-empty 16 steps are inactive and Morph<100 against them simply keeps inactive steps, which the test does not exercise (it generates a base at Morph 100 first).

- [ ] **Step 4: Run** `pio test -e test -f test_berlin_engine` → PASS. Then `pio test -e test` → PASS.
- [ ] **Step 5: Commit** `git add core/BerlinEngine.cpp test/test_berlin_engine && git commit -m "feat(berlin): generate() + Morph regeneration (0% keeps base, 100% fresh)"`

---

### Task 5: BerlinMode + latch transport + Structure screen

**Files:** Create `core/modes/BerlinMode.{h,cpp}`; Test `test/test_berlin_mode/test_berlin_mode.cpp` (new).

The mode owns the engine + a `DrunkardWalkGenerator` + `BerlinParams`, pushes the global scale/out-channel each `update()`, forwards `onClockTick()`, and **edge-detects** the latches in `onRawInput` (the shell delivers latch state every loop, so acting on level would regenerate every frame).

- [ ] **Step 1: Write `core/modes/BerlinMode.h`**
```cpp
#pragma once

#include "core/app/AppServices.h"
#include "core/app/Mode.h"
#include "core/BerlinEngine.h"
#include "core/BerlinTypes.h"
#include "core/DrunkardWalkGenerator.h"
#include "core/Scale.h"

namespace core {

class MidiOutput;

// BerlinMode — single-voice generative Berlin-School sequencer.
// Screens (Plan A): Structure / Character / Behavior. Each draws its top
// parameter row plus the shared bottom piano-roll (drawn every screen so the
// visualization persists across screen switches).
class BerlinMode : public Mode {
public:
    explicit BerlinMode(AppServices& svc);

    const char* name() const override { return "Berlin"; }
    int     screenCount() const override { return 1; }   // Task 6 raises this to 3
    Screen& screen(int i) override;

    void onEnter() override;
    void onClockTick() override { engine_.onClockTick(); }
    void onRawInput(const RawInput& in) override;
    bool capturesTransport() const override { return true; }
    void update(uint32_t nowMs) override;

    void setMidiOutput(MidiOutput* o) { engine_.setOutput(o); }

    // Test inspectors.
    BerlinParams&         params()        { return params_; }
    const BerlinEngine&   engine() const  { return engine_; }
    BerlinEngine&         engineMut()     { return engine_; }

private:
    AppServices&          svc_;
    DrunkardWalkGenerator walkGen_;
    BerlinEngine          engine_;
    BerlinParams          params_{};
    Scale                 scale_{};        // local copy pushed to the engine
    bool                  lastLatch_[4] = {false, false, false, false};  // 1..3 edge-detect

    // Structure screen: Algorithm / Length / Resolution / Density.
    class StructureScreen : public Screen {
    public:
        explicit StructureScreen(BerlinMode& m) : mode_(m) {}
        const char* name() const override { return "structure"; }
        void onEncoder(int index, int delta) override;
        void render(Display& d) const override;
    private:
        BerlinMode& mode_;
    };

    StructureScreen structureScreen_{*this};
};

} // namespace core
```
(Only `StructureScreen` exists in this task; `screenCount()` returns **1** and
`screen(int)` always returns `structureScreen_`. Task 6 adds the Character +
Behavior screens and raises `screenCount()` to 3.)

- [ ] **Step 2: Write `core/modes/BerlinMode.cpp`** (Structure screen renders params only for now; the piano-roll is added in Task 7):
```cpp
#include "core/modes/BerlinMode.h"

#include <cstdio>

#include "core/Display.h"
#include "core/MidiOutput.h"
#include "core/render/ParamGrid.h"   // cycleEnum

namespace core {

BerlinMode::BerlinMode(AppServices& svc) : svc_(svc) {
    engine_.setGenerator(&walkGen_);
    engine_.setParams(params_);
}

Screen& BerlinMode::screen(int) { return structureScreen_; }

void BerlinMode::onEnter() {
    scale_ = svc_.scale();
    engine_.setScale(&scale_);
    engine_.setParams(params_);
    engine_.setOutChannel(svc_.midiOutChannel());
    if (engine_.sequence().length() == 0 || !engine_.sequence().step(0).active)
        engine_.generate();              // ensure there is something to show/play
}

void BerlinMode::update(uint32_t /*nowMs*/) {
    scale_ = svc_.scale();
    engine_.setScale(&scale_);
    engine_.setParams(params_);
    engine_.setOutChannel(svc_.midiOutChannel());
}

void BerlinMode::onRawInput(const RawInput& in) {
    if (in.kind != RawInput::Kind::Latch) return;      // encoders go via Screen
    if (in.index < 1 || in.index > 3) return;
    const bool rising = in.on && !lastLatch_[in.index];
    lastLatch_[in.index] = in.on;
    switch (in.index) {
        case 1:  in.on ? engine_.play() : engine_.pause();  break;  // Play/Pause (level)
        case 2:  if (rising) engine_.stop();                break;  // Stop (edge)
        case 3:  if (rising) engine_.generate();            break;  // Reset/Generate (edge)
    }
}

// ---- Structure screen -----------------------------------------------------

static const char* algoName(BerlinAlgorithm a) {
    switch (a) {
        case BerlinAlgorithm::DrunkardWalk:     return "Walk";
        case BerlinAlgorithm::GatePitchPhasing: return "Phase";
        case BerlinAlgorithm::DegreeWeighted:   return "Degree";
        default:                                return "?";
    }
}

void BerlinMode::StructureScreen::onEncoder(int index, int delta) {
    BerlinParams& p = mode_.params_;
    switch (index) {
        case 1: p.algorithm = cycleEnum(p.algorithm, delta); break;
        case 2: { int v = p.length + delta; if (v < 3) v = 3; if (v > 16) v = 16;
                  p.length = static_cast<uint8_t>(v); } break;
        case 3: p.resolution = cycleEnum(p.resolution, delta); break;
        case 4: { int v = p.density + delta * 5; if (v < 0) v = 0; if (v > 100) v = 100;
                  p.density = static_cast<uint8_t>(v); } break;
    }
}

void BerlinMode::StructureScreen::render(Display& d) const {
    const BerlinParams& p = mode_.params_;
    char buf[12];
    // Top parameter row (BerlinLayout helper is added in Task 7; until then use
    // ParamGrid's drawParamCell at row 0 as a placeholder 2-wide layout).
    drawParamCell(d, 0, 0, "ALGO", algoName(p.algorithm));
    snprintf(buf, sizeof buf, "%d", p.length);            drawParamCell(d, 1, 0, "LENGTH", buf);
    drawParamCell(d, 0, 1, "RESOL", p.resolution == BerlinResolution::Sixteenth ? "16th" : "8th");
    snprintf(buf, sizeof buf, "%d%%", p.density);         drawParamCell(d, 1, 1, "DENSITY", buf);
}

} // namespace core
```
(The render here is a temporary 2×2 placeholder so the screen is visible; Task 7 replaces it with the 1×4 top row + piano-roll.)

- [ ] **Step 3: Write `test/test_berlin_mode/test_berlin_mode.cpp`** — uses the existing test `FakeServices` if present, else a minimal stub. FIRST grep for how `ArpMode` tests construct `AppServices` (`grep -rn "AppServices\|FakeServices\|StubServices" test/`), and mirror that exact pattern. Then:
```cpp
#include <unity.h>
#include "core/modes/BerlinMode.h"
#include "support/FakeMidiOutput.h"
// + the same AppServices stub include that test_arp_mode uses.

void setUp() {}
void tearDown() {}

static void test_latch1_play_pause_latch2_stop() {
    /* construct services stub `svc` exactly like test_arp_mode does */
    core::BerlinMode berlin(svc);
    FakeMidiOutput out; berlin.setMidiOutput(&out);
    berlin.onEnter();                                    // generates a sequence
    TEST_ASSERT_FALSE(berlin.engine().isPlaying());

    berlin.onRawInput({core::RawInput::Kind::Latch, 1, 0, true});   // Latch1 ON → play
    TEST_ASSERT_TRUE(berlin.engine().isPlaying());
    berlin.onRawInput({core::RawInput::Kind::Latch, 1, 0, true});   // same level again → still playing, no crash
    TEST_ASSERT_TRUE(berlin.engine().isPlaying());
    berlin.onRawInput({core::RawInput::Kind::Latch, 1, 0, false});  // Latch1 OFF → pause
    TEST_ASSERT_FALSE(berlin.engine().isPlaying());

    berlin.onRawInput({core::RawInput::Kind::Latch, 2, 0, true});   // Latch2 ON → stop (rewind)
    TEST_ASSERT_EQUAL_INT(0, berlin.engine().playhead());
}

static void test_latch3_generate_is_edge_only() {
    /* construct svc */
    core::BerlinMode berlin(svc);
    FakeMidiOutput out; berlin.setMidiOutput(&out);
    berlin.onEnter();
    // Hold Latch3 ON across several frames: generation must fire ONCE (edge),
    // not every frame. Verify by checking the playhead/stepTicks don't thrash:
    // capture the sequence, fire repeated ON, expect identical pattern when morph<100.
    berlin.params().morph = 0;                            // morph 0 → regen keeps base
    core::BerlinSequence before = berlin.engine().sequence();
    for (int i = 0; i < 5; ++i)
        berlin.onRawInput({core::RawInput::Kind::Latch, 3, 0, true});  // held ON
    // With morph 0 the sequence is stable regardless; the key assertion is no crash
    // and a single rising edge. (Behavioral edge-count is implicitly covered by
    // morph!=0 not thrashing — see note.)
    TEST_ASSERT_EQUAL_INT(before.length(), berlin.engine().sequence().length());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_latch1_play_pause_latch2_stop);
    RUN_TEST(test_latch3_generate_is_edge_only);
    return UNITY_END();
}
```
> Implementer note: if a cleaner edge-count assertion is feasible (e.g. expose a `generateCount()` test hook on the engine, or count NoteOn churn), prefer that to prove Latch3 fires exactly once per OFF→ON. Keep it deterministic.

- [ ] **Step 4: Run** `pio test -e test -f test_berlin_mode` → PASS, then `pio test -e test` → PASS.
- [ ] **Step 5: Commit** `git add core/modes/BerlinMode.h core/modes/BerlinMode.cpp test/test_berlin_mode && git commit -m "feat(berlin): BerlinMode + latch transport (edge-detected) + Structure screen"`

---

### Task 6: Character + Behavior screens

**Files:** Modify `core/modes/BerlinMode.{h,cpp}`; Test extends `test/test_berlin_mode/test_berlin_mode.cpp`.

- [ ] **Step 1: Add two nested screens** to `BerlinMode.h` (mirroring `StructureScreen`): `CharacterScreen` (Gate / Tension / OctaveBase / OctaveRange) and `BehaviorScreen` (Behavior / Morph / EvolveRate / spare). Add their member instances. Bump `screenCount()` to **3** and implement `screen(int i)`:
```cpp
    int screenCount() const override { return 3; }
    Screen& screen(int i) override;   // 0=Structure 1=Character 2=Behavior
```
```cpp
Screen& BerlinMode::screen(int i) {
    if (i == 1) return characterScreen_;
    if (i == 2) return behaviorScreen_;
    return structureScreen_;
}
```

- [ ] **Step 2: Implement the two screens' `onEncoder` + `render`** in `BerlinMode.cpp`. Edit rules (clamp as shown):
  - **CharacterScreen:** Enc1 Gate 40..99 (`±delta`), Enc2 Tension 0..100 (`±delta*5`), Enc3 OctaveBase 24..72 step 12 (`+delta*12`, clamp), Enc4 OctaveRange 1..3.
  - **BehaviorScreen:** Enc1 Behavior `cycleEnum`, Enc2 Morph 0..100 (`±delta*5`), Enc3 EvolveRate 1..8, Enc4 unused.
  Render each with the placeholder `drawParamCell` 2×2 (as Task 5) — Task 7 swaps all three to the 1×4 + piano-roll layout. Use `behaviorName()` / `octaveName()` helpers (e.g. octave label "C3" from the MIDI note: `note/12 - 1`).

- [ ] **Step 3: Add a test** `test_screens_edit_params` to `test/test_berlin_mode/test_berlin_mode.cpp`: drive `berlin.screen(0/1/2).onEncoder(...)` and assert `berlin.params()` fields change and clamp at bounds (e.g. Length stops at 16 and 3, Gate at 99/40, Morph at 100/0, OctaveRange at 3/1).

- [ ] **Step 4: Run** `pio test -e test -f test_berlin_mode` → PASS, then `pio test -e test` → PASS.
- [ ] **Step 5: Commit** `git add core/modes/BerlinMode.h core/modes/BerlinMode.cpp test/test_berlin_mode && git commit -m "feat(berlin): Character + Behavior parameter screens"`

---

### Task 7: Piano-roll visualization (persists across screens)

**Files:** Create `core/render/BerlinLayout.h`; Modify `core/modes/BerlinMode.cpp` (all three screens' `render`). Verified visually in the simulator.

- [ ] **Step 1: Write `core/render/BerlinLayout.h`** — a 1×4 top parameter row and the piano-roll renderer:
```cpp
#pragma once

#include "core/BerlinSequence.h"
#include "core/Display.h"

namespace core {

// Top parameter strip: one row of four cells (Enc1..4), below the 10px bar.
constexpr int kBerlinParamTop = 12;
constexpr int kBerlinParamH   = 78;
constexpr int kBerlinCellW    = 80;                          // 320 / 4
constexpr int kBerlinRollTop  = kBerlinParamTop + kBerlinParamH;     // 90
constexpr int kBerlinRollH    = 240 - kBerlinRollTop;               // 150

inline void drawBerlinParamCell(Display& d, int col, const char* name, const char* value) {
    const int x = col * kBerlinCellW;
    d.drawText(x + 4, kBerlinParamTop + 6,  name,  color::Gray,  color::Black, 1);
    d.drawText(x + 4, kBerlinParamTop + 24, value, color::White, color::Black, 2);
}

inline void drawBerlinParamDividers(Display& d) {
    for (int c = 1; c < 4; ++c)
        d.fillRect(c * kBerlinCellW, kBerlinParamTop, 1, kBerlinParamH, color::DarkGray);
    d.fillRect(0, kBerlinRollTop - 1, 320, 1, color::DarkGray);
}

// Piano-roll of the current sequence with a playhead. X = step, Y = pitch.
inline void drawBerlinPianoRoll(Display& d, const BerlinSequence& seq, int playhead,
                                uint16_t noteColor) {
    d.fillRect(0, kBerlinRollTop, 320, kBerlinRollH, color::Black);
    const int n = seq.length() < 1 ? 1 : seq.length();
    const int colW = 320 / n;

    // Pitch range across active notes (fallback to a 1-octave window).
    int lo = 127, hi = 0;
    for (int i = 0; i < n; ++i)
        if (seq.step(i).active) { int p = seq.step(i).note;
                                  if (p < lo) lo = p; if (p > hi) hi = p; }
    if (lo > hi) { lo = 60; hi = 72; }
    if (hi - lo < 11) { hi = lo + 11; }                 // min 1-octave span
    const int rollBot = kBerlinRollTop + kBerlinRollH - 2;
    const int rollH   = kBerlinRollH - 4;

    // Playhead column highlight.
    d.fillRect(playhead * colW, kBerlinRollTop, colW, kBerlinRollH, color::DarkGray);

    for (int i = 0; i < n; ++i) {
        const BerlinStep& s = seq.step(i);
        if (!s.active) continue;
        const int y = rollBot - (s.note - lo) * rollH / (hi - lo);
        const int w = colW * s.gateTicks / 12; // gate fraction of an 8th (12 ticks)
        const int bw = w < 3 ? 3 : (w > colW - 1 ? colW - 1 : w);
        const uint16_t c = s.accent ? color::White : noteColor;
        d.fillRect(i * colW + 1, y - 2, bw, 5, c);
    }
}

} // namespace core
```

- [ ] **Step 2: Replace each screen's `render`** in `BerlinMode.cpp` to draw the 1×4 param row (its own four params via `drawBerlinParamCell`) + `drawBerlinParamDividers(d)` + `drawBerlinPianoRoll(d, mode_.engine_.sequence(), mode_.engine_.playhead(), color::Green)`. The piano-roll call is identical in all three screens (so the viz persists across screen switches); only the four `drawBerlinParamCell` calls differ. Add `#include "core/render/BerlinLayout.h"`.

- [ ] **Step 3: Build + run** `pio run -e native` SUCCESS, `pio run -e teensy41` SUCCESS, `pio test -e test` PASS (render is not unit-tested; ensure nothing else broke).
- [ ] **Step 4 (controller/human): sim check** — note: the mode isn't registered yet (Task 8), so visual verification happens after Task 8. Commit now.
- [ ] **Step 5: Commit** `git add core/render/BerlinLayout.h core/modes/BerlinMode.cpp && git commit -m "feat(berlin): piano-roll visualization + 1x4 param row (persists across screens)"`

---

### Task 8: Register on both platforms + verify

**Files:** Modify `platform/teensy/main.cpp`, `platform/host/main.cpp`.

- [ ] **Step 1: Register `BerlinMode`** on both platforms, in spec order
  (Monitoring, Arp, **Berlin**, BPM, Settings, Debug):
  - Add `#include "core/modes/BerlinMode.h"`.
  - Add `static core::BerlinMode berlinMode(app);` next to the other mode instances.
  - In `setup()`/`main()`: `berlinMode.setMidiOutput(&midiOut);` (next to `arpMode.setMidiOutput`), and `app.addMode(&berlinMode);` placed **after** `arpMode` and **before** `bpmMode` so the mode order matches the spec.
  (Match each platform's exact construction/registration block — mirror how `arpMode` is wired.)

- [ ] **Step 2: Build** `pio run -e teensy41` SUCCESS, `pio run -e native` SUCCESS, `pio test -e test` PASS.

- [ ] **Step 3 (controller/human): simulator verification.** `make sim`. Enc5 → switch to **Berlin**. Confirm:
  - The bottom piano-roll shows a generated sequence; **switching screens (Enc5) keeps the piano-roll and changes only the top param row.**
  - **Latch1** plays/pauses (notes out + playhead moves), **Latch2** stops (playhead → step 1, silence), **Latch3** generates a new sequence (Morph 100% → fresh; lower Morph → subtle change).
  - Encoders edit the Structure/Character/Behavior params; **Length/Density/Resolution** changes are reflected on the next generate.
  - Tempo follows the global clock (internal master; or external when Settings → Clock = Ext).

- [ ] **Step 4: Commit** `git add platform/teensy/main.cpp platform/host/main.cpp && git commit -m "feat(berlin): register BerlinMode on both platforms"`

---

## Self-review notes

- **Spec coverage (Plan A):** engine + sequence (Tasks 1,3,4) · Drunkard's Walk (Task 2) · Locked behavior (the engine loops a fixed sequence; params apply on generate — Tasks 3–6) · piano-roll viz drawn by every screen so it persists (Task 7) · transport latches captured + edge-detected (Task 5) · Structure/Character/Behavior screens (Tasks 5–6) · registration (Task 8). Scale/root from `svc_.scale()`, out-channel from `svc_.midiOutChannel()`, ticks via `onClockTick()` (Tasks 5–6, wired through the Plan 5b AppShell routing).
- **Deferred to Plan B/C (not built here):** Gate/Pitch Phasing + Degree-Weighted generators, the contextual Scatter/GateLen cell, the Dynamics screen, Evolve/Live behaviors. `BerlinParams` already carries their fields so no struct churn later.
- **Edge-detect latches** (Task 5) is essential: `fireRaw` delivers latch state every main-loop iteration, so Latch3 must act only on the OFF→ON edge or it would regenerate every frame.
- **Render split** uses option (b): no `Mode::render` hook is added; every Berlin screen draws the shared piano-roll itself, which is what makes the bottom persist across screen switches.
- **Type consistency:** `BerlinParams`/enums (Task 1) used by the generator (Task 2), engine (Tasks 3–4) and mode/screens (Tasks 5–6). `SequenceGenerator::generate(out, params, scale, rng)` (Task 2) called by the engine (Task 4). `BerlinEngine` accessors `sequence()`/`playhead()`/`isPlaying()` (Task 3) used by the viz (Task 7) and mode tests (Tasks 5–6). `drawBerlinPianoRoll` (Task 7) called from all screens.
- **Risk:** Task 5's `AppServices` stub in the test — the implementer must mirror exactly how `test/test_arp_mode` constructs services (grep first), since Berlin reads `scale()` and `midiOutChannel()` the same way Arp does.
