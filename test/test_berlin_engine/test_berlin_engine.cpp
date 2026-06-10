#include <unity.h>
#include "core/BerlinTypes.h"
#include "core/BerlinRng.h"
#include "core/BerlinSequence.h"
#include "core/BerlinEngine.h"
#include "core/DrunkardWalkGenerator.h"
#include "core/Scale.h"
#include "core/render/BerlinLayout.h"
#include "support/FakeMidiOutput.h"

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

static int countOn (const FakeMidiOutput& o) { int n=0; for (auto&e:o.events) if (e.isOn) ++n; return n; }
static int countOff(const FakeMidiOutput& o) { int n=0; for (auto&e:o.events) if (!e.isOn) ++n; return n; }

static void seedTwoStep(core::BerlinEngine& e) {
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
    TEST_ASSERT_EQUAL_INT(1, countOn(out));
    TEST_ASSERT_EQUAL_UINT8(60, out.events[0].note);
    TEST_ASSERT_TRUE(e.isPlaying());
}

static void test_gate_closes_after_gate_ticks() {
    core::BerlinEngine e; FakeMidiOutput out; e.setOutput(&out);
    seedTwoStep(e); e.play();
    for (int i = 0; i < 6; ++i) e.onClockTick();
    TEST_ASSERT_EQUAL_INT(1, countOff(out));
    TEST_ASSERT_EQUAL_UINT8(60, out.events[1].note);
}

static void test_step_advances_at_resolution_boundary() {
    core::BerlinEngine e; FakeMidiOutput out; e.setOutput(&out);
    seedTwoStep(e); e.play();
    for (int i = 0; i < 12; ++i) e.onClockTick();
    TEST_ASSERT_EQUAL_INT(1, e.playhead());
    TEST_ASSERT_EQUAL_INT(2, countOn(out));
}

static void test_loops_back_to_step_0() {
    core::BerlinEngine e; FakeMidiOutput out; e.setOutput(&out);
    seedTwoStep(e); e.play();
    for (int i = 0; i < 24; ++i) e.onClockTick();
    TEST_ASSERT_EQUAL_INT(0, e.playhead());
}

static void test_pause_holds_and_stop_rewinds() {
    core::BerlinEngine e; FakeMidiOutput out; e.setOutput(&out);
    seedTwoStep(e); e.play();
    for (int i = 0; i < 12; ++i) e.onClockTick();      // at step 1
    e.pause();
    const int onBefore = countOn(out);
    for (int i = 0; i < 24; ++i) e.onClockTick();      // paused → no advance
    TEST_ASSERT_EQUAL_INT(1, e.playhead());
    TEST_ASSERT_EQUAL_INT(onBefore, countOn(out));
    e.stop();
    TEST_ASSERT_EQUAL_INT(0, e.playhead());
    TEST_ASSERT_FALSE(e.isPlaying());
    TEST_ASSERT_TRUE(countOff(out) >= 1);
}

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
    TEST_ASSERT_TRUE(e.sequence().step(0).active);
    TEST_ASSERT_EQUAL_INT(0, e.sequence().step(0).note % 12);
    TEST_ASSERT_EQUAL_INT(0, e.playhead());
}

static void test_morph_0_keeps_base_100_replaces() {
    core::BerlinEngine e; core::DrunkardWalkGenerator gen; core::Scale sc(core::Scale::Type::Minor, 0);
    e.setGenerator(&gen); e.setScale(&sc); e.seed(3);
    core::BerlinParams p; p.length = 16; p.density = 60; p.morph = 100; e.setParams(p);
    e.generate();
    core::BerlinSequence base = e.sequence();

    p.morph = 0; e.setParams(p); e.generate();
    TEST_ASSERT_TRUE(seqEqual(base, e.sequence()));        // morph 0 → identical

    p.morph = 100; e.setParams(p); e.generate();
    TEST_ASSERT_TRUE(countActiveDiff(base, e.sequence()) > 0);  // morph 100 → differs
}

static void test_roll_range_min_two_octaves_and_contains_notes() {
    core::BerlinSequence s; s.clear(); s.setLength(3);
    s.step(0) = {true, 60, 100, false, 6};   // C4
    s.step(1) = {true, 64, 100, false, 6};   // E4
    s.step(2) = {true, 67, 100, false, 6};   // G4
    int lo = 0, hi = 0;
    core::berlinRollRange(s, lo, hi);
    TEST_ASSERT_TRUE(hi - lo >= 23);                 // at least 2 octaves
    TEST_ASSERT_EQUAL_INT(0, lo % 12);               // snapped to a C
    TEST_ASSERT_EQUAL_INT(11, hi % 12);              // snapped to a B
    TEST_ASSERT_TRUE(lo <= 60 && hi >= 67);          // contains all active notes

    // Wide span (>2 octaves) is preserved.
    core::BerlinSequence w; w.clear(); w.setLength(2);
    w.step(0) = {true, 36, 100, false, 6};   // C2
    w.step(1) = {true, 84, 100, false, 6};   // C6
    core::berlinRollRange(w, lo, hi);
    TEST_ASSERT_TRUE(lo <= 36 && hi >= 84);
    TEST_ASSERT_TRUE(hi - lo >= 23);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_resolution_ticks);
    RUN_TEST(test_rng_is_deterministic_for_seed);
    RUN_TEST(test_sequence_defaults_and_clamp);
    RUN_TEST(test_play_emits_first_step_immediately);
    RUN_TEST(test_gate_closes_after_gate_ticks);
    RUN_TEST(test_step_advances_at_resolution_boundary);
    RUN_TEST(test_loops_back_to_step_0);
    RUN_TEST(test_pause_holds_and_stop_rewinds);
    RUN_TEST(test_generate_fills_via_generator);
    RUN_TEST(test_morph_0_keeps_base_100_replaces);
    RUN_TEST(test_roll_range_min_two_octaves_and_contains_notes);
    return UNITY_END();
}
