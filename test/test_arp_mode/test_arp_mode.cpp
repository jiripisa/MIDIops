#include <unity.h>

#include "core/ArpTypes.h"
#include "core/MidiMessage.h"
#include "core/app/AppShell.h"
#include "core/modes/ArpMode.h"
#include "support/FakeMidiOutput.h"
#include "support/StubDisplay.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static core::MidiMessage makeNoteOn(uint8_t note, uint8_t vel, uint8_t ch = 1) {
    core::MidiMessage m;
    m.type    = core::MidiType::NoteOn;
    m.channel = ch;
    m.data1   = note;
    m.data2   = vel;
    return m;
}

static core::MidiMessage makeNoteOff(uint8_t note, uint8_t ch = 1) {
    core::MidiMessage m;
    m.type    = core::MidiType::NoteOff;
    m.channel = ch;
    m.data1   = note;
    m.data2   = 0;
    return m;
}

// ---------------------------------------------------------------------------
// setUp / tearDown (no globals needed here — each test is self-contained)
// ---------------------------------------------------------------------------
void setUp()    {}
void tearDown() {}

// ---------------------------------------------------------------------------
// test_arp_mode_screens
//   Build a shell + ArpMode, verify screen count and screen names.
// ---------------------------------------------------------------------------
static void test_arp_mode_screens() {
    core::AppShell shell;
    core::ArpMode  arp(shell);

    TEST_ASSERT_EQUAL_INT(4, arp.screenCount());
    TEST_ASSERT_EQUAL_STRING("params1", arp.screen(0).name());
    TEST_ASSERT_EQUAL_STRING("params2", arp.screen(1).name());
    TEST_ASSERT_EQUAL_STRING("worms",   arp.screen(2).name());
    TEST_ASSERT_EQUAL_STRING("notes",   arp.screen(3).name());
}

// ---------------------------------------------------------------------------
// test_param_edit_steps
//   Verify enc1 edits steps, with default+delta, and clamping at 1 and 16.
// ---------------------------------------------------------------------------
static void test_param_edit_steps() {
    core::AppShell shell;
    core::ArpMode  arp(shell);

    // Default steps == 3, +2 → 5
    arp.screen(0).onEncoder(1, +2);
    TEST_ASSERT_EQUAL_INT(5, static_cast<int>(arp.params().steps));

    // Clamp at 16
    arp.screen(0).onEncoder(1, +20);
    TEST_ASSERT_EQUAL_INT(16, static_cast<int>(arp.params().steps));

    // Clamp at 1
    arp.screen(0).onEncoder(1, -30);
    TEST_ASSERT_EQUAL_INT(1, static_cast<int>(arp.params().steps));
}

// ---------------------------------------------------------------------------
// test_arp_outgoing_visualised
//   End-to-end: NoteOn → engine emits notes → model has worms → render shows
//   them; also verify the FakeMidiOutput received NoteOn events.
//
//   bpm=120, rate=Sixteenth (default) → msPerStep = 125 ms
//   steps=3 (default) → one cycle = 3 * 125 = 375 ms
// ---------------------------------------------------------------------------
static void test_arp_outgoing_visualised() {
    core::AppShell    shell;
    core::ArpMode     arp(shell);
    FakeMidiOutput    out;
    StubDisplay       disp;

    arp.setMidiOutput(&out);
    shell.addMode(&arp);
    shell.begin();   // enters mode 0 (ArpMode), screen 0

    // Switch to the worms screen (screen 2) via enc5
    // enc5 +1 each time advances screen index by 1
    shell.onEncoderKnob(5, +1);   // screen 0 → 1
    shell.onEncoderKnob(5, +1);   // screen 1 → 2  (worms)

    // Trigger note 60 at t=0
    shell.tick(0);
    shell.onMidiIn(makeNoteOn(60, 100));

    // Tick across several step boundaries (125 ms each).
    // t=0 → NoteOn(60) emitted immediately by engine.
    // The ArpMode::onMidiIn is called with lastNowMs_=0 so the engine fires step 0.
    // We need update() to run via tick(); we already did tick(0) before noteOn;
    // fire a tick at t=0 again is ok — idempotent for the first call.
    // Then drive further steps.
    shell.tick(125);   // step 1 boundary → NoteOn(64) for C major Up triad
    shell.tick(250);   // step 2 boundary → NoteOn(67)
    shell.tick(375);   // cycle boundary → back to NoteOn(60) (loop while held)
    shell.tick(500);   // one more step

    // Render the worms screen
    shell.render(disp);

    // 1. The engine emitted at least one NoteOn event to the FakeMidiOutput.
    bool hasNoteOn = false;
    for (const auto& ev : out.events) {
        if (ev.isOn) { hasNoteOn = true; break; }
    }
    TEST_ASSERT_TRUE_MESSAGE(hasNoteOn, "Engine should have emitted NoteOn events");

    // 2. The worms screen drew some rects (keyboard + worms).
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, disp.rects, "WormsRenderer should have drawn rects");

    // 3. The top bar shows the mode name "Arp".
    TEST_ASSERT_TRUE_MESSAGE(disp.drewText("Arp"), "Top bar should include 'Arp'");
}

// ---------------------------------------------------------------------------
// test_param_edit_rate_cycles
//   enc2 on params1 cycles ArpRate; test cycling and wrap-around.
// ---------------------------------------------------------------------------
static void test_param_edit_rate_cycles() {
    core::AppShell shell;
    core::ArpMode  arp(shell);

    // Default rate = Sixteenth (index 3). +1 → SixteenthT (index 4)
    arp.screen(0).onEncoder(2, +1);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(core::ArpRate::SixteenthT),
                          static_cast<int>(arp.params().rate));

    // Cycle all the way to wrap: kCount=6, go +6 more → back to SixteenthT
    arp.screen(0).onEncoder(2, +static_cast<int>(core::ArpRate::kCount));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(core::ArpRate::SixteenthT),
                          static_cast<int>(arp.params().rate));
}

// ---------------------------------------------------------------------------
// test_param_edit_latch_toggle
//   enc4 on params2 toggles latch (any non-zero delta).
// ---------------------------------------------------------------------------
static void test_param_edit_latch_toggle() {
    core::AppShell shell;
    core::ArpMode  arp(shell);

    // Default latch = false
    TEST_ASSERT_FALSE(arp.params().latch);

    arp.screen(1).onEncoder(4, +1);
    TEST_ASSERT_TRUE(arp.params().latch);

    arp.screen(1).onEncoder(4, -1);
    TEST_ASSERT_FALSE(arp.params().latch);

    // delta == 0 should NOT toggle
    arp.screen(1).onEncoder(4, 0);
    TEST_ASSERT_FALSE(arp.params().latch);
}

// ---------------------------------------------------------------------------
// test_transport_stop_silences_engine
//   onTransport(Stop) calls engine_.stop() — engine is no longer playing.
// ---------------------------------------------------------------------------
static void test_transport_stop_silences_engine() {
    core::AppShell shell;
    core::ArpMode  arp(shell);
    FakeMidiOutput out;

    arp.setMidiOutput(&out);
    shell.addMode(&arp);
    shell.begin();

    shell.tick(0);
    shell.onMidiIn(makeNoteOn(60, 100));
    // Engine should now be playing
    TEST_ASSERT_TRUE(arp.params().steps > 0);  // sanity

    // Issue Stop transport
    arp.onTransport(core::Transport::Stop);

    // After stop, any NoteOff for the sounding note should have been emitted.
    // Just verify it doesn't crash and the engine is quiescent.
    shell.tick(200);
    // No assertion on event count — the key test is no crash + future ticks are silent.
    // Verify no new NoteOn events after stop (clear events, tick more, check).
    out.events.clear();
    shell.tick(400);
    shell.tick(600);
    bool newNoteOn = false;
    for (const auto& ev : out.events) {
        if (ev.isOn) { newNoteOn = true; break; }
    }
    TEST_ASSERT_FALSE_MESSAGE(newNoteOn,
        "After stop(), engine should not emit new NoteOn events");
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main() {
    UNITY_BEGIN();
    RUN_TEST(test_arp_mode_screens);
    RUN_TEST(test_param_edit_steps);
    RUN_TEST(test_arp_outgoing_visualised);
    RUN_TEST(test_param_edit_rate_cycles);
    RUN_TEST(test_param_edit_latch_toggle);
    RUN_TEST(test_transport_stop_silences_engine);
    return UNITY_END();
}
