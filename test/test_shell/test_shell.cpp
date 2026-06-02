#include <unity.h>

#include "core/app/AppShell.h"
#include "support/Fakes.h"
#include "support/StubDisplay.h"
#include "support/FakeMidiOutput.h"

void setUp() {}
void tearDown() {}

static void test_fake_mode_screen_dispatch() {
    FakeMode m("arp", 2);
    TEST_ASSERT_EQUAL_INT(2, m.screenCount());
    m.screen(0).onEncoder(1, +1);
    auto& fs = static_cast<FakeScreen&>(m.screen(0));
    TEST_ASSERT_EQUAL_INT(1, static_cast<int>(fs.encoders.size()));
}

static void test_enc1to4_route_to_active_screen() {
    core::AppShell shell;
    FakeMode a("a", 2), b("b", 1);
    shell.addMode(&a); shell.addMode(&b);
    shell.begin();
    shell.onEncoderKnob(1, +3);
    shell.onEncoderSw(2);
    auto& s0 = static_cast<FakeScreen&>(a.screen(0));
    TEST_ASSERT_EQUAL_INT(1, static_cast<int>(s0.encoders.size()));
    TEST_ASSERT_EQUAL_INT(3, s0.encoders[0].second);
    TEST_ASSERT_EQUAL_INT(1, static_cast<int>(s0.sws.size()));
    TEST_ASSERT_EQUAL_INT(1, s0.encoders[0].first);  // index preserved
    TEST_ASSERT_EQUAL_INT(2, s0.sws[0]);             // switch index preserved
}

static void test_enc5_switches_screen_with_wrap() {
    core::AppShell shell;
    FakeMode a("a", 3);
    shell.addMode(&a);
    shell.begin();
    TEST_ASSERT_EQUAL_INT(0, shell.activeScreenIndex());
    shell.onEncoderKnob(5, +1);
    TEST_ASSERT_EQUAL_INT(1, shell.activeScreenIndex());
    shell.onEncoderKnob(5, -1);
    shell.onEncoderKnob(5, -1);
    TEST_ASSERT_EQUAL_INT(2, shell.activeScreenIndex());  // wrapped past 0
    shell.onEncoderKnob(5, +1);                       // 2 -> 0, forward wrap
    TEST_ASSERT_EQUAL_INT(0, shell.activeScreenIndex());
    // Each screen switch must fire the lifecycle pair on the screens involved.
    auto& s1 = static_cast<FakeScreen&>(a.screen(1));
    TEST_ASSERT_TRUE(s1.enters >= 1);
    TEST_ASSERT_TRUE(s1.exits  >= 1);
}

static void test_midi_in_reaches_active_mode() {
    core::AppShell shell;
    FakeMode a("a", 1);
    shell.addMode(&a);
    shell.begin();
    core::MidiMessage m{};
    shell.onMidiIn(m);
    TEST_ASSERT_EQUAL_INT(1, a.midiCount);
}

static void test_raw_input_tap_fires_for_all_controls() {
    core::AppShell shell;
    FakeMode a("a", 1);
    shell.addMode(&a);
    shell.begin();
    shell.onEncoderKnob(5, +1);
    shell.onLatch(2, true);
    TEST_ASSERT_EQUAL_INT(2, a.rawCount);
}

static void test_overlay_open_select_confirm() {
    core::AppShell shell;
    FakeMode a("a", 1), b("b", 1), c("c", 1);
    shell.addMode(&a); shell.addMode(&b); shell.addMode(&c);
    shell.begin();
    shell.onEncoderSw(5);                       // open overlay
    TEST_ASSERT_TRUE(shell.overlayOpen());
    shell.onEncoderKnob(2, +2);                 // select index 2 (c)
    TEST_ASSERT_EQUAL_INT(2, shell.overlayChoice());
    shell.onEncoderSw(5);                        // confirm
    TEST_ASSERT_FALSE(shell.overlayOpen());
    TEST_ASSERT_EQUAL_INT(2, shell.activeModeIndex());
    TEST_ASSERT_EQUAL_INT(1, c.enters);          // entered once on confirm
}

static void test_overlay_timeout_reverts() {
    core::AppShell shell;
    FakeMode a("a", 1), b("b", 1);
    shell.addMode(&a); shell.addMode(&b);
    shell.begin();
    shell.tick(1000);
    shell.onEncoderSw(5);                        // open at t=1000
    shell.onEncoderKnob(2, +1);                  // select b at t=1000
    shell.tick(1000 + 3000);                      // exactly timeout
    TEST_ASSERT_FALSE(shell.overlayOpen());
    TEST_ASSERT_EQUAL_INT(0, shell.activeModeIndex());  // unchanged
    TEST_ASSERT_EQUAL_INT(0, b.enters);
}

static void test_overlay_rotation_resets_timeout() {
    core::AppShell shell;
    FakeMode a("a", 1), b("b", 1);
    shell.addMode(&a); shell.addMode(&b);
    shell.begin();
    shell.tick(1000);
    shell.onEncoderSw(5);
    shell.tick(3500);                             // overlay still open (opened at 1000, <4000)
    shell.onEncoderKnob(2, +1);                   // rotate at t=3500 resets timer
    shell.tick(3500 + 2999);                       // <3s since last rotate
    TEST_ASSERT_TRUE(shell.overlayOpen());
}

static void test_latch1_toggles_play_pause_and_sends_realtime() {
    core::AppShell shell;
    FakeMidiOutput out;
    FakeMode a("a", 1);
    shell.addMode(&a);
    shell.setMidiOutput(&out);
    shell.begin();
    shell.onLatch(1, true);                  // Play (rising)
    TEST_ASSERT_EQUAL_INT(1, out.starts);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(core::Transport::Play), static_cast<int>(a.transports.back()));
    shell.onLatch(1, false);                 // change -> Pause
    TEST_ASSERT_EQUAL_INT(1, out.stops);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(core::Transport::Pause), static_cast<int>(a.transports.back()));
    shell.onLatch(1, true);                  // change -> Play (continue)
    TEST_ASSERT_EQUAL_INT(1, out.continues);
}

static void test_latch2_stop_latch3_reset() {
    core::AppShell shell;
    FakeMidiOutput out;
    FakeMode a("a", 1);
    shell.addMode(&a);
    shell.setMidiOutput(&out);
    shell.begin();
    shell.onLatch(2, true);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(core::Transport::Stop), static_cast<int>(a.transports.back()));
    shell.onLatch(3, true);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(core::Transport::Reset), static_cast<int>(a.transports.back()));
    TEST_ASSERT_EQUAL_INT(2, out.stops);     // stop + reset both send 0xFC
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_fake_mode_screen_dispatch);
    RUN_TEST(test_enc1to4_route_to_active_screen);
    RUN_TEST(test_enc5_switches_screen_with_wrap);
    RUN_TEST(test_midi_in_reaches_active_mode);
    RUN_TEST(test_raw_input_tap_fires_for_all_controls);
    RUN_TEST(test_overlay_open_select_confirm);
    RUN_TEST(test_overlay_timeout_reverts);
    RUN_TEST(test_overlay_rotation_resets_timeout);
    RUN_TEST(test_latch1_toggles_play_pause_and_sends_realtime);
    RUN_TEST(test_latch2_stop_latch3_reset);
    return UNITY_END();
}
