# Berlin mode — Plan B (more algorithms + dynamics)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the Algorithm selector functional by adding the **Gate/Pitch Phasing** and **Degree-Weighted** generators behind the existing pluggable interface, wire the mode to dispatch by `params.algorithm`, and add the **Dynamics** parameter screen (velocity / humanize / accent) plus the **contextual cell** (Scatter for Walk, GateLen for Phasing).

**Architecture:** Builds on Plan A (shipped, merged as v0.6). The engine and `SequenceGenerator` interface are unchanged; this plan adds two generators, a small shared helper module (`core/BerlinGen.*`) used by all three to keep them DRY, a `Scale::degreeCount()` accessor, generator dispatch in `BerlinMode`, and one new screen. Everything stays in `core/` (portable C++17, no platform headers, no exceptions).

**Tech Stack:** C++17, PlatformIO (envs `teensy41`, `native`, `test`), Unity. Run `pio test -e test`, single suite `pio test -e test -f <dir>`. Build-only: `pio run -e native`, `pio run -e teensy41`.

Spec: `docs/specs/2026-06-10-berlin-mode-design.md` (§4.2/§4.3 generators, §7 screens, §11 "Plan B"). Source theory: `/Users/jpisa/Development/Claude/synthseeker/berlin-school-theory-and-generator-spec-EN.md`. Plan A artifacts: `core/BerlinEngine.*`, `core/DrunkardWalkGenerator.*`, `core/modes/BerlinMode.*`, `core/render/BerlinLayout.h`.

Conventions: 2-space indent, `core::` namespace, English. `BerlinRng` is the portable seedable PRNG (`seed/next/range(lo,hi)/chance(percent)`). `Scale` (`core/Scale.h`) has `contains/quantize/degreeNote/root`; `intervals()` is private (returns the degree count).

---

## File structure (Plan B)

- Modify `core/Scale.h`, `core/Scale.cpp` — add public `int degreeCount() const`.
- Create `core/BerlinGen.{h,cpp}` — shared generator helpers (base root, register, gate, velocity, octave-fold, interval-weighted degree pick).
- Modify `core/DrunkardWalkGenerator.cpp` — refactor to use the shared helpers (behavior-preserving; existing tests guard it).
- Create `core/DegreeWeightedGenerator.{h,cpp}` — the Degree-Weighted generator.
- Create `core/GatePitchPhasingGenerator.{h,cpp}` — the Gate/Pitch Phasing generator.
- Modify `core/modes/BerlinMode.{h,cpp}` — hold all three generators, dispatch by `params.algorithm`; add the Dynamics screen + contextual cell; `screenCount()` → 4.
- Tests: `test/test_berlin_generator/` (extend), `test/test_berlin_mode/` (extend).

---

### Task 1: Shared generator helpers + `Scale::degreeCount()`

**Files:** Modify `core/Scale.{h,cpp}`; Create `core/BerlinGen.{h,cpp}`; Modify `core/DrunkardWalkGenerator.cpp`; Test `test/test_berlin_generator/test_berlin_generator.cpp` (extend).

- [ ] **Step 1: Add `Scale::degreeCount()`.** In `core/Scale.h`, in the public section after `degreeNote`:
```cpp
    int degreeCount() const;                     // number of notes in the scale (5..8)
```
In `core/Scale.cpp`, after `intervals(...)`:
```cpp
int Scale::degreeCount() const {
    const uint8_t* ivs = nullptr;
    return intervals(&ivs);
}
```

- [ ] **Step 2: Write `core/BerlinGen.h`**
```cpp
#pragma once

#include <cstdint>

#include "core/BerlinTypes.h"
#include "core/BerlinRng.h"

namespace core {

class Scale;

// Shared building blocks for the Berlin generators. Keeping these in one place
// lets Drunkard's Walk, Degree-Weighted and Gate/Pitch Phasing share identical
// register/gate/velocity/quantize behavior.

// Quantized scale root placed in the params' base octave.
uint8_t berlinBaseRoot(const Scale& scale, const BerlinParams& p);

// Register [lo, hi] (MIDI notes), clamped into valid MIDI so an octave-fold
// can't wrap. hi = octaveBase + 12*octaveRange.
void berlinRegister(const BerlinParams& p, int& lo, int& hi);

// Note-on duration in 24-PPQN ticks for the params' resolution + gate% (min 1).
int berlinGateTicks(const BerlinParams& p);

// Fold a note into [lo, hi] by whole octaves — preserves the pitch class (so it
// stays in scale) while bringing it inside the register.
int berlinFoldIntoRegister(int note, int lo, int hi);

// Velocity = base ± humanize, +accent when `accent`, clamped 1..126.
uint8_t berlinFinalizeVelocity(const BerlinParams& p, bool accent, BerlinRng& rng);

// Pick an in-scale note by interval-consonance weight (root/fifth heavy),
// spread flatter by Tension, in a random octave within the register. Always in
// scale and inside [lo, hi].
uint8_t berlinDegreeWeightedNote(const Scale& scale, uint8_t baseRoot,
                                 const BerlinParams& p, BerlinRng& rng);

} // namespace core
```

- [ ] **Step 3: Write `core/BerlinGen.cpp`**
```cpp
#include "core/BerlinGen.h"

#include "core/Scale.h"

namespace core {

uint8_t berlinBaseRoot(const Scale& scale, const BerlinParams& p) {
    return scale.quantize(static_cast<uint8_t>(p.octaveBase + scale.root()));
}

void berlinRegister(const BerlinParams& p, int& lo, int& hi) {
    lo = p.octaveBase;
    hi = p.octaveBase + 12 * (p.octaveRange < 1 ? 1 : p.octaveRange);
    if (hi > 127) hi = 127;
    if (lo > hi)  lo = hi;
}

int berlinGateTicks(const BerlinParams& p) {
    int g = berlinResolutionTicks(p.resolution) * p.gatePercent / 100;
    return g < 1 ? 1 : g;
}

int berlinFoldIntoRegister(int note, int lo, int hi) {
    while (note > hi) note -= 12;
    while (note < lo) note += 12;
    return note;
}

uint8_t berlinFinalizeVelocity(const BerlinParams& p, bool accent, BerlinRng& rng) {
    int vel = p.velocityBase
              + rng.range(-static_cast<int>(p.velocityHumanize), p.velocityHumanize);
    if (accent) vel += p.accent;
    if (vel < 1)   vel = 1;
    if (vel > 126) vel = 126;     // leave 127 as headroom (per the Berlin doc)
    return static_cast<uint8_t>(vel);
}

// Interval consonance weight (0..100) by semitone (mod 12). From spec §2.1:
// root/fifth highest, tritone/minor-second lowest.
static int intervalWeight(int semitone) {
    switch (((semitone % 12) + 12) % 12) {
        case 0:  return 100;   // unison / root
        case 7:  return 95;    // perfect fifth
        case 5:  return 85;    // perfect fourth
        case 4:  return 55;    // major third
        case 3:  return 55;    // minor third
        case 9:  return 45;    // major sixth
        case 8:  return 45;    // minor sixth
        case 2:  return 35;    // major second
        case 10: return 30;    // minor seventh
        case 11: return 30;    // major seventh
        case 1:  return 15;    // minor second
        case 6:  return 5;     // tritone
        default: return 30;
    }
}

uint8_t berlinDegreeWeightedNote(const Scale& scale, uint8_t baseRoot,
                                 const BerlinParams& p, BerlinRng& rng) {
    int lo, hi; berlinRegister(p, lo, hi);
    const int degs = scale.degreeCount();
    const int tension = p.tension > 100 ? 100 : p.tension;

    // Build spread weights: tension flattens toward uniform (100).
    int weights[8];                 // degreeCount is 5..8
    int total = 0;
    for (int i = 0; i < degs; ++i) {
        const int semis = (static_cast<int>(scale.degreeNote(baseRoot, i)) - baseRoot + 120) % 12;
        int w = intervalWeight(semis);
        w = w + (100 - w) * tension / 100;        // higher tension → flatter
        weights[i] = w;
        total += w;
    }

    // Weighted pick of a degree index.
    int pick = rng.range(0, total - 1);
    int idx = 0;
    for (int i = 0; i < degs; ++i) { pick -= weights[i]; if (pick < 0) { idx = i; break; } }

    // Random octave within the range, then fold into the register for safety.
    const int oct = rng.range(0, (p.octaveRange < 1 ? 1 : p.octaveRange) - 1);
    int note = scale.degreeNote(baseRoot, idx + degs * oct);
    note = berlinFoldIntoRegister(note, lo, hi);
    return static_cast<uint8_t>(note);
}

} // namespace core
```

- [ ] **Step 4: Refactor `core/DrunkardWalkGenerator.cpp` to use the helpers** — behavior-preserving (the existing 5 tests must still pass). Replace the inline register/gate/baseRoot/fold/velocity code with helper calls, keeping the SAME rng call order (scatter range, then velocity humanize):
```cpp
#include "core/DrunkardWalkGenerator.h"

#include "core/BerlinGen.h"
#include "core/Scale.h"

namespace core {

void DrunkardWalkGenerator::generate(BerlinSequence& out, const BerlinParams& p,
                                     const Scale& scale, BerlinRng& rng) {
    const int length = p.length < 1 ? 1 : (p.length > BerlinSequence::kMaxSteps
                                               ? BerlinSequence::kMaxSteps : p.length);
    out.clear();
    out.setLength(length);

    int lo, hi; berlinRegister(p, lo, hi);
    const int gate = berlinGateTicks(p);
    const uint8_t baseRoot = berlinBaseRoot(scale, p);
    int last = baseRoot;

    for (int i = 0; i < length; ++i) {
        BerlinStep s;
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
            note = scale.quantize(static_cast<uint8_t>(cand < 0 ? 0 : (cand > 127 ? 127 : cand)));
            note = berlinFoldIntoRegister(note, lo, hi);
        }
        last = note;

        const bool accent = (i == 0) || (note % 12 == scale.root());
        s.active    = true;
        s.note      = static_cast<uint8_t>(note);
        s.velocity  = berlinFinalizeVelocity(p, accent, rng);
        s.accent    = accent;
        s.gateTicks = static_cast<uint16_t>(gate);
        out.step(i) = s;
    }
}

} // namespace core
```

- [ ] **Step 5: Add a helper test** to `test/test_berlin_generator/test_berlin_generator.cpp` — `test_degree_weighted_note_in_scale_and_register` (loop roots 0..11 over Minor + PentaMinor, octaveBase 48, range 2; call `berlinDegreeWeightedNote` 200× and assert each result is `scale.contains(n)` and `lo<=n<=hi`). Add `#include "core/BerlinGen.h"`. Register in `main()`.

- [ ] **Step 6: Run** `pio test -e test -f test_berlin_generator` → all PASS (the 5 existing Drunkard's Walk tests still pass after the refactor + the new helper test). Then `pio test -e test` → PASS.
- [ ] **Step 7: Commit** `git add core/Scale.h core/Scale.cpp core/BerlinGen.h core/BerlinGen.cpp core/DrunkardWalkGenerator.cpp test/test_berlin_generator && git commit -m "feat(berlin): shared generator helpers + Scale::degreeCount(); refactor Drunkard's Walk"`

---

### Task 2: Degree-Weighted generator

**Files:** Create `core/DegreeWeightedGenerator.{h,cpp}`; Test `test/test_berlin_generator/test_berlin_generator.cpp` (extend).

Each active step independently picks a scale degree by interval-consonance weight (root/fifth heavy), spread by Tension, root-anchored on step 0. The simplest generator — no contour memory.

- [ ] **Step 1: Write `core/DegreeWeightedGenerator.h`**
```cpp
#pragma once
#include "core/SequenceGenerator.h"

namespace core {

// Each active step picks a scale degree weighted toward root/fifth (Tension
// flattens the weights). Step 0 anchors on the root. Density gates rests.
class DegreeWeightedGenerator : public SequenceGenerator {
public:
    void generate(BerlinSequence& out, const BerlinParams& p,
                  const Scale& scale, BerlinRng& rng) override;
};

} // namespace core
```

- [ ] **Step 2: Write the failing test** in `test/test_berlin_generator/test_berlin_generator.cpp` (add `#include "core/DegreeWeightedGenerator.h"`, register in `main()`):
```cpp
static void test_degree_generator_in_scale_root_anchored() {
    core::DegreeWeightedGenerator gen;
    core::Scale scale(core::Scale::Type::Minor, 3);     // Eb minor (non-C root)
    core::BerlinParams p = baseParams(); p.density = 100; p.octaveBase = 48; p.octaveRange = 2;
    core::BerlinSequence seq; core::BerlinRng rng; rng.seed(21);
    gen.generate(seq, p, scale, rng);

    TEST_ASSERT_EQUAL_INT(16, seq.length());
    TEST_ASSERT_TRUE(seq.step(0).active);
    TEST_ASSERT_EQUAL_INT(scale.root(), seq.step(0).note % 12);   // root anchor
    const int lo = p.octaveBase, hi = p.octaveBase + 12 * p.octaveRange;
    for (int i = 0; i < seq.length(); ++i)
        if (seq.step(i).active) {
            TEST_ASSERT_TRUE(scale.contains(seq.step(i).note));
            TEST_ASSERT_TRUE(seq.step(i).note >= lo && seq.step(i).note <= hi);
        }
}

static void test_degree_generator_density_and_determinism() {
    core::DegreeWeightedGenerator gen;
    core::Scale scale(core::Scale::Type::Minor, 0);
    core::BerlinParams none = baseParams(); none.density = 0;
    core::BerlinSequence s; core::BerlinRng rng; rng.seed(1); gen.generate(s, none, scale, rng);
    int active = 0; for (int i = 0; i < s.length(); ++i) if (s.step(i).active) ++active;
    TEST_ASSERT_EQUAL_INT(1, active);                   // 0% → only the root anchor

    core::BerlinSequence a, b; core::BerlinRng r1, r2; r1.seed(9); r2.seed(9);
    core::BerlinParams p = baseParams(); p.density = 70;
    gen.generate(a, p, scale, r1); gen.generate(b, p, scale, r2);
    for (int i = 0; i < a.length(); ++i) {
        TEST_ASSERT_EQUAL_INT(a.step(i).active, b.step(i).active);
        TEST_ASSERT_EQUAL_UINT8(a.step(i).note, b.step(i).note);
    }
}
```

- [ ] **Step 3: Run to verify FAIL** — `pio test -e test -f test_berlin_generator`.

- [ ] **Step 4: Write `core/DegreeWeightedGenerator.cpp`**
```cpp
#include "core/DegreeWeightedGenerator.h"

#include "core/BerlinGen.h"
#include "core/Scale.h"

namespace core {

void DegreeWeightedGenerator::generate(BerlinSequence& out, const BerlinParams& p,
                                       const Scale& scale, BerlinRng& rng) {
    const int length = p.length < 1 ? 1 : (p.length > BerlinSequence::kMaxSteps
                                               ? BerlinSequence::kMaxSteps : p.length);
    out.clear();
    out.setLength(length);

    const int gate = berlinGateTicks(p);
    const uint8_t baseRoot = berlinBaseRoot(scale, p);

    for (int i = 0; i < length; ++i) {
        BerlinStep s;
        const bool active = (i == 0) || rng.chance(p.density);
        if (!active) { out.step(i) = s; continue; }

        const int note = (i == 0) ? baseRoot
                                  : berlinDegreeWeightedNote(scale, baseRoot, p, rng);
        const bool accent = (i == 0) || (note % 12 == scale.root());

        s.active    = true;
        s.note      = static_cast<uint8_t>(note);
        s.velocity  = berlinFinalizeVelocity(p, accent, rng);
        s.accent    = accent;
        s.gateTicks = static_cast<uint16_t>(gate);
        out.step(i) = s;
    }
}

} // namespace core
```

- [ ] **Step 5: Run** `pio test -e test -f test_berlin_generator` → PASS. Then `pio test -e test` → PASS.
- [ ] **Step 6: Commit** `git add core/DegreeWeightedGenerator.h core/DegreeWeightedGenerator.cpp test/test_berlin_generator && git commit -m "feat(berlin): Degree-Weighted generator"`

---

### Task 3: Gate/Pitch Phasing generator

**Files:** Create `core/GatePitchPhasingGenerator.{h,cpp}`; Test `test/test_berlin_generator/test_berlin_generator.cpp` (extend).

Runs a PITCH list (length `params.length`) against a GATE list (length `params.gateLen`) of different lengths: realized step `i` plays `pitch[i % P]` gated by `gate[i % G]`. The realized sequence length = `lcm(P, G)`, capped at `kMaxSteps` (32) — a long evolving pattern that "sounds random but isn't." Pitch list is root-heavy (degree-weighted); gate list is density-gated.

- [ ] **Step 1: Write `core/GatePitchPhasingGenerator.h`**
```cpp
#pragma once
#include "core/SequenceGenerator.h"

namespace core {

// Note-phasing within one voice: a pitch list (length = params.length) and a
// gate list (length = params.gateLen) of different lengths run against each
// other; realized step i = pitch[i % P] gated by gate[i % G]. The realized
// pattern length is lcm(P, G), capped at BerlinSequence::kMaxSteps.
class GatePitchPhasingGenerator : public SequenceGenerator {
public:
    void generate(BerlinSequence& out, const BerlinParams& p,
                  const Scale& scale, BerlinRng& rng) override;
};

} // namespace core
```

- [ ] **Step 2: Write the failing test** in `test/test_berlin_generator/test_berlin_generator.cpp` (add `#include "core/GatePitchPhasingGenerator.h"`, register in `main()`):
```cpp
static int igcd(int a, int b) { while (b) { int t = a % b; a = b; b = t; } return a; }

static void test_phasing_length_is_capped_lcm() {
    core::GatePitchPhasingGenerator gen;
    core::Scale scale(core::Scale::Type::Minor, 0);
    core::BerlinParams p = baseParams(); p.length = 8; p.gateLen = 6;   // lcm(8,6)=24
    core::BerlinSequence seq; core::BerlinRng rng; rng.seed(4);
    gen.generate(seq, p, scale, rng);
    const int lcm = 8 / igcd(8, 6) * 6;
    const int expect = lcm < core::BerlinSequence::kMaxSteps ? lcm : core::BerlinSequence::kMaxSteps;
    TEST_ASSERT_EQUAL_INT(expect, seq.length());        // 24
}

static void test_phasing_in_scale_and_capped() {
    core::GatePitchPhasingGenerator gen;
    core::Scale scale(core::Scale::Type::PentaMinor, 7);
    core::BerlinParams p = baseParams(); p.length = 16; p.gateLen = 15; p.density = 100;
    core::BerlinSequence seq; core::BerlinRng rng; rng.seed(8);
    gen.generate(seq, p, scale, rng);
    TEST_ASSERT_EQUAL_INT(core::BerlinSequence::kMaxSteps, seq.length());  // lcm(16,15)=240 → capped 32
    const int lo = p.octaveBase, hi = p.octaveBase + 12 * p.octaveRange;
    for (int i = 0; i < seq.length(); ++i)
        if (seq.step(i).active) {
            TEST_ASSERT_TRUE(scale.contains(seq.step(i).note));
            TEST_ASSERT_TRUE(seq.step(i).note >= lo && seq.step(i).note <= hi);
        }
}

static void test_phasing_repeats_pitch_by_period() {
    // With density 100 (all gates open) every realized step is active, and
    // pitch[i] must equal pitch[i % P]. Verify the pitch list repeats every P.
    core::GatePitchPhasingGenerator gen;
    core::Scale scale(core::Scale::Type::Minor, 0);
    core::BerlinParams p = baseParams(); p.length = 5; p.gateLen = 8; p.density = 100;
    core::BerlinSequence seq; core::BerlinRng rng; rng.seed(2);
    gen.generate(seq, p, scale, rng);
    const int P = 5;
    for (int i = P; i < seq.length(); ++i)
        TEST_ASSERT_EQUAL_UINT8(seq.step(i % P).note, seq.step(i).note);
}
```

- [ ] **Step 3: Run to verify FAIL** — `pio test -e test -f test_berlin_generator`.

- [ ] **Step 4: Write `core/GatePitchPhasingGenerator.cpp`**
```cpp
#include "core/GatePitchPhasingGenerator.h"

#include "core/BerlinGen.h"
#include "core/Scale.h"

namespace core {

static int gcdInt(int a, int b) { while (b) { int t = a % b; a = b; b = t; } return a; }

void GatePitchPhasingGenerator::generate(BerlinSequence& out, const BerlinParams& p,
                                         const Scale& scale, BerlinRng& rng) {
    auto clampLen = [](int v) { return v < 1 ? 1 : (v > 16 ? 16 : v); };
    const int P = clampLen(p.length);
    const int G = clampLen(p.gateLen);

    const uint8_t baseRoot = berlinBaseRoot(scale, p);
    const int gate = berlinGateTicks(p);

    // PITCH list (root-heavy). pitch[0] = root.
    uint8_t pitch[16];
    for (int k = 0; k < P; ++k)
        pitch[k] = (k == 0) ? baseRoot : berlinDegreeWeightedNote(scale, baseRoot, p, rng);

    // GATE list (density-gated). gate[0] = on.
    bool gateOn[16];
    for (int k = 0; k < G; ++k)
        gateOn[k] = (k == 0) ? true : rng.chance(p.density);

    // Realized length = lcm(P, G), capped at kMaxSteps.
    int lcm = P / gcdInt(P, G) * G;
    if (lcm > BerlinSequence::kMaxSteps) lcm = BerlinSequence::kMaxSteps;

    out.clear();
    out.setLength(lcm);
    for (int i = 0; i < lcm; ++i) {
        BerlinStep s;
        if (gateOn[i % G]) {
            const uint8_t note = pitch[i % P];
            const bool accent = (note % 12 == scale.root());
            s.active    = true;
            s.note      = note;
            s.velocity  = berlinFinalizeVelocity(p, accent, rng);
            s.accent    = accent;
            s.gateTicks = static_cast<uint16_t>(gate);
        }
        out.step(i) = s;
    }
}

} // namespace core
```

- [ ] **Step 5: Run** `pio test -e test -f test_berlin_generator` → PASS. Then `pio test -e test` → PASS.
- [ ] **Step 6: Commit** `git add core/GatePitchPhasingGenerator.h core/GatePitchPhasingGenerator.cpp test/test_berlin_generator && git commit -m "feat(berlin): Gate/Pitch Phasing generator (lcm-capped realized pattern)"`

---

### Task 4: Algorithm dispatch in BerlinMode

**Files:** Modify `core/modes/BerlinMode.{h,cpp}`; Test `test/test_berlin_mode/test_berlin_mode.cpp` (extend).

The mode holds all three generators and points the engine at the one matching `params_.algorithm` before every generate.

- [ ] **Step 1: In `core/modes/BerlinMode.h`** add includes + members:
```cpp
#include "core/DegreeWeightedGenerator.h"
#include "core/GatePitchPhasingGenerator.h"
```
Add member instances next to `walkGen_`:
```cpp
    DrunkardWalkGenerator     walkGen_;
    DegreeWeightedGenerator   degreeGen_;
    GatePitchPhasingGenerator phasingGen_;
```
Add a private helper declaration:
```cpp
    void applyGenerator();   // point the engine at the generator for params_.algorithm
```

- [ ] **Step 2: In `core/modes/BerlinMode.cpp`** implement dispatch and call it where the generator must be current. Replace the ctor's `engine_.setGenerator(&walkGen_);` with `applyGenerator();` AND add `applyGenerator()`:
```cpp
void BerlinMode::applyGenerator() {
    switch (params_.algorithm) {
        case BerlinAlgorithm::DegreeWeighted:   engine_.setGenerator(&degreeGen_);  break;
        case BerlinAlgorithm::GatePitchPhasing: engine_.setGenerator(&phasingGen_); break;
        case BerlinAlgorithm::DrunkardWalk:
        default:                                engine_.setGenerator(&walkGen_);    break;
    }
}
```
Call `applyGenerator();` in `onEnter()` (before the `engine_.generate()` guard), in `update()` (after `engine_.setParams(params_)`), and in `onRawInput()` immediately before `engine_.generate()` on the Latch3 case. For the Latch3 case, change it to:
```cpp
        case 3: if (changed) { applyGenerator(); engine_.generate(); } break;  // Reset/Generate (any flip)
```
(The ctor runs before `params_` could change, so `applyGenerator()` there selects Walk — the default. Confirm the ctor still calls `engine_.setParams(params_)`.)

- [ ] **Step 3: Add a test** `test_algorithm_dispatch_changes_generator` to `test/test_berlin_mode/test_berlin_mode.cpp`:
```cpp
static int activeCount(const core::BerlinSequence& s) {
    int n = 0; for (int i = 0; i < s.length(); ++i) if (s.step(i).active) ++n; return n;
}

static void test_algorithm_dispatch() {
    core::AppShell shell;
    core::BerlinMode berlin(shell);
    FakeMidiOutput out; berlin.setMidiOutput(&out);
    berlin.onEnter();

    // Phasing: length 8 × gateLen 6 → realized length lcm = 24 (≠ the 16 Walk uses).
    berlin.params().algorithm = core::BerlinAlgorithm::GatePitchPhasing;
    berlin.params().length = 8; berlin.params().gateLen = 6;
    berlin.onRawInput({core::RawInput::Kind::Latch, 3, 0, true});   // generate
    TEST_ASSERT_EQUAL_INT(24, berlin.engine().sequence().length());

    // Degree-Weighted: back to a length-16 sequence, all in scale.
    berlin.params().algorithm = core::BerlinAlgorithm::DegreeWeighted;
    berlin.params().length = 16;
    berlin.onRawInput({core::RawInput::Kind::Latch, 3, 0, false});  // generate (other flip)
    TEST_ASSERT_EQUAL_INT(16, berlin.engine().sequence().length());
    TEST_ASSERT_TRUE(activeCount(berlin.engine().sequence()) >= 1);
}
```
Register it in `main()`.

- [ ] **Step 4: Run** `pio test -e test -f test_berlin_mode` → PASS. Then `pio test -e test` → PASS. Also `pio run -e native` SUCCESS.
- [ ] **Step 5: Commit** `git add core/modes/BerlinMode.h core/modes/BerlinMode.cpp test/test_berlin_mode && git commit -m "feat(berlin): dispatch generator by Algorithm param (Walk/Phasing/Degree)"`

---

### Task 5: Dynamics screen + contextual cell

**Files:** Modify `core/modes/BerlinMode.{h,cpp}`; Test `test/test_berlin_mode/test_berlin_mode.cpp` (extend).

Add the Dynamics screen (Velocity / Humanize / Accent / contextual) at index 2, pushing Behavior to index 3. Screen order becomes Structure(0) / Character(1) / Dynamics(2) / Behavior(3). The contextual Enc4 cell edits **Scatter** when Algorithm = Walk and **GateLen** when Algorithm = Phasing (nothing for Degree).

- [ ] **Step 1: In `core/modes/BerlinMode.h`** add a `DynamicsScreen` nested class (mirroring the others) and a member `DynamicsScreen dynamicsScreen_{*this};`. Bump `screenCount()` to **4**.

- [ ] **Step 2: In `core/modes/BerlinMode.cpp`** update `screen(int)`:
```cpp
Screen& BerlinMode::screen(int i) {
    if (i == 1) return characterScreen_;
    if (i == 2) return dynamicsScreen_;
    if (i == 3) return behaviorScreen_;
    return structureScreen_;
}
```

- [ ] **Step 3: Implement `DynamicsScreen::onEncoder`** (clamp as shown):
  - Enc1 VelocityBase: `int v = p.velocityBase + delta; clamp 1..126`
  - Enc2 VelocityHumanize: `int v = p.velocityHumanize + delta; clamp 0..30`
  - Enc3 Accent: `int v = p.accent + delta; clamp 0..27`
  - Enc4 contextual: if `p.algorithm == DrunkardWalk` edit Scatter (`p.scatter + delta`, clamp 1..7); else if `p.algorithm == GatePitchPhasing` edit GateLen (`p.gateLen + delta`, clamp 3..16); else (Degree) no-op.

- [ ] **Step 4: Implement `DynamicsScreen::render`** — draw the 1×4 param row (`drawBerlinParamCell` cols 0..3) + `drawBerlinParamDividers(d)` + the shared piano-roll (`drawBerlinPianoRoll(d, mode_.engine().sequence(), mode_.engine().playhead(), color::Green)`). Cells: col0 "VEL" "<base>", col1 "HUMAN" "±<n>", col2 "ACCENT" "+<n>", col3 contextual — when Walk: "SCATTER" "<n>"; when Phasing: "GATELEN" "<n>"; when Degree: "-" "-". (Use `snprintf` like the other screens.)

- [ ] **Step 5: Add a test** `test_dynamics_screen_edits_and_contextual` to `test/test_berlin_mode/test_berlin_mode.cpp`:
```cpp
static void test_dynamics_screen() {
    core::AppShell shell;
    core::BerlinMode berlin(shell);
    core::Screen& dyn = berlin.screen(2);          // Dynamics now at index 2

    // Velocity base clamps 1..126
    for (int i = 0; i < 200; ++i) dyn.onEncoder(1, +1);
    TEST_ASSERT_EQUAL_INT(126, berlin.params().velocityBase);
    // Accent clamps 0..27
    for (int i = 0; i < 50; ++i) dyn.onEncoder(3, -1);
    TEST_ASSERT_EQUAL_INT(0, berlin.params().accent);

    // Contextual cell: Walk → Scatter (1..7)
    berlin.params().algorithm = core::BerlinAlgorithm::DrunkardWalk;
    for (int i = 0; i < 20; ++i) dyn.onEncoder(4, +1);
    TEST_ASSERT_EQUAL_INT(7, berlin.params().scatter);
    // Contextual cell: Phasing → GateLen (3..16)
    berlin.params().algorithm = core::BerlinAlgorithm::GatePitchPhasing;
    for (int i = 0; i < 30; ++i) dyn.onEncoder(4, +1);
    TEST_ASSERT_EQUAL_INT(16, berlin.params().gateLen);
    // Contextual cell: Degree → no-op (scatter/gateLen unchanged by Enc4)
    berlin.params().algorithm = core::BerlinAlgorithm::DegreeWeighted;
    uint8_t scBefore = berlin.params().scatter, glBefore = berlin.params().gateLen;
    for (int i = 0; i < 5; ++i) dyn.onEncoder(4, +1);
    TEST_ASSERT_EQUAL_UINT8(scBefore, berlin.params().scatter);
    TEST_ASSERT_EQUAL_UINT8(glBefore, berlin.params().gateLen);
}
```
Also verify the Behavior screen moved: it is now `berlin.screen(3)` — update any existing test that referenced `berlin.screen(2)` for Behavior to `screen(3)` (the Task 6 `test_character_behavior_screens_edit_clamp` uses `screen(2)` for Behavior — change that to `screen(3)`). Register the new test in `main()`.

- [ ] **Step 6: Run** `pio test -e test -f test_berlin_mode` → PASS. Then `pio test -e test` → PASS.
- [ ] **Step 7: Commit** `git add core/modes/BerlinMode.h core/modes/BerlinMode.cpp test/test_berlin_mode && git commit -m "feat(berlin): Dynamics screen (velocity/humanize/accent) + contextual Scatter/GateLen cell"`

---

### Task 6: Build both platforms + verify

**Files:** none expected (the mode is already registered from Plan A).

- [ ] **Step 1: Build** `pio run -e teensy41` SUCCESS, `pio run -e native` SUCCESS, `pio test -e test` PASS. Confirm WARNING-CLEAN (watch `-Wmisleading-indentation`).
- [ ] **Step 2 (controller/human): simulator verification.** `make sim` → Enc5 to **Berlin**. Confirm:
  - **Algorithm** (Structure Enc1) now changes the generated character: **Walk** = meandering contour, **Phase** = evolving phased pattern (its length changes — lcm of Length×GateLen), **Degree** = root/fifth-weighted scatter. Each `Generate` (Latch3) reflects the selected algorithm.
  - **Dynamics screen** (3rd screen): Velocity / Humanize / Accent change the played velocities; the **contextual cell** shows Scatter under Walk and GateLen under Phasing, and editing it changes the next generation.
  - Tension (Character) noticeably shifts how "safe" (root/fifth) vs "tense" the Degree/Phasing pitches are.
  - The piano-roll still persists across all four screens; transport unchanged.

---

## Self-review notes

- **Spec coverage (Plan B):** Gate/Pitch Phasing §4.2 → Task 3; Degree-Weighted §4.3 → Task 2; shared degree-weight/interval tables §2.1 → Task 1 (`intervalWeight`); contextual Scatter/GateLen cell + Dynamics screen §7 → Task 5; algorithm becomes functional → Task 4.
- **Engine unchanged:** per-step velocity/accent already flow through `BerlinEngine::emitStep` (Plan A); the Dynamics params reach playback via the generators, so no engine edit is needed.
- **Phasing capacity:** realized length is `min(lcm(P,G), kMaxSteps=32)`. Pairs whose lcm > 32 (e.g. 16×15=240) loop a 32-step window — a long evolving pattern, not a perfect mathematical realignment. This is an explicit v1 simplification (documented in the spec §4.2); raising `kMaxSteps` or on-the-fly computation is a later refinement.
- **DRY refactor risk:** Task 1 rewrites Drunkard's Walk to use the shared helpers, preserving the rng call order; the 5 existing generator tests (in-scale, register-across-roots, density, determinism, scatter) guard against regressions.
- **Screen index shift:** Dynamics inserts at index 2, moving Behavior to 3 — Task 5 updates the one existing test that addressed Behavior via `screen(2)`.
- **Type consistency:** `berlin*` helpers (Task 1) used by all three generators (Tasks 1–3); `BerlinAlgorithm` dispatch (Task 4) selects among `walkGen_`/`degreeGen_`/`phasingGen_`; `params.gateLen`/`scatter` edited by the contextual cell (Task 5) consumed by Phasing/Walk (Tasks 1,3). `Scale::degreeCount()` (Task 1) used by `berlinDegreeWeightedNote`.
