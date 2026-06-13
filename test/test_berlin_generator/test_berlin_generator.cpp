#include <unity.h>
#include <cstdlib>
#include <initializer_list>

#include "core/DrunkardWalkGenerator.h"
#include "core/DegreeWeightedGenerator.h"
#include "core/GatePitchPhasingGenerator.h"
#include "core/BassAnchorGenerator.h"
#include "core/BerlinGen.h"
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

static void test_degree_weighted_note_in_scale_and_register() {
    for (int root = 0; root < 12; ++root) {
        for (core::Scale::Type t : {core::Scale::Type::Minor, core::Scale::Type::PentaMinor}) {
            core::Scale scale(t, static_cast<uint8_t>(root));
            core::BerlinParams p = baseParams(); p.octaveBase = 48; p.octaveRange = 2; p.tension = 30;
            const int lo = p.octaveBase, hi = p.octaveBase + 12 * p.octaveRange;
            const uint8_t baseRoot = core::berlinBaseRoot(scale, p);
            core::BerlinRng rng; rng.seed(static_cast<uint32_t>(root * 13 + 1));
            for (int k = 0; k < 200; ++k) {
                uint8_t n = core::berlinDegreeWeightedNote(scale, baseRoot, p, rng);
                TEST_ASSERT_TRUE(scale.contains(n));
                TEST_ASSERT_TRUE(n >= lo && n <= hi);
            }
        }
    }
}

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
    // density 100 → every gate open → every realized step active and
    // note[i] == pitch[i % P]. Verify the pitch list repeats every P.
    core::GatePitchPhasingGenerator gen;
    core::Scale scale(core::Scale::Type::Minor, 0);
    core::BerlinParams p = baseParams(); p.length = 5; p.gateLen = 8; p.density = 100;
    core::BerlinSequence seq; core::BerlinRng rng; rng.seed(2);
    gen.generate(seq, p, scale, rng);
    const int P = 5;
    for (int i = P; i < seq.length(); ++i)
        TEST_ASSERT_EQUAL_UINT8(seq.step(i % P).note, seq.step(i).note);
}

// Length now goes up to 32 (the full BerlinSequence capacity): a 32-step Walk
// materializes all 32 steps in scale, and Phasing's pitch list follows Length
// (P=32 with G=6 → lcm 96 capped at 32, pitch[i % 32] = the list itself).
static void test_length_32_supported() {
    core::Scale scale(core::Scale::Type::Minor, 0);

    core::DrunkardWalkGenerator walk;
    core::BerlinParams p = baseParams(); p.length = 32; p.density = 100;
    core::BerlinSequence seq; core::BerlinRng rng; rng.seed(13);
    walk.generate(seq, p, scale, rng);
    TEST_ASSERT_EQUAL_INT(32, seq.length());
    for (int i = 0; i < seq.length(); ++i)
        if (seq.step(i).active) TEST_ASSERT_TRUE(scale.contains(seq.step(i).note));

    core::GatePitchPhasingGenerator phase;
    core::BerlinParams q = baseParams(); q.length = 32; q.gateLen = 6; q.density = 100;
    core::BerlinSequence seq2; core::BerlinRng rng2; rng2.seed(13);
    phase.generate(seq2, q, scale, rng2);
    TEST_ASSERT_EQUAL_INT(32, seq2.length());          // lcm(32,6)=96 → capped 32
    for (int i = 0; i < seq2.length(); ++i)
        if (seq2.step(i).active) TEST_ASSERT_TRUE(scale.contains(seq2.step(i).note));
}

// Spec §4.1: the Walk biases its wander by degree-consonance weights, spread
// by Tension. Low tension must gravitate to root/fifth pitch classes more
// than high tension does (summed over a few seeds to be robust).
static int rootFifthCount(const core::BerlinSequence& s, const core::Scale& sc) {
    int n = 0;
    for (int i = 0; i < s.length(); ++i) {
        if (!s.step(i).active) continue;
        const int pc = ((s.step(i).note % 12) - sc.root() + 24) % 12;
        if (pc == 0 || pc == 7) ++n;
    }
    return n;
}

static void test_walk_tension_biases_toward_root_fifth() {
    core::DrunkardWalkGenerator gen;
    core::Scale scale(core::Scale::Type::Minor, 0);
    int lowT = 0;
    int highT = 0;
    for (uint32_t seed = 1; seed <= 4; ++seed) {
        core::BerlinParams p = baseParams();
        p.length = 32; p.density = 100; p.scatter = 7; p.octaveRange = 2;
        core::BerlinSequence a, b;
        core::BerlinRng r1, r2; r1.seed(seed); r2.seed(seed);
        p.tension = 0;   gen.generate(a, p, scale, r1);
        p.tension = 100; gen.generate(b, p, scale, r2);
        lowT  += rootFifthCount(a, scale);
        highT += rootFifthCount(b, scale);
    }
    TEST_ASSERT_GREATER_THAN(highT, lowT);   // low tension → more root/fifth hits
}

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

// Spec §2.4 step 3: simultaneously sounding notes at interval class 1/6/11
// get the HIGHER note moved to the nearest in-scale tone that clears the
// clash. High tension (>60) keeps the grit: nothing moves.
static void test_consonance_check_moves_clashing_high_note() {
    core::Scale scale(core::Scale::Type::Minor, 0);       // C minor
    core::BerlinSequence a, b;
    a.setLength(4); b.setLength(4);
    for (int i = 0; i < 4; ++i) {
        a.step(i).active = true; a.step(i).note = 48;      // C3 root, all steps
        b.step(i).active = false;
    }
    b.step(0).active = true; b.step(0).note = 53;          // F3 vs C3: consonant
    b.step(1).active = true; b.step(1).note = 49;          // Db3 vs C3: interval
                                                           // class 1 = clash
    core::BerlinSequence* seqs[2] = {&a, &b};
    core::berlinEnforceConsonance(seqs, 2, scale, /*tension=*/30);

    TEST_ASSERT_EQUAL_INT(53, b.step(0).note);             // consonant: untouched
    // The clashing Db moved to an in-scale tone that no longer clashes with C.
    TEST_ASSERT_TRUE(scale.contains(b.step(1).note));
    int ic = (b.step(1).note - 48) % 12; if (ic < 0) ic += 12;
    TEST_ASSERT_TRUE(ic != 1 && ic != 6 && ic != 11);
}

static void test_consonance_check_skipped_at_high_tension() {
    core::Scale scale(core::Scale::Type::Minor, 0);
    core::BerlinSequence a, b;
    a.setLength(2); b.setLength(2);
    a.step(0).active = true; a.step(0).note = 48;
    b.step(0).active = true; b.step(0).note = 49;          // clash
    core::BerlinSequence* seqs[2] = {&a, &b};
    core::berlinEnforceConsonance(seqs, 2, scale, /*tension=*/80);
    TEST_ASSERT_EQUAL_INT(49, b.step(0).note);             // untouched
}

// Phasing alignment: voices of different lengths clash where columns
// coincide MOD length — a 2-step voice against a 3-step voice meets the
// clash at columns 1, 3, 5... the fix clears them all in one pass.
static void test_consonance_check_respects_phasing_alignment() {
    core::Scale scale(core::Scale::Type::Minor, 0);
    core::BerlinSequence a, b;
    a.setLength(3);
    a.step(1).active = true; a.step(1).note = 48;          // C on column 1 (mod 3)
    b.setLength(2);
    b.step(1).active = true; b.step(1).note = 49;          // Db on column 1 (mod 2)
    core::BerlinSequence* seqs[2] = {&a, &b};
    core::berlinEnforceConsonance(seqs, 2, scale, 30);
    int ic = (b.step(1).note - 48) % 12; if (ic < 0) ic += 12;
    TEST_ASSERT_TRUE(ic != 1 && ic != 6 && ic != 11);
}

// Dorian / Phrygian / Harmonic minor: verify a couple of in-scale and
// out-of-scale pitch classes (root C = 0) and that each has 7 degrees.
static void test_new_scales_intervals() {
    core::Scale dor(core::Scale::Type::Dorian, 0);       // 0 2 3 5 7 9 10
    TEST_ASSERT_EQUAL_INT(7, dor.degreeCount());
    TEST_ASSERT_TRUE(dor.contains(60 + 3));   // Eb in C dorian
    TEST_ASSERT_TRUE(dor.contains(60 + 9));   // A in C dorian
    TEST_ASSERT_FALSE(dor.contains(60 + 4));  // E natural not in dorian

    core::Scale phr(core::Scale::Type::Phrygian, 0);     // 0 1 3 5 7 8 10
    TEST_ASSERT_EQUAL_INT(7, phr.degreeCount());
    TEST_ASSERT_TRUE(phr.contains(60 + 1));   // Db (flat 2) in C phrygian
    TEST_ASSERT_FALSE(phr.contains(60 + 2));  // D natural not in phrygian

    core::Scale hm(core::Scale::Type::HarmonicMinor, 0); // 0 2 3 5 7 8 11
    TEST_ASSERT_EQUAL_INT(7, hm.degreeCount());
    TEST_ASSERT_TRUE(hm.contains(60 + 11));   // B (leading tone) in C harmonic minor
    TEST_ASSERT_TRUE(hm.contains(60 + 8));    // Ab in C harmonic minor
    TEST_ASSERT_FALSE(hm.contains(60 + 10));  // Bb not in harmonic minor
}

// degreeIndex: +1 per scale step, +degreeCount() per octave, signed across
// the root; consistent with degreeNote (its inverse over a degree delta).
static void test_scale_degree_index() {
    core::Scale cmaj(core::Scale::Type::Major, 0);   // C major, 7 notes
    const int n = cmaj.degreeCount();                // 7
    // One scale step up (C->D = 60->62) is +1.
    TEST_ASSERT_EQUAL_INT(1, cmaj.degreeIndex(62) - cmaj.degreeIndex(60));
    // One octave up (60->72) is +degreeCount.
    TEST_ASSERT_EQUAL_INT(n, cmaj.degreeIndex(72) - cmaj.degreeIndex(60));
    // One octave down is -degreeCount.
    TEST_ASSERT_EQUAL_INT(-n, cmaj.degreeIndex(48) - cmaj.degreeIndex(60));
    // Round-trip: stepping degreeNote by the index delta lands on the note.
    const int d = cmaj.degreeIndex(67) - cmaj.degreeIndex(60);   // C->G = +4
    TEST_ASSERT_EQUAL_INT(67, cmaj.degreeNote(60, d));
    // Non-C root stays consistent (A minor: A->B is +1).
    core::Scale amin(core::Scale::Type::Minor, 9);   // root A (pc 9)
    TEST_ASSERT_EQUAL_INT(1, amin.degreeIndex(71) - amin.degreeIndex(69));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_walk_starts_on_root_and_stays_in_scale);
    RUN_TEST(test_density_controls_active_count);
    RUN_TEST(test_walk_respects_scatter_and_register);
    RUN_TEST(test_walk_register_holds_for_all_roots);
    RUN_TEST(test_walk_is_deterministic_for_seed);
    RUN_TEST(test_degree_weighted_note_in_scale_and_register);
    RUN_TEST(test_degree_generator_in_scale_root_anchored);
    RUN_TEST(test_degree_generator_density_and_determinism);
    RUN_TEST(test_phasing_length_is_capped_lcm);
    RUN_TEST(test_phasing_in_scale_and_capped);
    RUN_TEST(test_phasing_repeats_pitch_by_period);
    RUN_TEST(test_length_32_supported);
    RUN_TEST(test_walk_tension_biases_toward_root_fifth);
    RUN_TEST(test_bass_anchor_generator_is_root_heavy);
    RUN_TEST(test_consonance_check_moves_clashing_high_note);
    RUN_TEST(test_consonance_check_skipped_at_high_tension);
    RUN_TEST(test_consonance_check_respects_phasing_alignment);
    RUN_TEST(test_new_scales_intervals);
    RUN_TEST(test_scale_degree_index);
    return UNITY_END();
}
