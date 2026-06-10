#include <unity.h>

#include "core/app/AppShell.h"
#include "core/modes/SettingsMode.h"
#include "support/StubDisplay.h"

void setUp() {}
void tearDown() {}

static void test_settings_screens() {
    core::AppShell shell; core::SettingsMode s(shell);
    TEST_ASSERT_EQUAL_INT(2, s.screenCount());
    TEST_ASSERT_EQUAL_STRING("midi",  s.screen(0).name());
    TEST_ASSERT_EQUAL_STRING("scale", s.screen(1).name());
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
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_settings_screens);
    RUN_TEST(test_midi_screen_edits);
    RUN_TEST(test_scale_screen_edits_and_renders);
    RUN_TEST(test_midi_screen_renders_omni);
    return UNITY_END();
}
