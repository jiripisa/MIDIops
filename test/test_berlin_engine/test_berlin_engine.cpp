#include <unity.h>
#include "core/BerlinTypes.h"
#include "core/BerlinRng.h"
#include "core/BerlinSequence.h"
#include "core/BerlinEngine.h"
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
    return UNITY_END();
}
