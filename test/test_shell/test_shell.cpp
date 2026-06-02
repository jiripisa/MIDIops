#include <unity.h>

#include "support/Fakes.h"

void setUp() {}
void tearDown() {}

static void test_fake_mode_screen_dispatch() {
    FakeMode m("arp", 2);
    TEST_ASSERT_EQUAL_INT(2, m.screenCount());
    m.screen(0).onEncoder(1, +1);
    auto& fs = static_cast<FakeScreen&>(m.screen(0));
    TEST_ASSERT_EQUAL_INT(1, static_cast<int>(fs.encoders.size()));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_fake_mode_screen_dispatch);
    return UNITY_END();
}
