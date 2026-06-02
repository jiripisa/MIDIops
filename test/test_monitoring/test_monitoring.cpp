#include <unity.h>

#include "support/StubDisplay.h"
#include "core/render/Color.h"
#include "core/render/KeyLayout.h"
#include "core/render/WormsRenderer.h"
#include "core/NoteWormModel.h"
#include "core/app/AppShell.h"
#include "core/modes/MonitoringMode.h"

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

// ---- NoteWormModel tests -------------------------------------------------

static int countLive(const core::NoteWormModel& m) {
    int n=0; for (int i=0;i<m.maxWorms();++i) if (m.worms()[i].live) ++n; return n;
}

static void test_noteon_spawns_growing_input_worm() {
    core::NoteWormModel m; m.tick(0); m.onNoteOn(1,60);
    TEST_ASSERT_EQUAL_INT(1, countLive(m));
    const core::NoteWormModel::Worm* w=nullptr;
    for (int i=0;i<m.maxWorms();++i) if (m.worms()[i].live) w=&m.worms()[i];
    TEST_ASSERT_TRUE(w->growing); TEST_ASSERT_FALSE(w->isOutput);
    TEST_ASSERT_EQUAL_INT(60, w->note);
    TEST_ASSERT_EQUAL_INT(core::kRollBottom-1, w->bottomY);
}

static void test_pressedChannelFor_tracks_bitmask() {
    core::NoteWormModel m; m.tick(0); m.onNoteOn(3,64);
    TEST_ASSERT_EQUAL_INT(3, m.pressedChannelFor(64));
    m.onNoteOff(3,64);
    TEST_ASSERT_EQUAL_INT(0, m.pressedChannelFor(64));
}

static void test_noteoff_freezes_worm_growth() {
    core::NoteWormModel m; m.tick(0); m.onNoteOn(1,60); m.onNoteOff(1,60);
    const core::NoteWormModel::Worm* w=nullptr;
    for (int i=0;i<m.maxWorms();++i) if (m.worms()[i].live) w=&m.worms()[i];
    TEST_ASSERT_TRUE(w!=nullptr); TEST_ASSERT_FALSE(w->growing);
}

static void test_tick_scrolls_growing_worm_topY_up() {
    core::NoteWormModel m; m.tick(0); m.onNoteOn(1,60);
    const int16_t top0=m.worms()[0].topY; m.tick(100);
    TEST_ASSERT_TRUE(m.worms()[0].topY < top0);
    TEST_ASSERT_EQUAL_INT(core::kRollBottom-1, m.worms()[0].bottomY);
}

static void test_released_worm_eventually_expires() {
    core::NoteWormModel m; m.tick(0); m.onNoteOn(1,60); m.onNoteOff(1,60);
    for (uint32_t t=100;t<=20000;t+=100) m.tick(t);
    TEST_ASSERT_EQUAL_INT(0, countLive(m));
}

static void test_engine_worm_is_output() {
    core::NoteWormModel m; m.tick(0); m.onEngineNoteOn(2,67);
    TEST_ASSERT_EQUAL_INT(2, m.outPressedChannelFor(67));
    bool out=false;
    for (int i=0;i<m.maxWorms();++i) if (m.worms()[i].live && m.worms()[i].isOutput) out=true;
    TEST_ASSERT_TRUE(out);
}

static void test_clearInput_freezes_and_clears() {
    core::NoteWormModel m; m.tick(0); m.onNoteOn(1,60); m.clearInput();
    TEST_ASSERT_EQUAL_INT(0, m.pressedChannelFor(60));
    for (int i=0;i<m.maxWorms();++i) if (m.worms()[i].live) TEST_ASSERT_FALSE(m.worms()[i].growing);
}

static void test_two_channels_same_note() {
    core::NoteWormModel m; m.tick(0);
    m.onNoteOn(1, 60);
    m.onNoteOn(2, 60);
    TEST_ASSERT_EQUAL_INT(2, countLive(m));
    TEST_ASSERT_EQUAL_INT(1, m.pressedChannelFor(60));  // lowest held channel
    m.onNoteOff(1, 60);
    TEST_ASSERT_EQUAL_INT(2, m.pressedChannelFor(60));  // ch2 still holds
    // ch2's worm is still growing; ch1's worm is released (not growing)
    int growing = 0;
    for (int i = 0; i < m.maxWorms(); ++i)
        if (m.worms()[i].live && m.worms()[i].growing) ++growing;
    TEST_ASSERT_EQUAL_INT(1, growing);
}

static void test_channel_out_of_range_is_ignored() {
    core::NoteWormModel m; m.tick(0);
    m.onNoteOn(0, 60);
    m.onNoteOn(17, 60);
    TEST_ASSERT_EQUAL_INT(0, countLive(m));
    TEST_ASSERT_EQUAL_INT(0, m.pressedChannelFor(60));
}

static void test_engine_noteoff_clears_output_state() {
    core::NoteWormModel m; m.tick(0);
    m.onEngineNoteOn(2, 67);
    TEST_ASSERT_EQUAL_INT(2, m.outPressedChannelFor(67));
    m.onEngineNoteOff(2, 67);
    TEST_ASSERT_EQUAL_INT(0, m.outPressedChannelFor(67));
}

// ---- WormsRenderer tests -------------------------------------------------

static void test_renderer_draws_keyboard_surface() {
    core::NoteWormModel m; m.tick(0);
    StubDisplay d;
    core::WormsRenderer::render(m, d);
    TEST_ASSERT_TRUE(d.rects > 0);   // keyboard surface + separators at least
}
static void test_live_input_worm_adds_fills() {
    core::NoteWormModel m; m.tick(0);
    StubDisplay empty; core::WormsRenderer::drawWorms(m, empty);
    const int before = empty.rects;
    m.onNoteOn(1, 60); m.tick(200);   // grow a worm a few px
    StubDisplay withWorm; core::WormsRenderer::drawWorms(m, withWorm);
    TEST_ASSERT_TRUE(withWorm.rects > before);
}

// ---- MonitoringMode tests ---------------------------------------------------

static void test_monitoring_mode_renders_injected_note() {
    core::AppShell shell;
    core::MonitoringMode mon;
    shell.addMode(&mon);
    shell.begin();

    // NoteOn: type=NoteOn, channel=1, data1=note, data2=velocity
    core::MidiMessage on{};
    on.type    = core::MidiType::NoteOn;
    on.channel = 1;
    on.data1   = 60;   // middle C
    on.data2   = 100;  // velocity

    shell.onMidiIn(on);
    shell.tick(200);

    StubDisplay d;
    shell.render(d);
    TEST_ASSERT_TRUE(d.rects > 0);               // worms + keyboard drawn
    TEST_ASSERT_TRUE(d.drewText("Monitoring"));  // top bar shows mode name
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
    RUN_TEST(test_noteon_spawns_growing_input_worm);
    RUN_TEST(test_pressedChannelFor_tracks_bitmask);
    RUN_TEST(test_noteoff_freezes_worm_growth);
    RUN_TEST(test_tick_scrolls_growing_worm_topY_up);
    RUN_TEST(test_released_worm_eventually_expires);
    RUN_TEST(test_engine_worm_is_output);
    RUN_TEST(test_clearInput_freezes_and_clears);
    RUN_TEST(test_two_channels_same_note);
    RUN_TEST(test_channel_out_of_range_is_ignored);
    RUN_TEST(test_engine_noteoff_clears_output_state);
    RUN_TEST(test_renderer_draws_keyboard_surface);
    RUN_TEST(test_live_input_worm_adds_fills);
    RUN_TEST(test_monitoring_mode_renders_injected_note);
    return UNITY_END();
}
