#include <unity.h>
#include <cstdlib>
#include <initializer_list>

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
    TEST_ASSERT_TRUE(seq.step(0).active);
    TEST_ASSERT_EQUAL_INT(0, seq.step(0).note % 12);   // C
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
    TEST_ASSERT_EQUAL_INT(16, active1);

    core::BerlinParams none = baseParams(); none.density = 0;
    core::BerlinSequence s2; rng.seed(1); gen.generate(s2, none, scale, rng);
    int active2 = 0; for (int i = 0; i < s2.length(); ++i) if (s2.step(i).active) ++active2;
    TEST_ASSERT_EQUAL_INT(1, active2);                 // only step 0 (root anchor)
}

static void test_walk_respects_scatter_and_register() {
    core::DrunkardWalkGenerator gen;
    core::Scale scale(core::Scale::Type::Minor, 0);
    core::BerlinParams p = baseParams(); p.scatter = 2; p.octaveRange = 2;
    core::BerlinSequence seq; core::BerlinRng rng; rng.seed(99);
    gen.generate(seq, p, scale, rng);

    const int lo = p.octaveBase;
    const int hi = p.octaveBase + 12 * p.octaveRange;
    int prev = -1;
    for (int i = 0; i < seq.length(); ++i) {
        if (!seq.step(i).active) continue;
        int n = seq.step(i).note;
        TEST_ASSERT_TRUE(n >= lo && n <= hi);
        if (prev >= 0) TEST_ASSERT_TRUE(abs(n - prev) <= 2 + 2);  // ≤ scatter + a scale-quantize step
        prev = n;
    }
}

static void test_walk_register_holds_for_all_roots() {
    core::DrunkardWalkGenerator gen;
    for (int root = 0; root < 12; ++root) {
        for (core::Scale::Type t : {core::Scale::Type::Minor, core::Scale::Type::PentaMajor}) {
            core::Scale scale(t, static_cast<uint8_t>(root));
            core::BerlinParams p = baseParams(); p.octaveBase = 48; p.octaveRange = 1; p.scatter = 5;
            const int lo = p.octaveBase, hi = p.octaveBase + 12 * p.octaveRange;
            core::BerlinSequence seq; core::BerlinRng rng; rng.seed(static_cast<uint32_t>(root * 7 + 3));
            gen.generate(seq, p, scale, rng);
            for (int i = 0; i < seq.length(); ++i) {
                if (!seq.step(i).active) continue;
                int n = seq.step(i).note;
                TEST_ASSERT_TRUE(n >= lo && n <= hi);
                TEST_ASSERT_TRUE(scale.contains(static_cast<uint8_t>(n)));
            }
        }
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
    RUN_TEST(test_walk_register_holds_for_all_roots);
    RUN_TEST(test_walk_is_deterministic_for_seed);
    return UNITY_END();
}
