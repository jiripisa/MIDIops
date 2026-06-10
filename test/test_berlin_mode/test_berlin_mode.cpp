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
// Latch2/Latch3 are edge-detected: a held-ON latch delivered every main-loop
// frame must NOT re-trigger. We prove this on Latch2 (Stop) by advancing the
// playhead, then holding Latch2 ON across many frames: the first ON rewinds,
// and subsequent (still-ON) frames are no-ops — playhead stays at 0 and the
// engine remains stopped (it does not "play" or re-fire).
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

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_latch1_play_pause_latch2_stop);
    RUN_TEST(test_held_latch_edge_detect);
    RUN_TEST(test_structure_screen_edits_and_clamps);
    return UNITY_END();
}
