#include <unity.h>
#include "core/MidiMessage.h"

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

int main() { UNITY_BEGIN(); RUN_TEST(test_realtime_types_are_not_channel_voice); return UNITY_END(); }
