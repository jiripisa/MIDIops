#include <unity.h>

#include "core/app/AppShell.h"
#include "core/modes/SettingsMode.h"
#include "support/FakeStorage.h"
#include "support/StubDisplay.h"

void setUp() {}
void tearDown() {}

static void test_settings_screens() {
    core::AppShell shell; core::SettingsMode s(shell);
    TEST_ASSERT_EQUAL_INT(3, s.screenCount());
    TEST_ASSERT_EQUAL_STRING("midi",   s.screen(0).name());
    TEST_ASSERT_EQUAL_STRING("scale",  s.screen(1).name());
    TEST_ASSERT_EQUAL_STRING("system", s.screen(2).name());
}

static void test_midi_screen_edits() {
    core::AppShell shell; core::SettingsMode s(shell);
    s.screen(0).onEncoder(1, +3);
    TEST_ASSERT_EQUAL_INT(4, shell.midiOutChannel());
    s.screen(0).onEncoder(2, -1);
    TEST_ASSERT_EQUAL_INT(0, shell.midiInChannel());
    s.screen(0).onEncoder(2, +5);
    TEST_ASSERT_EQUAL_INT(5, shell.midiInChannel());
    s.screen(0).onEncoder(3, +1);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(core::ClockSource::External),
                          static_cast<int>(shell.clockSource()));
}

static void test_scale_screen_edits_and_renders() {
    core::AppShell shell; core::SettingsMode s(shell);
    s.screen(1).onEncoder(2, +2);
    TEST_ASSERT_EQUAL_INT(2, shell.scale().root());
    StubDisplay d; s.screen(1).render(d);
    TEST_ASSERT_TRUE(d.drewText("D"));
    TEST_ASSERT_TRUE(d.drewText("Maj"));
}

static void test_midi_screen_renders_omni() {
    core::AppShell shell; core::SettingsMode s(shell);
    StubDisplay d; s.screen(0).render(d);
    TEST_ASSERT_TRUE(d.drewText("OMNI"));     // default in channel
    TEST_ASSERT_TRUE(d.drewText("Int"));      // default clock source
    TEST_ASSERT_TRUE(d.drewText("Send"));     // default transport mode
}

static void test_midi_screen_transport_cycles() {
    core::AppShell shell; core::SettingsMode s(shell);
    // Default is Send.
    TEST_ASSERT_EQUAL_INT(static_cast<int>(core::TransportMode::Send),
                          static_cast<int>(shell.transportMode()));
    // Enc4 +1: Send → Receive → Off → Send (cycleEnum order Off, Send, Receive).
    s.screen(0).onEncoder(4, +1);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(core::TransportMode::Receive),
                          static_cast<int>(shell.transportMode()));
    s.screen(0).onEncoder(4, +1);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(core::TransportMode::Off),
                          static_cast<int>(shell.transportMode()));
    s.screen(0).onEncoder(4, +1);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(core::TransportMode::Send),
                          static_cast<int>(shell.transportMode()));
    // Zero delta is a no-op.
    s.screen(0).onEncoder(4, 0);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(core::TransportMode::Send),
                          static_cast<int>(shell.transportMode()));
}

// ---------------------------------------------------------------------------
// System screen: two-step factory reset on Enc1 press. First press arms for
// 3 s ("SURE?"); a second press inside the window resets; the window expiring
// returns to idle (the next press only re-arms, never resets).
// ---------------------------------------------------------------------------
static void test_system_screen_arm_confirm_resets() {
    core::AppShell shell;
    FakeStorage st;
    shell.setStorage(&st);
    shell.begin();
    shell.setMidiOutChannel(9);
    core::SettingsMode s(shell);
    core::Screen& sys = s.screen(2);
    sys.update(1000);
    sys.onEncoderSw(1);                 // arm
    TEST_ASSERT_EQUAL_INT(9, shell.midiOutChannel());   // arming alone: no reset
    sys.onEncoderSw(1);                 // confirm inside the window
    TEST_ASSERT_EQUAL_INT(1, shell.midiOutChannel());   // defaults restored
    TEST_ASSERT_TRUE(st.removes >= 1);
}

static void test_system_screen_arm_window_expires() {
    core::AppShell shell;
    shell.begin();
    shell.setMidiOutChannel(9);
    core::SettingsMode s(shell);
    core::Screen& sys = s.screen(2);
    sys.update(1000);
    sys.onEncoderSw(1);                 // arm at t=1000
    sys.update(4100);                   // window (3 s) expired
    sys.onEncoderSw(1);                 // this press only re-arms
    TEST_ASSERT_EQUAL_INT(9, shell.midiOutChannel());   // no reset happened
}

static void test_system_screen_renders_reset_cell() {
    core::AppShell shell;
    core::SettingsMode s(shell);
    core::Screen& sys = s.screen(2);
    StubDisplay d;
    sys.render(d);
    TEST_ASSERT_TRUE(d.drewText("RESET"));
    sys.update(1000);
    sys.onEncoderSw(1);                 // armed
    StubDisplay d2;
    sys.render(d2);
    TEST_ASSERT_TRUE(d2.drewText("SURE?"));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_settings_screens);
    RUN_TEST(test_midi_screen_edits);
    RUN_TEST(test_scale_screen_edits_and_renders);
    RUN_TEST(test_midi_screen_renders_omni);
    RUN_TEST(test_midi_screen_transport_cycles);
    RUN_TEST(test_system_screen_arm_confirm_resets);
    RUN_TEST(test_system_screen_arm_window_expires);
    RUN_TEST(test_system_screen_renders_reset_cell);
    return UNITY_END();
}
