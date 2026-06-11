#include <unity.h>
#include "core/MidiMessage.h"
#include "core/ClockFollower.h"
#include "core/app/AppShell.h"
#include "core/modes/BpmMode.h"
#include "support/Fakes.h"
#include "support/FakeMidiOutput.h"

void setUp() {}
void tearDown() {}

static void test_realtime_types_are_not_channel_voice() {
    core::MidiMessage m{};
    m.type = core::MidiType::Clock;
    TEST_ASSERT_FALSE(m.isChannelVoice());
    TEST_ASSERT_EQUAL_HEX8(0xF8, static_cast<uint8_t>(core::MidiType::Clock));
    TEST_ASSERT_EQUAL_HEX8(0xFA, static_cast<uint8_t>(core::MidiType::Start));
    TEST_ASSERT_EQUAL_HEX8(0xFC, static_cast<uint8_t>(core::MidiType::Stop));
}

static void test_follower_derives_120bpm() {
    core::ClockFollower f;
    // 120 BPM → beat = 500 ms → 24 pulses over 500 ms.
    for (int i = 0; i <= 24; ++i) f.onPulse(static_cast<uint32_t>(i * 500 / 24));
    TEST_ASSERT_INT_WITHIN(2, 120, f.bpm());
}

static void test_follower_clamps() {
    core::ClockFollower f;
    for (int i = 0; i <= 24; ++i) f.onPulse(static_cast<uint32_t>(i * 5));  // very fast → clamp 300
    TEST_ASSERT_EQUAL_INT(300, f.bpm());
}

static void test_external_clock_follows_without_echo() {
    core::AppShell shell;
    FakeMode a("a", 1);
    FakeMidiOutput out;
    shell.setMidiOutput(&out);
    shell.addMode(&a);
    shell.begin();
    shell.setClockSource(core::ClockSource::External);
    TEST_ASSERT_EQUAL_INT(0, out.lastBpm);        // internal stopped: setClockBpm(0).
    core::MidiMessage clk{}; clk.type = core::MidiType::Clock;
    shell.onMidiIn(clk);
    TEST_ASSERT_EQUAL_INT(1, a.clockTicks);       // active mode ticked once (follow)
    // The incoming clock is followed (mode ticked), NOT echoed back out: on a
    // single MIDI interface the host is the master, and re-emitting its own
    // clock floods usbMIDI TX and starves RX on the device. The output sees no
    // clock-related sends — only the setClockBpm(0) that stopped the internal
    // master when External was selected.
    TEST_ASSERT_EQUAL_INT(0, out.starts + out.continues + out.stops);
}

// After the external clock pauses and resumes, the first window must not report
// a bogus low BPM derived across the gap. A >500 ms silence re-anchors the
// window while keeping the last known bpm_.
static void test_follower_gap_reset() {
    core::ClockFollower f;
    // Steady 120 BPM (beat = 500 ms over 24 pulses) → bpm becomes ~120. Use
    // i*500/24 absolute timestamps so the window spans exactly 500 ms.
    uint32_t base = 0;
    for (int i = 0; i <= 24; ++i) f.onPulse(base + static_cast<uint32_t>(i * 500 / 24));
    TEST_ASSERT_INT_WITHIN(2, 120, f.bpm());

    // Pause: the clock stops for 5 seconds, then resume steady 120 BPM.
    base += 500 + 5000;
    // Immediately after the gap, the old tempo is retained (NOT a bogus ~30).
    f.onPulse(base);
    TEST_ASSERT_INT_WITHIN(2, 120, f.bpm());

    // After one full resume window bpm is still ~120, never having dipped
    // toward 30 because of the cross-gap interval.
    for (int i = 1; i <= 24; ++i) f.onPulse(base + static_cast<uint32_t>(i * 500 / 24));
    TEST_ASSERT_INT_WITHIN(2, 120, f.bpm());
}

// BpmMode tempo screen ignores encoder edits under an External clock source
// (BPM is followed, not set); Internal source allows edits.
static void test_bpm_readonly_under_external_clock() {
    core::AppShell shell;
    core::BpmMode bpm(shell);
    shell.addMode(&bpm);
    shell.begin();
    const uint16_t before = shell.bpm();

    shell.setClockSource(core::ClockSource::External);
    bpm.screen(0).onEncoder(1, +5);
    TEST_ASSERT_EQUAL_UINT16(before, shell.bpm());   // unchanged under External

    shell.setClockSource(core::ClockSource::Internal);
    bpm.screen(0).onEncoder(1, +5);
    TEST_ASSERT_EQUAL_UINT16(before + 5, shell.bpm());   // editable under Internal
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_realtime_types_are_not_channel_voice);
    RUN_TEST(test_follower_derives_120bpm);
    RUN_TEST(test_follower_clamps);
    RUN_TEST(test_external_clock_follows_without_echo);
    RUN_TEST(test_follower_gap_reset);
    RUN_TEST(test_bpm_readonly_under_external_clock);
    return UNITY_END();
}
