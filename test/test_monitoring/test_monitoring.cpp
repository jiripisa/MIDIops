#include <unity.h>

#include "core/render/Color.h"
#include "core/render/KeyLayout.h"

void setUp() {}
void tearDown() {}

static void test_white_key_index_roundtrip() {
    for (int i = 0; i < core::kWhiteKeysVisible; ++i) {
        const uint8_t note = core::whiteKeyAt(i);
        TEST_ASSERT_EQUAL_INT(i, core::whiteKeyIdx(note));
    }
}

static void test_c2_is_white_first_key() {
    TEST_ASSERT_FALSE(core::isBlackPc(0));            // C is white
    TEST_ASSERT_TRUE(core::isBlackPc(1));             // C# is black
    const core::KeyRect r = core::keyRectFor(core::kLowestNote);  // C2
    TEST_ASSERT_FALSE(r.isBlack);
    TEST_ASSERT_EQUAL_INT(core::kKeyboardX0, r.x);
}

static void test_out_of_range_note_has_invalid_rect() {
    const core::KeyRect r = core::keyRectFor(0);
    TEST_ASSERT_EQUAL_INT(-1, r.x);
}

static void test_scale_full_is_identity() {
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, core::scaleRgb565(0xFFFF, 256));
}
static void test_scale_zero_is_black() {
    TEST_ASSERT_EQUAL_HEX16(0x0000, core::scaleRgb565(0xFFFF, 0));
}
static void test_channel0_is_white() {
    TEST_ASSERT_EQUAL_HEX16(core::color::White, core::channelColor(0));
}
static void test_channels_have_distinct_colors() {
    TEST_ASSERT_NOT_EQUAL(core::channelColor(1), core::channelColor(2));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_white_key_index_roundtrip);
    RUN_TEST(test_c2_is_white_first_key);
    RUN_TEST(test_out_of_range_note_has_invalid_rect);
    RUN_TEST(test_scale_full_is_identity);
    RUN_TEST(test_scale_zero_is_black);
    RUN_TEST(test_channel0_is_white);
    RUN_TEST(test_channels_have_distinct_colors);
    return UNITY_END();
}
