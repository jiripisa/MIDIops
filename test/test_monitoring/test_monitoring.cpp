#include <unity.h>

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

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_white_key_index_roundtrip);
    RUN_TEST(test_c2_is_white_first_key);
    RUN_TEST(test_out_of_range_note_has_invalid_rect);
    return UNITY_END();
}
