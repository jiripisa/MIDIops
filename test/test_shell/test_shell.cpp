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

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_fake_mode_screen_dispatch);
    RUN_TEST(test_enc1to4_route_to_active_screen);
    RUN_TEST(test_enc5_switches_screen_with_wrap);
    RUN_TEST(test_midi_in_reaches_active_mode);
    RUN_TEST(test_raw_input_tap_fires_for_all_controls);
    return UNITY_END();
}
