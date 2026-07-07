#include <unity.h>

#include "core/Presets.h"
#include "core/app/PresetScreen.h"
#include "support/FakeStorage.h"
#include "support/StubDisplay.h"

void setUp() {}
void tearDown() {}

// ---------------------------------------------------------------------------
// Serialization round-trips + corruption rejection
// ---------------------------------------------------------------------------

static void test_preset_key_naming() {
    char key[24];
    core::presetKey("arp", 0, key, sizeof key);
    TEST_ASSERT_EQUAL_STRING("arp.s01", key);
    core::presetKey("berlin", 19, key, sizeof key);
    TEST_ASSERT_EQUAL_STRING("berlin.s20", key);
}

static void test_arp_preset_round_trip() {
    FakeStorage st;
    core::ArpParams p;
    p.steps = 7;
    p.rate = core::ArpRate::EighthT;
    p.gatePercent = 35;
    p.direction = core::ArpDirection::DownUp;
    p.octave = -2;
    p.swingPercent = 63;
    p.velocityMode = core::ArpVelocityMode::Accent;
    p.fixedVelocity = 88;
    p.latch = true;
    TEST_ASSERT_FALSE(core::presetExists(st, "arp", 4));
    TEST_ASSERT_TRUE(core::saveArpPreset(st, 4, p));
    TEST_ASSERT_TRUE(core::presetExists(st, "arp", 4));
    core::ArpParams q;
    TEST_ASSERT_TRUE(core::loadArpPreset(st, 4, q));
    TEST_ASSERT_EQUAL_INT(7, q.steps);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(core::ArpRate::EighthT),
                          static_cast<int>(q.rate));
    TEST_ASSERT_EQUAL_INT(35, q.gatePercent);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(core::ArpDirection::DownUp),
                          static_cast<int>(q.direction));
    TEST_ASSERT_EQUAL_INT(-2, q.octave);
    TEST_ASSERT_EQUAL_INT(63, q.swingPercent);
    TEST_ASSERT_EQUAL_INT(88, q.fixedVelocity);
    TEST_ASSERT_TRUE(q.latch);
    TEST_ASSERT_TRUE(core::deletePreset(st, "arp", 4));
    TEST_ASSERT_FALSE(core::presetExists(st, "arp", 4));
}

static void test_berlin_preset2_round_trip_three_voices() {
    FakeStorage st;
    core::BerlinVoicePreset in[3];
    for (int v = 0; v < 3; ++v) {
        in[v].params.length  = static_cast<uint8_t>(14 + v);
        in[v].params.density = static_cast<uint8_t>(30 + v * 10);
        in[v].channel        = static_cast<uint8_t>(4 + v);
        in[v].muted          = (v == 1);
        in[v].seq.setLength(14 + v);
        for (int i = 0; i < in[v].seq.length(); ++i) {
            core::BerlinStep& s = in[v].seq.step(i);
            s.active = (i % 2) == 0;
            s.note = static_cast<uint8_t>(36 + v * 12 + i);
            s.velocity = static_cast<uint8_t>(70 + i);
            s.gateTicks = 6;
            s.velJitter = static_cast<int8_t>(i - 5);
        }
    }
    TEST_ASSERT_FALSE(core::berlinPreset2Usable(st, 3, 3));
    TEST_ASSERT_TRUE(core::saveBerlinPreset2(st, 3, in, 3));
    TEST_ASSERT_TRUE(core::berlinPreset2Usable(st, 3, 3));
    core::BerlinVoicePreset out[3];
    TEST_ASSERT_TRUE(core::loadBerlinPreset2(st, 3, out, 3));
    for (int v = 0; v < 3; ++v) {
        TEST_ASSERT_EQUAL_INT(in[v].params.length, out[v].params.length);
        TEST_ASSERT_EQUAL_INT(in[v].params.density, out[v].params.density);
        TEST_ASSERT_EQUAL_INT(in[v].channel, out[v].channel);
        TEST_ASSERT_EQUAL_INT(in[v].muted ? 1 : 0, out[v].muted ? 1 : 0);
        TEST_ASSERT_EQUAL_INT(in[v].seq.length(), out[v].seq.length());
        for (int i = 0; i < in[v].seq.length(); ++i) {
            TEST_ASSERT_EQUAL_INT(in[v].seq.step(i).note, out[v].seq.step(i).note);
            TEST_ASSERT_EQUAL_INT(in[v].seq.step(i).velocity, out[v].seq.step(i).velocity);
            TEST_ASSERT_EQUAL_INT(in[v].seq.step(i).velJitter, out[v].seq.step(i).velJitter);
        }
    }
}

// A v1-sized blob under the same key reads as EMPTY, never as garbage.
static void test_berlin_preset2_round_trip_four_voices() {
    FakeStorage st;
    core::BerlinVoicePreset in[4];
    for (int v = 0; v < 4; ++v) {
        in[v].params.length  = static_cast<uint8_t>(8 + v * 3);
        in[v].params.density = static_cast<uint8_t>(20 + v * 15);
        in[v].channel        = static_cast<uint8_t>(1 + v);
        in[v].muted          = (v % 2 == 1);
        in[v].seq.setLength(8 + v * 3);
        for (int i = 0; i < in[v].seq.length(); ++i) {
            core::BerlinStep& s = in[v].seq.step(i);
            s.active    = (i % 3) != 2;
            s.note      = static_cast<uint8_t>(24 + v * 10 + i);
            s.velocity  = static_cast<uint8_t>(60 + v * 5 + i);
            s.gateTicks = static_cast<uint16_t>(4 + v);
            s.velJitter = static_cast<int8_t>(v * 3 - i);
        }
    }
    TEST_ASSERT_FALSE(core::berlinPreset2Usable(st, 7, 4));
    TEST_ASSERT_TRUE(core::saveBerlinPreset2(st, 7, in, 4));
    TEST_ASSERT_TRUE(core::berlinPreset2Usable(st, 7, 4));
    core::BerlinVoicePreset out[4];
    TEST_ASSERT_TRUE(core::loadBerlinPreset2(st, 7, out, 4));
    for (int v = 0; v < 4; ++v) {
        TEST_ASSERT_EQUAL_INT(in[v].params.length,  out[v].params.length);
        TEST_ASSERT_EQUAL_INT(in[v].params.density, out[v].params.density);
        TEST_ASSERT_EQUAL_INT(in[v].channel,        out[v].channel);
        TEST_ASSERT_EQUAL_INT(in[v].muted ? 1 : 0,  out[v].muted ? 1 : 0);
        TEST_ASSERT_EQUAL_INT(in[v].seq.length(),   out[v].seq.length());
        for (int i = 0; i < in[v].seq.length(); ++i) {
            TEST_ASSERT_EQUAL_INT(in[v].seq.step(i).note,     out[v].seq.step(i).note);
            TEST_ASSERT_EQUAL_INT(in[v].seq.step(i).velocity, out[v].seq.step(i).velocity);
            TEST_ASSERT_EQUAL_INT(in[v].seq.step(i).velJitter,out[v].seq.step(i).velJitter);
        }
    }
}

static void test_berlin_preset_cross_count_reads_empty() {
    FakeStorage st;
    // Save with count 4, then query/load with count 3 → must fail.
    core::BerlinVoicePreset in4[4];
    for (int v = 0; v < 4; ++v) {
        in4[v].params.length  = 12;
        in4[v].params.density = 50;
        in4[v].channel = static_cast<uint8_t>(v + 1);
        in4[v].seq.setLength(12);
    }
    TEST_ASSERT_TRUE(core::saveBerlinPreset2(st, 5, in4, 4));
    TEST_ASSERT_FALSE(core::berlinPreset2Usable(st, 5, 3));
    core::BerlinVoicePreset out3[3];
    TEST_ASSERT_FALSE(core::loadBerlinPreset2(st, 5, out3, 3));

    // Save with count 3, then query/load with count 4 → must fail.
    FakeStorage st2;
    core::BerlinVoicePreset in3[3];
    for (int v = 0; v < 3; ++v) {
        in3[v].params.length  = 10;
        in3[v].params.density = 40;
        in3[v].channel = static_cast<uint8_t>(v + 1);
        in3[v].seq.setLength(10);
    }
    TEST_ASSERT_TRUE(core::saveBerlinPreset2(st2, 2, in3, 3));
    TEST_ASSERT_FALSE(core::berlinPreset2Usable(st2, 2, 4));
    core::BerlinVoicePreset out4[4];
    TEST_ASSERT_FALSE(core::loadBerlinPreset2(st2, 2, out4, 4));
}

static void test_berlin_v1_blob_reads_as_empty() {
    FakeStorage st;
    st.data["berlin.s01"] = std::vector<uint8_t>(214, 0);  // v1-sized junk
    TEST_ASSERT_FALSE(core::berlinPreset2Usable(st, 0, 3));
    core::BerlinVoicePreset out[3];
    TEST_ASSERT_FALSE(core::loadBerlinPreset2(st, 0, out, 3));
}

static void test_corrupt_preset_loads_as_empty() {
    FakeStorage st;
    core::ArpParams p;
    TEST_ASSERT_TRUE(core::saveArpPreset(st, 2, p));
    // Bad magic.
    auto blob = st.data["arp.s03"];
    blob[0] = 'X';
    st.data["arp.s03"] = blob;
    core::ArpParams q;
    TEST_ASSERT_FALSE(core::loadArpPreset(st, 2, q));
    // Out-of-range field (steps = 99).
    blob[0] = 'M';
    blob[5] = 99;
    st.data["arp.s03"] = blob;
    TEST_ASSERT_FALSE(core::loadArpPreset(st, 2, q));
    // Wrong size.
    blob.pop_back();
    st.data["arp.s03"] = blob;
    TEST_ASSERT_FALSE(core::loadArpPreset(st, 2, q));
    // Out-of-bounds slot.
    TEST_ASSERT_FALSE(core::loadArpPreset(st, 20, q));
    TEST_ASSERT_FALSE(core::saveArpPreset(st, -1, p));
}

// ---------------------------------------------------------------------------
// PresetScreen state machine (against a scripted fake)
// ---------------------------------------------------------------------------

struct FakeOps : public core::PresetOps {
    bool used[core::kPresetSlots] = {};
    int  savedSlot = -1, loadedSlot = -1, deletedSlot = -1;
    bool failAll = false;
    bool presetUsed(int slot) override { return used[slot]; }
    bool savePreset(int slot) override {
        if (failAll) return false;
        savedSlot = slot; used[slot] = true; return true;
    }
    bool loadPreset(int slot) override {
        if (failAll) return false;
        loadedSlot = slot; return true;
    }
    bool deletePresetSlot(int slot) override {
        if (failAll) return false;
        deletedSlot = slot; used[slot] = false; return true;
    }
};

static void test_picker_open_rotate_confirm_save() {
    FakeOps ops;
    core::PresetScreen s(ops);
    s.update(1000);
    TEST_ASSERT_FALSE(s.pickerOpen());
    s.onEncoderSw(1);                                  // open Save
    TEST_ASSERT_TRUE(s.pickerOpen());
    s.onEncoder(1, +3);                                // slot 3
    s.onEncoder(2, -1);                                // any encoder rotates: slot 2
    TEST_ASSERT_EQUAL_INT(2, s.slot());
    s.onEncoderSw(1);                                  // confirm with the SAME encoder
    TEST_ASSERT_EQUAL_INT(2, ops.savedSlot);
    TEST_ASSERT_FALSE(s.pickerOpen());                 // save closes
}

static void test_picker_slot_wraps_and_is_remembered() {
    FakeOps ops;
    core::PresetScreen s(ops);
    s.update(1000);
    s.onEncoderSw(2);
    s.onEncoder(1, -1);                                // 0 -> 19 (wrap)
    TEST_ASSERT_EQUAL_INT(19, s.slot());
    s.onEncoderSw(3);                                  // different encoder: cancel
    TEST_ASSERT_FALSE(s.pickerOpen());
    s.onEncoderSw(1);                                  // reopen (Save)
    TEST_ASSERT_EQUAL_INT(19, s.slot());               // slot remembered
}

static void test_picker_other_press_cancels_without_action() {
    FakeOps ops;
    core::PresetScreen s(ops);
    s.update(1000);
    s.onEncoderSw(3);                                  // open Delete
    s.onEncoderSw(2);                                  // cancel
    TEST_ASSERT_FALSE(s.pickerOpen());
    TEST_ASSERT_EQUAL_INT(-1, ops.deletedSlot);
}

static void test_picker_timeout_cancels() {
    FakeOps ops;
    core::PresetScreen s(ops);
    s.update(1000);
    s.onEncoderSw(1);
    s.update(5999);                                    // < 5 s since open
    TEST_ASSERT_TRUE(s.pickerOpen());
    s.update(6000);                                    // timeout
    TEST_ASSERT_FALSE(s.pickerOpen());
}

static void test_load_empty_slot_is_noop_and_stays_open() {
    FakeOps ops;
    core::PresetScreen s(ops);
    s.update(1000);
    s.onEncoderSw(2);                                  // Load, slot 0 empty
    s.onEncoderSw(2);                                  // confirm
    TEST_ASSERT_EQUAL_INT(-1, ops.loadedSlot);         // nothing loaded
    TEST_ASSERT_TRUE(s.pickerOpen());                  // pick another slot
}

static void test_delete_stays_open_and_refreshes() {
    FakeOps ops;
    ops.used[5] = true;
    ops.used[6] = true;
    core::PresetScreen s(ops);
    s.update(1000);
    s.onEncoderSw(3);                                  // Delete
    s.onEncoder(1, +5);                                // slot 5
    s.onEncoderSw(3);                                  // confirm
    TEST_ASSERT_EQUAL_INT(5, ops.deletedSlot);
    TEST_ASSERT_TRUE(s.pickerOpen());                  // bulk cleanup
    s.onEncoder(1, +1);                                // slot 6
    s.onEncoderSw(3);
    TEST_ASSERT_EQUAL_INT(6, ops.deletedSlot);
}

static void test_render_idle_and_picker() {
    FakeOps ops;
    core::PresetScreen s(ops);
    s.update(1000);
    StubDisplay d;
    s.render(d);
    TEST_ASSERT_TRUE(d.drewText("SAVE"));
    TEST_ASSERT_TRUE(d.drewText("LOAD"));
    s.onEncoderSw(2);
    StubDisplay d2;
    s.render(d2);
    TEST_ASSERT_TRUE(d2.drewText("LOAD SLOT"));
    TEST_ASSERT_TRUE(d2.drewText("01"));
    TEST_ASSERT_TRUE(d2.drewText("20"));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_preset_key_naming);
    RUN_TEST(test_arp_preset_round_trip);
    RUN_TEST(test_berlin_preset2_round_trip_three_voices);
    RUN_TEST(test_berlin_preset2_round_trip_four_voices);
    RUN_TEST(test_berlin_preset_cross_count_reads_empty);
    RUN_TEST(test_berlin_v1_blob_reads_as_empty);
    RUN_TEST(test_corrupt_preset_loads_as_empty);
    RUN_TEST(test_picker_open_rotate_confirm_save);
    RUN_TEST(test_picker_slot_wraps_and_is_remembered);
    RUN_TEST(test_picker_other_press_cancels_without_action);
    RUN_TEST(test_picker_timeout_cancels);
    RUN_TEST(test_load_empty_slot_is_noop_and_stays_open);
    RUN_TEST(test_delete_stays_open_and_refreshes);
    RUN_TEST(test_render_idle_and_picker);
    return UNITY_END();
}
