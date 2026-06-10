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
