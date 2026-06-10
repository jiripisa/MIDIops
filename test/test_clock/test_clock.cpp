#include <unity.h>
#include "core/MidiMessage.h"
#include "core/ClockFollower.h"
#include "core/app/AppShell.h"
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
    // The incoming clock is followed, NOT echoed back: on a single MIDI
    // interface the host is the master, and re-emitting its own clock floods
    // usbMIDI TX and starves RX on the device. So no pulse is forwarded.
    TEST_ASSERT_EQUAL_INT(0, out.forwarded);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_realtime_types_are_not_channel_voice);
    RUN_TEST(test_follower_derives_120bpm);
    RUN_TEST(test_follower_clamps);
    RUN_TEST(test_external_clock_follows_without_echo);
    return UNITY_END();
}
