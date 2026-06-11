#include <unity.h>

#include "core/app/AppShell.h"
#include "core/modes/BerlinMode.h"
#include "support/FakeMidiOutput.h"

void setUp()    {}
void tearDown() {}

// ---------------------------------------------------------------------------
// Latch transport: Latch1 = Play/Pause (level), Latch2 = Stop (rewind, edge).
// ---------------------------------------------------------------------------
static void test_latch1_play_pause_latch2_stop() {
    core::AppShell shell;
    core::BerlinMode berlin(shell);
    FakeMidiOutput out; berlin.setMidiOutput(&out);
    berlin.onEnter();
    TEST_ASSERT_FALSE(berlin.engine().isPlaying());

    berlin.onRawInput({core::RawInput::Kind::Latch, 1, 0, true});   // play
    TEST_ASSERT_TRUE(berlin.engine().isPlaying());
    berlin.onRawInput({core::RawInput::Kind::Latch, 1, 0, true});   // same level → still playing
    TEST_ASSERT_TRUE(berlin.engine().isPlaying());
    berlin.onRawInput({core::RawInput::Kind::Latch, 1, 0, false});  // pause
    TEST_ASSERT_FALSE(berlin.engine().isPlaying());

    berlin.onRawInput({core::RawInput::Kind::Latch, 2, 0, true});   // stop → rewind
    TEST_ASSERT_EQUAL_INT(0, berlin.engine().playhead());
}

// ---------------------------------------------------------------------------
// Latch2/Latch3 are CHANGE-detected: the shell delivers the latch level every
// main-loop frame, so a held-ON latch must NOT re-trigger per frame. We prove
// this on Latch2 (Stop) by advancing the playhead, then holding Latch2 ON
// across many frames: the first ON rewinds, and subsequent (still-ON) frames
// are no-ops — playhead stays at 0 and the engine remains stopped.
// ---------------------------------------------------------------------------
static void test_held_latch_edge_detect() {
    core::AppShell shell;
    core::BerlinMode berlin(shell);
    FakeMidiOutput out; berlin.setMidiOutput(&out);
    berlin.onEnter();

    // Run a few ticks so the playhead moves off step 0.
    berlin.onRawInput({core::RawInput::Kind::Latch, 1, 0, true});   // play
    for (int i = 0; i < 24; ++i) berlin.onClockTick();
    TEST_ASSERT_TRUE(berlin.engine().playhead() > 0 || berlin.engine().isPlaying());

    // Hold Latch2 (Stop) ON across many frames — only the rising edge acts.
    for (int i = 0; i < 10; ++i)
        berlin.onRawInput({core::RawInput::Kind::Latch, 2, 0, true});
    TEST_ASSERT_FALSE(berlin.engine().isPlaying());
    TEST_ASSERT_EQUAL_INT(0, berlin.engine().playhead());

    // Releasing then re-asserting Latch2 is a fresh edge (still a no-op on an
    // already-stopped engine, but must not throw / advance).
    berlin.onRawInput({core::RawInput::Kind::Latch, 2, 0, false});
    berlin.onRawInput({core::RawInput::Kind::Latch, 2, 0, true});
    TEST_ASSERT_EQUAL_INT(0, berlin.engine().playhead());
}

// ---------------------------------------------------------------------------
// Structure screen encoder edits + clamping.
// ---------------------------------------------------------------------------
static void test_structure_screen_edits_and_clamps() {
    core::AppShell shell;
    core::BerlinMode berlin(shell);
    core::Screen& s = berlin.screen(0);

    // Length clamps 3..16
    for (int i = 0; i < 30; ++i) s.onEncoder(2, +1);
    TEST_ASSERT_EQUAL_INT(16, berlin.params().length);
    for (int i = 0; i < 30; ++i) s.onEncoder(2, -1);
    TEST_ASSERT_EQUAL_INT(3, berlin.params().length);

    // Density clamps 0..100 (step = 5)
    for (int i = 0; i < 40; ++i) s.onEncoder(4, +1);
    TEST_ASSERT_EQUAL_INT(100, berlin.params().density);
    for (int i = 0; i < 40; ++i) s.onEncoder(4, -1);
    TEST_ASSERT_EQUAL_INT(0, berlin.params().density);
}

// ---------------------------------------------------------------------------
// Character + Behavior screen encoder edits + clamping.
// ---------------------------------------------------------------------------
static void test_character_behavior_screens_edit_clamp() {
    core::AppShell shell;
    core::BerlinMode berlin(shell);
    core::Screen& ch = berlin.screen(1);
    core::Screen& bh = berlin.screen(3);

    // --- CharacterScreen ---
    // Gate clamps 40..99 (Enc1)
    for (int i = 0; i < 100; ++i) ch.onEncoder(1, +1);
    TEST_ASSERT_EQUAL_INT(99, berlin.params().gatePercent);
    for (int i = 0; i < 100; ++i) ch.onEncoder(1, -1);
    TEST_ASSERT_EQUAL_INT(40, berlin.params().gatePercent);

    // OctaveRange clamps 1..3 (Enc4)
    for (int i = 0; i < 10; ++i) ch.onEncoder(4, +1);
    TEST_ASSERT_EQUAL_INT(3, berlin.params().octaveRange);
    for (int i = 0; i < 10; ++i) ch.onEncoder(4, -1);
    TEST_ASSERT_EQUAL_INT(1, berlin.params().octaveRange);

    // --- BehaviorScreen ---
    // Morph clamps 0..100 (Enc2, step 5)
    for (int i = 0; i < 40; ++i) bh.onEncoder(2, +1);
    TEST_ASSERT_EQUAL_INT(100, berlin.params().morph);
    for (int i = 0; i < 40; ++i) bh.onEncoder(2, -1);
    TEST_ASSERT_EQUAL_INT(0, berlin.params().morph);

    // EvolveRate clamps 1..8 (Enc3)
    for (int i = 0; i < 20; ++i) bh.onEncoder(3, +1);
    TEST_ASSERT_EQUAL_INT(8, berlin.params().evolveRate);
    for (int i = 0; i < 20; ++i) bh.onEncoder(3, -1);
    TEST_ASSERT_EQUAL_INT(1, berlin.params().evolveRate);
}

// ---------------------------------------------------------------------------
// Dynamics screen (index 2): velocity/humanize/accent + contextual Enc4.
// ---------------------------------------------------------------------------
static void test_dynamics_screen() {
    core::AppShell shell;
    core::BerlinMode berlin(shell);
    core::Screen& dyn = berlin.screen(2);          // Dynamics now at index 2

    for (int i = 0; i < 200; ++i) dyn.onEncoder(1, +1);
    TEST_ASSERT_EQUAL_INT(126, berlin.params().velocityBase);
    for (int i = 0; i < 50; ++i) dyn.onEncoder(3, -1);
    TEST_ASSERT_EQUAL_INT(0, berlin.params().accent);

    berlin.params().algorithm = core::BerlinAlgorithm::DrunkardWalk;
    for (int i = 0; i < 20; ++i) dyn.onEncoder(4, +1);
    TEST_ASSERT_EQUAL_INT(7, berlin.params().scatter);
    berlin.params().algorithm = core::BerlinAlgorithm::GatePitchPhasing;
    for (int i = 0; i < 30; ++i) dyn.onEncoder(4, +1);
    TEST_ASSERT_EQUAL_INT(16, berlin.params().gateLen);
    berlin.params().algorithm = core::BerlinAlgorithm::DegreeWeighted;
    uint8_t scBefore = berlin.params().scatter, glBefore = berlin.params().gateLen;
    for (int i = 0; i < 5; ++i) dyn.onEncoder(4, +1);
    TEST_ASSERT_EQUAL_UINT8(scBefore, berlin.params().scatter);
    TEST_ASSERT_EQUAL_UINT8(glBefore, berlin.params().gateLen);
}

// ---------------------------------------------------------------------------
// onExit() silences a sounding note so voices do not hang on mode switch.
// ---------------------------------------------------------------------------
static void test_onexit_silences_sounding_note() {
    core::AppShell shell;
    core::BerlinMode berlin(shell);
    FakeMidiOutput out; berlin.setMidiOutput(&out);
    berlin.onEnter();

    // Latch1 ON → play; advance a tick so the engine emits a NoteOn for step 0.
    berlin.onRawInput({core::RawInput::Kind::Latch, 1, 0, true});
    berlin.onClockTick();   // emitStep(0) → NoteOn queued; noteSounding_ = true
    TEST_ASSERT_TRUE(berlin.engine().isPlaying());

    // Count NoteOff events before calling onExit().
    int noteOffsBefore = 0;
    for (const auto& e : out.events) { if (!e.isOn) ++noteOffsBefore; }

    berlin.onExit();

    // Engine must be stopped and the sounding note silenced.
    TEST_ASSERT_FALSE(berlin.engine().isPlaying());
    int noteOffsAfter = 0;
    for (const auto& e : out.events) { if (!e.isOn) ++noteOffsAfter; }
    TEST_ASSERT_GREATER_THAN(noteOffsBefore, noteOffsAfter);
}

// ---------------------------------------------------------------------------
// Latch3 (Generate) fires on EACH flip (up or down), so one toggle = one
// regenerate (the sim/hardware latch toggles, so rising-only would need an
// off-then-on to re-trigger). generate() rewinds the playhead to 0, so we
// prove each flip acted by advancing the playhead and checking a flip resets it.
// ---------------------------------------------------------------------------
static void test_latch3_generate_on_each_flip() {
    core::AppShell shell;
    core::BerlinMode berlin(shell);
    FakeMidiOutput out; berlin.setMidiOutput(&out);
    berlin.onEnter();
    berlin.onRawInput({core::RawInput::Kind::Latch, 1, 0, true});   // play

    for (int i = 0; i < 12; ++i) berlin.onClockTick();              // 8th step → playhead 1
    TEST_ASSERT_EQUAL_INT(1, berlin.engine().playhead());
    berlin.onRawInput({core::RawInput::Kind::Latch, 3, 0, true});   // flip ON → generate
    TEST_ASSERT_EQUAL_INT(0, berlin.engine().playhead());           // rewound

    for (int i = 0; i < 12; ++i) berlin.onClockTick();              // playhead 1 again
    TEST_ASSERT_EQUAL_INT(1, berlin.engine().playhead());
    berlin.onRawInput({core::RawInput::Kind::Latch, 3, 0, false});  // flip OFF → also generates
    TEST_ASSERT_EQUAL_INT(0, berlin.engine().playhead());           // rewound again
}

// ---------------------------------------------------------------------------
// Algorithm dispatch: switching params_.algorithm before a generate causes the
// engine to use the corresponding generator (reflected in the produced sequence).
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// Live behavior: structural edits immediately regenerate the sequence.
// ---------------------------------------------------------------------------
static int berlinSeqDiff(const core::BerlinSequence& a, const core::BerlinSequence& b) {
    if (a.length() != b.length()) return 999;
    int d = 0;
    for (int i = 0; i < a.length(); ++i) {
        if (a.step(i).active   != b.step(i).active   ||
            a.step(i).note     != b.step(i).note      ||
            a.step(i).velocity != b.step(i).velocity) {
            ++d;
        }
    }
    return d;
}

static void test_live_regenerates_on_structural_edit() {
    core::AppShell shell;
    core::BerlinMode berlin(shell);
    FakeMidiOutput out; berlin.setMidiOutput(&out);
    berlin.onEnter();
    berlin.params().behavior = core::BerlinBehavior::Live;
    berlin.params().length = 16;

    core::Screen& structure = berlin.screen(0);
    structure.onEncoder(2, -1);                       // Length 16 → 15, Live regen
    TEST_ASSERT_EQUAL_INT(15, berlin.engine().sequence().length());
    structure.onEncoder(2, -1);                       // 15 → 14
    TEST_ASSERT_EQUAL_INT(14, berlin.engine().sequence().length());
}

static void test_live_ignores_performance_edit_and_locked_never_regens() {
    core::AppShell shell;
    core::BerlinMode berlin(shell);
    FakeMidiOutput out; berlin.setMidiOutput(&out);
    berlin.onEnter();

    // Live + PERFORMANCE edit (Velocity, Dynamics Enc1) must NOT regenerate.
    berlin.params().behavior = core::BerlinBehavior::Live;
    core::BerlinSequence before = berlin.engine().sequence();
    berlin.screen(2).onEncoder(1, +1);                // velocity base +1 (performance)
    TEST_ASSERT_EQUAL_INT(0, berlinSeqDiff(before, berlin.engine().sequence()));

    // Locked + structural edit (Density) must NOT regenerate either.
    berlin.params().behavior = core::BerlinBehavior::Locked;
    core::BerlinSequence base2 = berlin.engine().sequence();
    berlin.screen(0).onEncoder(4, +1);                // density +5 (structural) but Locked
    TEST_ASSERT_EQUAL_INT(0, berlinSeqDiff(base2, berlin.engine().sequence()));
}

// Live regenerates FULLY (ignores Morph): a structural edit must take complete
// effect even at Morph 0 (where a morph-blend would keep the old steps).
static void test_live_full_regen_ignores_morph() {
    core::AppShell shell;
    core::BerlinMode berlin(shell);
    FakeMidiOutput out; berlin.setMidiOutput(&out);
    berlin.onEnter();                                  // base generated at octaveBase 48
    berlin.params().behavior = core::BerlinBehavior::Live;
    berlin.params().morph = 0;                         // a blend would keep the base notes

    const uint8_t before0 = berlin.engine().sequence().step(0).note;   // root in base octave
    berlin.screen(1).onEncoder(3, +1);                 // Character Enc3: OctaveBase 48 → 60
    const uint8_t after0 = berlin.engine().sequence().step(0).note;
    TEST_ASSERT_TRUE(after0 > before0);                // octave shift fully applied (full regen)
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_latch1_play_pause_latch2_stop);
    RUN_TEST(test_held_latch_edge_detect);
    RUN_TEST(test_latch3_generate_on_each_flip);
    RUN_TEST(test_structure_screen_edits_and_clamps);
    RUN_TEST(test_character_behavior_screens_edit_clamp);
    RUN_TEST(test_dynamics_screen);
    RUN_TEST(test_onexit_silences_sounding_note);
    RUN_TEST(test_algorithm_dispatch);
    RUN_TEST(test_live_regenerates_on_structural_edit);
    RUN_TEST(test_live_ignores_performance_edit_and_locked_never_regens);
    RUN_TEST(test_live_full_regen_ignores_morph);
    return UNITY_END();
}
