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
//   them; also verify the FakeMidiOutput received multiple NoteOn events
//   (i.e. multiple arp steps actually fire and are visualised).
//
//   Default rate = Sixteenth → arpRateTicks(Sixteenth) = 6 ticks/step.
//   Default steps = 3 → one cycle = 3 steps.
//   The engine advances via the internal-clock tick drain: set
//   out.pendingTicks = 6 before each shell.tick() to cross one step boundary.
// ---------------------------------------------------------------------------
static void test_arp_outgoing_visualised() {
    core::AppShell    shell;
    core::ArpMode     arp(shell);
    FakeMidiOutput    out;
    StubDisplay       disp;

    arp.setMidiOutput(&out);
    shell.setMidiOutput(&out);   // wire clock tick drain to ArpEngine
    shell.addMode(&arp);
    shell.begin();   // enters mode 0 (ArpMode), screen 0

    // Switch to the worms screen (screen 2) via enc5.
    shell.onEncoderKnob(5, +1);   // screen 0 → 1
    shell.onEncoderKnob(5, +1);   // screen 1 → 2  (worms)

    // Trigger note 60 at t=0; step 0 fires immediately inside noteOn().
    shell.tick(0);
    shell.onMidiIn(makeNoteOn(60, 100));

    // Drive three more step boundaries (6 ticks each) so the engine advances
    // through multiple steps of the arpeggio.
    const int stepTicks = core::arpRateTicks(core::ArpRate::Sixteenth);  // 6

    out.pendingTicks = static_cast<uint32_t>(stepTicks);
    shell.tick(1);   // step 1 boundary → NoteOn(64)

    out.pendingTicks = static_cast<uint32_t>(stepTicks);
    shell.tick(2);   // step 2 boundary → NoteOn(67)

    out.pendingTicks = static_cast<uint32_t>(stepTicks);
    shell.tick(3);   // cycle boundary  → NoteOn(60) again (one-shot done, engine idle)

    // Render the worms screen.
    shell.render(disp);

    // 1. The engine emitted multiple NoteOn events (steps 0, 1, 2 at minimum).
    int noteOnCount = 0;
    for (const auto& ev : out.events) {
        if (ev.isOn) ++noteOnCount;
    }
    TEST_ASSERT_GREATER_THAN_MESSAGE(1, noteOnCount,
        "Engine should have emitted multiple NoteOn events across arp steps");

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
// test_param_edit_latch_enc4_noop
//   enc4 on params2 no longer controls latch (Latch is now Latch1 button).
//   Verify enc4 does nothing.
// ---------------------------------------------------------------------------
static void test_param_edit_latch_enc4_noop() {
    core::AppShell shell;
    core::ArpMode  arp(shell);

    // Default latch = false
    TEST_ASSERT_FALSE(arp.params().latch);

    // enc4 must no longer toggle latch
    arp.screen(1).onEncoder(4, +1);
    TEST_ASSERT_FALSE(arp.params().latch);

    arp.screen(1).onEncoder(4, -1);
    TEST_ASSERT_FALSE(arp.params().latch);
}

// ---------------------------------------------------------------------------
// test_arp_captures_transport
//   ArpMode::capturesTransport() must return true.
// ---------------------------------------------------------------------------
static void test_arp_captures_transport() {
    core::AppShell shell;
    core::ArpMode  arp(shell);
    TEST_ASSERT_TRUE(arp.capturesTransport());
}

// ---------------------------------------------------------------------------
// test_hold_via_latch1
//   Latch1 is a stateless CLICK toggle for Hold: each flip toggles, the switch
//   POSITION carries no meaning. The first delivery after onEnter() is absorbed.
// ---------------------------------------------------------------------------
static void test_hold_via_latch1() {
    core::AppShell shell;
    core::ArpMode  arp(shell);
    arp.onEnter();

    core::RawInput in{};
    in.kind  = core::RawInput::Kind::Latch;
    in.index = 1;
    in.delta = 0;

    in.on = false;
    arp.onRawInput(in);          // prime: absorb first delivery
    TEST_ASSERT_FALSE(arp.hold());

    in.on = true;
    arp.onRawInput(in);          // flip → hold ON
    TEST_ASSERT_TRUE(arp.hold());

    in.on = false;
    arp.onRawInput(in);          // flip → hold OFF
    TEST_ASSERT_FALSE(arp.hold());
}

// ---------------------------------------------------------------------------
// test_mute_via_latch2
//   Latch2 is a stateless CLICK toggle for Mute: each flip toggles, the switch
//   POSITION carries no meaning. The first delivery after onEnter() is absorbed.
// ---------------------------------------------------------------------------
static void test_mute_via_latch2() {
    core::AppShell shell;
    core::ArpMode  arp(shell);
    arp.onEnter();

    core::RawInput in{};
    in.kind  = core::RawInput::Kind::Latch;
    in.index = 2;
    in.delta = 0;

    in.on = false;
    arp.onRawInput(in);          // prime: absorb first delivery
    TEST_ASSERT_FALSE(arp.muted());

    in.on = true;
    arp.onRawInput(in);          // flip → mute ON
    TEST_ASSERT_TRUE(arp.muted());

    in.on = false;
    arp.onRawInput(in);          // flip → mute OFF
    TEST_ASSERT_FALSE(arp.muted());
}

// ---------------------------------------------------------------------------
// test_arp_hold_mute_click_toggle
//   Hold (Latch1) and Mute (Latch2) are stateless CLICK toggles: a flip toggles,
//   holding the SAME level across frames never re-triggers, and the switch
//   POSITION carries no meaning.
// ---------------------------------------------------------------------------
static void test_arp_hold_mute_click_toggle() {
    core::AppShell shell;
    core::ArpMode  arp(shell);
    arp.onEnter();

    // --- Hold (Latch1) ---
    arp.onRawInput({core::RawInput::Kind::Latch, 1, 0, false});  // prime
    TEST_ASSERT_FALSE(arp.hold());
    arp.onRawInput({core::RawInput::Kind::Latch, 1, 0, true});   // flip → ON
    TEST_ASSERT_TRUE(arp.hold());
    for (int i = 0; i < 8; ++i)                                  // same level — no re-trigger
        arp.onRawInput({core::RawInput::Kind::Latch, 1, 0, true});
    TEST_ASSERT_TRUE(arp.hold());
    arp.onRawInput({core::RawInput::Kind::Latch, 1, 0, false});  // flip → OFF
    TEST_ASSERT_FALSE(arp.hold());

    // --- Mute (Latch2) ---
    arp.onRawInput({core::RawInput::Kind::Latch, 2, 0, false});  // prime
    TEST_ASSERT_FALSE(arp.muted());
    arp.onRawInput({core::RawInput::Kind::Latch, 2, 0, true});   // flip → ON
    TEST_ASSERT_TRUE(arp.muted());
    for (int i = 0; i < 8; ++i)                                  // same level — no re-trigger
        arp.onRawInput({core::RawInput::Kind::Latch, 2, 0, true});
    TEST_ASSERT_TRUE(arp.muted());
    arp.onRawInput({core::RawInput::Kind::Latch, 2, 0, false});  // flip → OFF
    TEST_ASSERT_FALSE(arp.muted());

    // Direction-independence: prime ON, then a flip to OFF must TOGGLE Hold ON
    // (a level-driven impl would instead drive it OFF). Proves position-blindness.
    core::ArpMode arp2(shell);
    arp2.onEnter();
    arp2.onRawInput({core::RawInput::Kind::Latch, 1, 0, true});   // prime ON (absorbed)
    TEST_ASSERT_FALSE(arp2.hold());
    arp2.onRawInput({core::RawInput::Kind::Latch, 1, 0, false});  // flip ON→OFF toggles Hold ON
    TEST_ASSERT_TRUE(arp2.hold());
}

// ---------------------------------------------------------------------------
// test_reset_via_latch3
//   Latch3 (Reset) fires on ANY flip (both directions) — a stateless click.
//   The first delivery after onEnter() is absorbed; both subsequent flips
//   reset the engine and must not crash.
// ---------------------------------------------------------------------------
static void test_reset_via_latch3() {
    core::AppShell shell;
    core::ArpMode  arp(shell);
    arp.onEnter();

    core::RawInput in{};
    in.kind  = core::RawInput::Kind::Latch;
    in.index = 3;
    in.delta = 0;

    in.on = false;
    arp.onRawInput(in);  // prime: absorb first delivery

    in.on = true;
    arp.onRawInput(in);  // flip ON → engine_.reset() — must not crash

    in.on = false;
    arp.onRawInput(in);  // flip OFF → also resets (any flip) — must not crash
    // No crash == pass
    TEST_ASSERT_TRUE(true);
}

// ---------------------------------------------------------------------------
// test_arp_latch3_reset_is_edge_triggered (N8)
//   On hardware the shell delivers the Latch3 LEVEL every main-loop frame.
//   Latch3 (Reset) must fire on the RISING edge only — not every frame while
//   the switch sits ON, which would zero step timing faster than clock ticks
//   arrive and freeze the arp. We start the arp playing, then deliver
//   {Latch,3,0,true} on every frame interleaved with clock ticks, and assert
//   the arp still advances (new NoteOns keep appearing).
// ---------------------------------------------------------------------------
static void test_arp_latch3_reset_is_edge_triggered() {
    core::AppShell shell;
    core::ArpMode  arp(shell);
    FakeMidiOutput out;

    arp.setMidiOutput(&out);
    shell.setMidiOutput(&out);
    shell.addMode(&arp);
    shell.begin();

    shell.tick(0);
    arp.onMidiIn(makeNoteOn(60, 100));   // step 0 fires immediately

    // First Latch3 delivery after entry is absorbed (no reset); start counting.
    arp.onRawInput({core::RawInput::Kind::Latch, 3, 0, true});
    out.events.clear();

    const int stepTicks = core::arpRateTicks(core::ArpRate::Sixteenth);  // 6
    int noteOns = 0;
    // Mirror the hardware main loop: onLatch fires EVERY frame and one clock
    // tick is drained per frame. If Reset were level-triggered it would zero
    // the step-tick accumulator every frame, the boundary would never be
    // crossed, and the arp would freeze. Run for several steps' worth of frames.
    for (int frame = 0; frame < stepTicks * 4; ++frame) {
        arp.onRawInput({core::RawInput::Kind::Latch, 3, 0, true});  // held ON
        out.pendingTicks = 1;
        shell.tick(static_cast<uint32_t>(frame + 1));
        for (const auto& ev : out.events) if (ev.isOn) ++noteOns;
        out.events.clear();
    }
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, noteOns,
        "Latch3 held ON must not freeze the arp — it advances on clock ticks");
}

// ---------------------------------------------------------------------------
// test_arp_mode_exit_silences_engine
//   onExit() calls engine_.stop() — engine is no longer playing after mode exit.
// ---------------------------------------------------------------------------
static void test_arp_mode_exit_silences_engine() {
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

    // Exit the mode (simulates switching away) — engine_.stop() is called.
    arp.onExit();

    // After exit, verify no new NoteOn events are produced.
    out.events.clear();
    shell.tick(400);
    shell.tick(600);
    bool newNoteOn = false;
    for (const auto& ev : out.events) {
        if (ev.isOn) { newNoteOn = true; break; }
    }
    TEST_ASSERT_FALSE_MESSAGE(newNoteOn,
        "After onExit(), engine should not emit new NoteOn events");
}

// ---------------------------------------------------------------------------
// test_arp_uses_settings_out_channel
//   After shell.setMidiOutChannel(7), emitted NoteOn events should use ch 7.
// ---------------------------------------------------------------------------
static void test_arp_uses_settings_out_channel() {
    core::AppShell shell;
    core::ArpMode  arp(shell);
    FakeMidiOutput out;
    arp.setMidiOutput(&out);
    shell.addMode(&arp);
    shell.begin();
    shell.setMidiOutChannel(7);
    shell.tick(0);   // update() pushes channel 7 into the engine before noteOn
    core::MidiMessage on{};
    on.type    = core::MidiType::NoteOn;
    on.channel = 1;
    on.data1   = 60;
    on.data2   = 100;
    arp.onMidiIn(on);
    bool any = false;
    for (const auto& e : out.events) {
        if (e.isOn) { TEST_ASSERT_EQUAL_INT(7, e.channel); any = true; }
    }
    TEST_ASSERT_TRUE(any);
}

// ---------------------------------------------------------------------------
// test_arp_ticks_from_internal_clock
//   AppShell drains consumeClockTicks() → calls ArpMode::onClockTick() →
//   calls ArpEngine::onClockTick().  After one full step's worth of ticks
//   the engine should advance and emit a second NoteOn.
//
//   Default rate = Sixteenth → arpRateTicks(Sixteenth) = 6 ticks per step.
//   Step 0 NoteOn fires immediately on noteOn(); after 6 more ticks the step
//   boundary is crossed and step 1 NoteOn should appear.
// ---------------------------------------------------------------------------
static void test_arp_ticks_from_internal_clock() {
    core::AppShell shell;
    core::ArpMode  arp(shell);
    FakeMidiOutput out;

    arp.setMidiOutput(&out);
    shell.setMidiOutput(&out);
    shell.addMode(&arp);
    shell.begin();  // enters ArpMode

    // Confirm clock source is Internal (default).
    TEST_ASSERT_EQUAL_INT(static_cast<int>(core::ClockSource::Internal),
                          static_cast<int>(shell.clockSource()));

    // Push a note so the engine has a sequence; step 0 fires immediately.
    shell.tick(0);
    shell.onMidiIn(makeNoteOn(60, 100));

    // Step 0 NoteOn should have been emitted already (fires in noteOn()).
    int countBefore = 0;
    for (const auto& ev : out.events) { if (ev.isOn) ++countBefore; }
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, countBefore,
        "Step 0 NoteOn should fire immediately on noteOn()");

    // Load one full step's worth of ticks into pendingTicks.
    // Sixteenth rate = 6 ticks/step.
    const int stepTicks = core::arpRateTicks(core::ArpRate::Sixteenth);  // 6
    out.pendingTicks = static_cast<uint32_t>(stepTicks);

    // shell.tick() drains pendingTicks and calls onClockTick() N times.
    shell.tick(1);

    // After 6 ticks the engine should have crossed the step boundary and emitted
    // another NoteOn (step 1).
    int countAfter = 0;
    for (const auto& ev : out.events) { if (ev.isOn) ++countAfter; }
    TEST_ASSERT_GREATER_THAN_MESSAGE(countBefore, countAfter,
        "Engine should emit a second NoteOn after one full step's ticks");
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
    RUN_TEST(test_param_edit_latch_enc4_noop);
    RUN_TEST(test_arp_mode_exit_silences_engine);
    // Arp-mode transport capture + latch button wiring
    RUN_TEST(test_arp_captures_transport);
    RUN_TEST(test_hold_via_latch1);
    RUN_TEST(test_mute_via_latch2);
    RUN_TEST(test_arp_hold_mute_click_toggle);
    RUN_TEST(test_reset_via_latch3);
    RUN_TEST(test_arp_latch3_reset_is_edge_triggered);
    RUN_TEST(test_arp_uses_settings_out_channel);
    RUN_TEST(test_arp_ticks_from_internal_clock);
    return UNITY_END();
}
