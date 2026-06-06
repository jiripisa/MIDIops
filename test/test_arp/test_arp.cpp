#include <unity.h>

#include "core/Scale.h"

void setUp() {}
void tearDown() {}

static void test_cmajor_contains() {
    core::Scale s(core::Scale::Type::Major, 0);
    TEST_ASSERT_TRUE(s.contains(60));   // C4
    TEST_ASSERT_TRUE(s.contains(62));   // D4
    TEST_ASSERT_FALSE(s.contains(61));  // C#4
}
static void test_quantize_rounds_to_scale() {
    core::Scale s(core::Scale::Type::Major, 0);
    TEST_ASSERT_EQUAL_INT(60, s.quantize(61));   // C#4 -> C4
    TEST_ASSERT_EQUAL_INT(60, s.quantize(60));
}
static void test_degree_builds_triad_cmajor() {
    core::Scale s(core::Scale::Type::Major, 0);
    TEST_ASSERT_EQUAL_INT(60, s.degreeNote(60, 0));  // C4
    TEST_ASSERT_EQUAL_INT(64, s.degreeNote(60, 2));  // E4
    TEST_ASSERT_EQUAL_INT(67, s.degreeNote(60, 4));  // G4
    TEST_ASSERT_EQUAL_INT(72, s.degreeNote(60, 7));  // C5
}
static void test_degree_on_second_degree() {
    core::Scale s(core::Scale::Type::Major, 0);
    TEST_ASSERT_EQUAL_INT(62, s.degreeNote(62, 0));  // D4
    TEST_ASSERT_EQUAL_INT(65, s.degreeNote(62, 2));  // F4
    TEST_ASSERT_EQUAL_INT(69, s.degreeNote(62, 4));  // A4
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_cmajor_contains);
    RUN_TEST(test_quantize_rounds_to_scale);
    RUN_TEST(test_degree_builds_triad_cmajor);
    RUN_TEST(test_degree_on_second_degree);
    return UNITY_END();
}
