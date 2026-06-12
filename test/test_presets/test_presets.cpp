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

static void test_berlin_preset_round_trip_includes_sequence() {
    FakeStorage st;
    core::BerlinParams p;
    p.algorithm = core::BerlinAlgorithm::DegreeWeighted;
    p.length = 21;
    p.density = 73;
    p.behavior = core::BerlinBehavior::Evolve;
    core::BerlinSequence seq;
    seq.setLength(21);
    for (int i = 0; i < 21; ++i) {
        core::BerlinStep& s = seq.step(i);
        s.active = (i % 3) != 2;
        s.note = static_cast<uint8_t>(48 + i);
        s.velocity = static_cast<uint8_t>(60 + i);
        s.accent = (i % 5) == 0;
        s.gateTicks = static_cast<uint16_t>(3 + (i % 9));
        s.velJitter = static_cast<int8_t>(i - 10);
    }
    TEST_ASSERT_TRUE(core::saveBerlinPreset(st, 0, p, seq));
    core::BerlinParams q;
    core::BerlinSequence out;
    TEST_ASSERT_TRUE(core::loadBerlinPreset(st, 0, q, out));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(core::BerlinAlgorithm::DegreeWeighted),
                          static_cast<int>(q.algorithm));
    TEST_ASSERT_EQUAL_INT(21, q.length);
    TEST_ASSERT_EQUAL_INT(73, q.density);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(core::BerlinBehavior::Evolve),
                          static_cast<int>(q.behavior));
    TEST_ASSERT_EQUAL_INT(21, out.length());
    for (int i = 0; i < 21; ++i) {
        TEST_ASSERT_EQUAL_INT(seq.step(i).active ? 1 : 0, out.step(i).active ? 1 : 0);
        TEST_ASSERT_EQUAL_INT(seq.step(i).note, out.step(i).note);
        TEST_ASSERT_EQUAL_INT(seq.step(i).velocity, out.step(i).velocity);
        TEST_ASSERT_EQUAL_INT(seq.step(i).accent ? 1 : 0, out.step(i).accent ? 1 : 0);
        TEST_ASSERT_EQUAL_INT(seq.step(i).gateTicks, out.step(i).gateTicks);
        TEST_ASSERT_EQUAL_INT(seq.step(i).velJitter, out.step(i).velJitter);
    }
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
    RUN_TEST(test_berlin_preset_round_trip_includes_sequence);
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
