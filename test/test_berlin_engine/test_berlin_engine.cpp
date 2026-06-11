#include <unity.h>
#include "core/BerlinTypes.h"
#include "core/BerlinRng.h"
#include "core/BerlinSequence.h"
#include "core/BerlinEngine.h"
#include "core/DrunkardWalkGenerator.h"
#include "core/GatePitchPhasingGenerator.h"
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

// Like countActiveDiff but also counts a velocity change. The Evolve test uses
// this so a re-rolled step is detected even if its note coincidentally matches
// the base (a fresh candidate step always re-draws a humanized velocity), making
// the test independent of the RNG seed.
static int stepDiff(const core::BerlinSequence& a, const core::BerlinSequence& b) {
    int d = 0; int n = a.length() < b.length() ? a.length() : b.length();
    for (int i = 0; i < n; ++i)
        if (a.step(i).active   != b.step(i).active ||
            a.step(i).note     != b.step(i).note   ||
            a.step(i).velocity != b.step(i).velocity) ++d;
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
    const int ticksPerLoop = 4 * 6;
    clocksB(e, ticksPerLoop);
    TEST_ASSERT_EQUAL_INT(1, e.loopCount());
    // stepDiff counts velocity changes too, so this holds for any seed: exactly
    // the 1-2 spliced steps differ (a fresh candidate step re-draws velocity).
    const int diff = stepDiff(base, e.sequence());
    TEST_ASSERT_TRUE(diff >= 1 && diff <= 2);
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
    clocksB(e, 4 * 6 * 5);
    TEST_ASSERT_EQUAL_INT(0, countActiveDiff(base, e.sequence()));
}

// N1: emitStep must kill a still-sounding note before starting a new one.
// Repro: gateTicks baked into the sequence are LARGER than the live step length
// (sequence generated at a coarser resolution, then resolution switched finer).
// At each step boundary the previous note is still inside its (over-long) gate;
// emitStep must send its NoteOff or notes accumulate as stuck.
static void test_emitstep_kills_overlong_gate_note() {
    core::BerlinEngine e; FakeMidiOutput out; e.setOutput(&out);
    core::BerlinSequence& s = e.sequenceMut();
    s.clear(); s.setLength(2);
    // gateTicks=11 > stepLen=6 (Sixteenth): the gate never closes before the
    // next step boundary, so emitStep is the only place the NoteOff can come from.
    s.step(0) = {true, 60, 100, false, 11};
    s.step(1) = {true, 67, 100, false, 11};
    core::BerlinParams p; p.resolution = core::BerlinResolution::Sixteenth; p.length = 2;
    e.setParams(p);
    e.play();
    // Drive ~4 step boundaries (24 ticks at 6 ticks/step). At every boundary the
    // count of NoteOffs must keep up with NoteOns (no permanently stuck note).
    for (int i = 0; i < 24; ++i) {
        e.onClockTick();
        const int ons  = countOn(out);
        const int offs = countOff(out);
        TEST_ASSERT_TRUE_MESSAGE(offs >= ons - 1, "stuck note: NoteOffs fell behind NoteOns");
    }
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

// Morph 1-99: partial replacement — at least one step re-rolled (differs from
// the base) and at least one step kept (identical note+active+velocity).
// Density=100 so every step is active; length=16 gives enough steps that even
// at morph=50 both "some differ" and "some identical" hold with overwhelming
// probability for any reasonable seed.
static void test_morph_mid_range_partial_replace() {
    core::BerlinEngine e; core::DrunkardWalkGenerator gen; core::Scale sc(core::Scale::Type::Minor, 0);
    e.setGenerator(&gen); e.setScale(&sc); e.seed(42);
    core::BerlinParams p;
    p.length = 16; p.density = 100; p.morph = 100; p.resolution = core::BerlinResolution::Sixteenth;
    e.setParams(p);
    e.generate();
    core::BerlinSequence base = e.sequence();
    TEST_ASSERT_EQUAL_INT(16, base.length());

    // Now generate with morph=50: some steps should be re-rolled, some kept.
    p.morph = 50; e.setParams(p); e.generate();
    const core::BerlinSequence& result = e.sequence();
    TEST_ASSERT_EQUAL_INT(16, result.length());

    // (a) At least 1 step differs (something re-rolled).
    int diff = 0, same = 0;
    for (int i = 0; i < 16; ++i) {
        const bool stepSame = base.step(i).active   == result.step(i).active &&
                              base.step(i).note     == result.step(i).note   &&
                              base.step(i).velocity == result.step(i).velocity;
        if (stepSame) ++same; else ++diff;
    }
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, diff, "morph=50 must re-roll at least 1 step");
    // (b) At least 1 step kept (not a full replacement).
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, same, "morph=50 must keep at least 1 step identical");
}

// Morph length-change mid: base at length=16, then change params to length=8
// with morph=50. The engine must adopt the candidate's length (8) even though
// a partial merge is in effect.
static void test_morph_length_change_mid() {
    core::BerlinEngine e; core::DrunkardWalkGenerator gen; core::Scale sc(core::Scale::Type::Minor, 0);
    e.setGenerator(&gen); e.setScale(&sc); e.seed(42);
    core::BerlinParams p;
    p.length = 16; p.density = 100; p.morph = 100; p.resolution = core::BerlinResolution::Sixteenth;
    e.setParams(p);
    e.generate();
    TEST_ASSERT_EQUAL_INT(16, e.sequence().length());

    // Shorten to length=8 with partial morph — candidate length = 8.
    p.length = 8; p.morph = 50; e.setParams(p); e.generate();
    TEST_ASSERT_EQUAL_INT_MESSAGE(8, e.sequence().length(),
        "engine must adopt the candidate length even under partial morph");
}

// Evolve on a Phasing sequence: GatePitchPhasingGenerator with length=8,
// gateLen=6 realizes lcm(8,6)=24 steps. After one full loop (24 × 6 ticks =
// 144 ticks), the Evolve splice must have run exactly once in-bounds on the
// 24-step sequence: loopCount==1, sequence still length 24, and 1-2 steps
// differ compared to the original (stepDiff counts velocity too, so a re-drawn
// step is detected even if its note coincidentally matches).
static void test_evolve_on_phasing_sequence() {
    core::BerlinEngine e; core::GatePitchPhasingGenerator gen; core::Scale sc(core::Scale::Type::Minor, 0);
    e.setGenerator(&gen); e.setScale(&sc); e.seed(55);
    core::BerlinParams p;
    p.length = 8; p.gateLen = 6; p.density = 100;
    p.resolution = core::BerlinResolution::Sixteenth;  // 6 ticks/step
    p.behavior = core::BerlinBehavior::Evolve; p.evolveRate = 1;
    e.setParams(p);
    e.generate();

    // GatePitchPhasing with P=8, G=6 → lcm=24 realized steps.
    TEST_ASSERT_EQUAL_INT_MESSAGE(24, e.sequence().length(),
        "phasing lcm(8,6) must yield a 24-step sequence");
    core::BerlinSequence base = e.sequence();

    e.play();
    // One full loop = 24 steps × 6 ticks/step = 144 ticks.
    const int ticksPerLoop = 24 * 6;
    for (int i = 0; i < ticksPerLoop; ++i) e.onClockTick();

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, e.loopCount(), "exactly one loop must have completed");
    TEST_ASSERT_EQUAL_INT_MESSAGE(24, e.sequence().length(), "sequence length must remain 24 after evolve");

    // The splice touched 1 or 2 steps — verify the diff is in that range.
    const int diff = stepDiff(base, e.sequence());
    TEST_ASSERT_TRUE_MESSAGE(diff >= 1 && diff <= 2,
        "evolve on phasing sequence must splice exactly 1-2 steps");
}

// silence() flushes a sounding note (NoteOff) without moving the playhead or
// changing the playing state — used by external transport Pause / Stop safety.
// Gate is a LIVE performance parameter: the engine derives the gate length at
// each step from the current params (stepLen * gatePercent / 100), not from
// the gateTicks baked into the sequence at generation time. Changing the Gate
// knob mid-playback therefore audibly shortens/lengthens notes immediately.
static void test_gate_param_applies_live() {
    core::BerlinEngine e; FakeMidiOutput out; e.setOutput(&out);
    seedTwoStep(e);                                   // Eighth → stepLen 12 ticks
    core::BerlinParams p; p.resolution = core::BerlinResolution::Eighth; p.length = 2;
    p.gatePercent = 50;                               // gate = 12*50/100 = 6 ticks
    e.setParams(p);
    e.play();                                         // NoteOn(60), gate armed

    for (int i = 0; i < 6; ++i) e.onClockTick();
    TEST_ASSERT_EQUAL_INT(1, countOff(out));          // 50% gate closed at tick 6

    p.gatePercent = 90;                               // live change: gate = 10 ticks
    e.setParams(p);
    for (int i = 0; i < 6; ++i) e.onClockTick();      // tick 12: step 1 NoteOn(67)
    TEST_ASSERT_EQUAL_INT(2, countOn(out));
    TEST_ASSERT_EQUAL_INT(1, countOff(out));

    for (int i = 0; i < 9; ++i) e.onClockTick();      // tick 21: 90% gate still open
    TEST_ASSERT_EQUAL_INT(1, countOff(out));
    e.onClockTick();                                  // tick 22: gate (10) elapses
    TEST_ASSERT_EQUAL_INT(2, countOff(out));
}

static void test_silence_flushes_note_without_moving_playhead() {
    core::BerlinEngine e; FakeMidiOutput out; e.setOutput(&out);
    seedTwoStep(e); e.play();                          // step 0 fires NoteOn
    for (int i = 0; i < 12; ++i) e.onClockTick();      // advance to step 1 (still playing)
    const int playheadBefore = e.playhead();
    const bool playingBefore = e.isPlaying();
    const int offBefore = countOff(out);

    e.silence();
    TEST_ASSERT_EQUAL_INT(offBefore + 1, countOff(out));    // a NoteOff arrived
    TEST_ASSERT_EQUAL_INT(playheadBefore, e.playhead());    // playhead unchanged
    TEST_ASSERT_EQUAL_INT(static_cast<int>(playingBefore),
                          static_cast<int>(e.isPlaying())); // playing unchanged
    TEST_ASSERT_EQUAL_INT(-1, e.soundingNote());            // gate cleared

    e.silence();                                            // no note → no extra NoteOff
    TEST_ASSERT_EQUAL_INT(offBefore + 1, countOff(out));
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
    RUN_TEST(test_evolve_varies_a_few_steps_each_n_loops);
    RUN_TEST(test_locked_never_auto_varies);
    RUN_TEST(test_emitstep_kills_overlong_gate_note);
    RUN_TEST(test_roll_range_min_two_octaves_and_contains_notes);
    RUN_TEST(test_morph_mid_range_partial_replace);
    RUN_TEST(test_morph_length_change_mid);
    RUN_TEST(test_evolve_on_phasing_sequence);
    RUN_TEST(test_gate_param_applies_live);
    RUN_TEST(test_silence_flushes_note_without_moving_playhead);
    return UNITY_END();
}
