#include <unity.h>
#include "core/MidiMessage.h"
#include "core/ClockFollower.h"
#include "core/app/AppShell.h"
#include "core/modes/BpmMode.h"
#include "core/modes/BerlinMode.h"
#include "support/Fakes.h"
#include "support/FakeMidiOutput.h"

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

static void test_follower_derives_120bpm() {
    core::ClockFollower f;
    // 120 BPM → beat = 500 ms → 24 pulses over 500 ms.
    for (int i = 0; i <= 24; ++i) f.onPulse(static_cast<uint32_t>(i * 500 / 24));
    TEST_ASSERT_INT_WITHIN(2, 120, f.bpm());
}

static void test_follower_clamps() {
    core::ClockFollower f;
    for (int i = 0; i <= 24; ++i) f.onPulse(static_cast<uint32_t>(i * 5));  // very fast → clamp 300
    TEST_ASSERT_EQUAL_INT(300, f.bpm());
}

static void test_external_clock_follows_without_echo() {
    core::AppShell shell;
    FakeMode a("a", 1);
    FakeMidiOutput out;
    shell.setMidiOutput(&out);
    shell.addMode(&a);
    shell.begin();
    shell.setClockSource(core::ClockSource::External);
    TEST_ASSERT_EQUAL_INT(0, out.lastBpm);        // internal stopped: setClockBpm(0).
    core::MidiMessage clk{}; clk.type = core::MidiType::Clock;
    shell.onMidiIn(clk);
    TEST_ASSERT_EQUAL_INT(1, a.clockTicks);       // active mode ticked once (follow)
    // The incoming clock is followed (mode ticked), NOT echoed back out: on a
    // single MIDI interface the host is the master, and re-emitting its own
    // clock floods usbMIDI TX and starves RX on the device. The output sees no
    // clock-related sends — only the setClockBpm(0) that stopped the internal
    // master when External was selected.
    TEST_ASSERT_EQUAL_INT(0, out.starts + out.continues + out.stops);
}

// After the external clock pauses and resumes, the first window must not report
// a bogus low BPM derived across the gap. A >500 ms silence re-anchors the
// window while keeping the last known bpm_.
static void test_follower_gap_reset() {
    core::ClockFollower f;
    // Steady 120 BPM (beat = 500 ms over 24 pulses) → bpm becomes ~120. Use
    // i*500/24 absolute timestamps so the window spans exactly 500 ms.
    uint32_t base = 0;
    for (int i = 0; i <= 24; ++i) f.onPulse(base + static_cast<uint32_t>(i * 500 / 24));
    TEST_ASSERT_INT_WITHIN(2, 120, f.bpm());

    // Pause: the clock stops for 5 seconds, then resume steady 120 BPM.
    base += 500 + 5000;
    // Immediately after the gap, the old tempo is retained (NOT a bogus ~30).
    f.onPulse(base);
    TEST_ASSERT_INT_WITHIN(2, 120, f.bpm());

    // After one full resume window bpm is still ~120, never having dipped
    // toward 30 because of the cross-gap interval.
    for (int i = 1; i <= 24; ++i) f.onPulse(base + static_cast<uint32_t>(i * 500 / 24));
    TEST_ASSERT_INT_WITHIN(2, 120, f.bpm());
}

// BpmMode tempo screen ignores encoder edits under an External clock source
// (BPM is followed, not set); Internal source allows edits.
static void test_bpm_readonly_under_external_clock() {
    core::AppShell shell;
    core::BpmMode bpm(shell);
    shell.addMode(&bpm);
    shell.begin();
    const uint16_t before = shell.bpm();

    shell.setClockSource(core::ClockSource::External);
    bpm.screen(0).onEncoder(1, +5);
    TEST_ASSERT_EQUAL_UINT16(before, shell.bpm());   // unchanged under External

    shell.setClockSource(core::ClockSource::Internal);
    bpm.screen(0).onEncoder(1, +5);
    TEST_ASSERT_EQUAL_UINT16(before + 5, shell.bpm());   // editable under Internal
}

// Document-by-test: switching clock source Internal→External mid-note.
//
// (a) Internal stopped: setClockBpm(0) → FakeMidiOutput::lastBpm == 0.
// (b) The sounding note's gate-off is DEFERRED until external pulses arrive
//     (v1 limitation documented in AppShell::setClockSource comment): the note
//     keeps sounding immediately after the switch, and NoteOff only arrives once
//     external Clock messages drive onClockTick().
// (c) Switching back to Internal resumes: setClockBpm(bpm) → lastBpm == bpm.
//
// Setup mirrors test_external_stop_silences_engine in test_berlin_mode.cpp.
static void test_clock_source_switch_mid_note() {
    core::AppShell shell;
    core::BerlinMode berlin(shell);
    FakeMidiOutput out;
    berlin.setMidiOutput(&out);
    shell.setMidiOutput(&out);
    shell.addMode(&berlin);
    shell.begin();
    // Shell starts on Internal clock; begin() implicitly sets up the BPM output.
    const uint16_t bpm = shell.bpm();   // default 120

    // Prime the first-frame latch absorb (BerlinMode absorbs the first delivery
    // per latch index after onEnter). Send OFF to consume the absorb, then ON to play.
    berlin.onRawInput({core::RawInput::Kind::Latch, 1, 0, false});
    berlin.onRawInput({core::RawInput::Kind::Latch, 1, 0, true});   // play
    TEST_ASSERT_TRUE(berlin.engine().isPlaying());

    // Drive the engine with direct clock ticks (internal path) until a note is
    // sounding. Step 0 is always active (both generators guarantee it), so the
    // first onClockTick() call has the gate open. We pump up to 96 ticks to be safe.
    for (int i = 0; i < 96 && berlin.engine().soundingNote() < 0; ++i)
        berlin.onClockTick();
    TEST_ASSERT_GREATER_OR_EQUAL_INT_MESSAGE(0, berlin.engine().soundingNote(),
        "a note must be sounding before switching clock source");

    // (a) Switch to External: internal clock master must stop (setClockBpm(0)).
    shell.setClockSource(core::ClockSource::External);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, out.lastBpm,
        "switching to External must call setClockBpm(0) to stop internal generation");

    // (b) Immediately after the switch the note is still sounding — the gate-off
    // is deferred until external pulses arrive (v1 limitation).
    TEST_ASSERT_GREATER_OR_EQUAL_INT_MESSAGE(0, berlin.engine().soundingNote(),
        "gate-off must be deferred until external clock pulses arrive");

    // Count NoteOffs before feeding external pulses.
    int noteOffsBefore = 0;
    for (const auto& ev : out.events) { if (!ev.isOn) ++noteOffsBefore; }

    // Feed external Clock messages through the shell (mirrors the live code path).
    // Each Clock → AppShell::onMidiIn → mode->onClockTick() → engine_.onClockTick().
    // The gate timer will fire the NoteOff within gateTicks pulses.
    core::MidiMessage clk{}; clk.type = core::MidiType::Clock;
    for (int i = 0; i < 96; ++i) shell.onMidiIn(clk);

    int noteOffsAfter = 0;
    for (const auto& ev : out.events) { if (!ev.isOn) ++noteOffsAfter; }
    TEST_ASSERT_GREATER_THAN_MESSAGE(noteOffsBefore, noteOffsAfter,
        "NoteOff must arrive once external clock pulses drive the gate timer");

    // (c) Switch back to Internal: internal master must resume at the current BPM.
    shell.setClockSource(core::ClockSource::Internal);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(bpm, out.lastBpm,
        "switching back to Internal must call setClockBpm(bpm) to resume internal generation");
}

// Receive: incoming Start/Continue/Stop drive Berlin playback, consumed (never
// re-emitted). Start plays from step 0; Stop pauses + silences keeping position;
// Continue resumes from the held position.
static void test_receive_start_continue_stop_drive_berlin() {
    core::AppShell shell;
    core::BerlinMode berlin(shell);
    FakeMidiOutput out;
    berlin.setMidiOutput(&out);
    shell.setMidiOutput(&out);
    shell.addMode(&berlin);
    shell.begin();
    shell.setTransportMode(core::TransportMode::Receive);   // INTERNAL clock (default)

    auto countOn  = [&] { int n = 0; for (auto& e : out.events) if (e.isOn) ++n; return n; };
    auto countOff = [&] { int n = 0; for (auto& e : out.events) if (!e.isOn) ++n; return n; };

    // Incoming Start → Reset+Play: engine plays from step 0 and fires its NoteOn.
    core::MidiMessage start{}; start.type = core::MidiType::Start;
    shell.onMidiIn(start);
    TEST_ASSERT_TRUE(berlin.engine().isPlaying());
    TEST_ASSERT_EQUAL_INT(0, berlin.engine().playhead());
    TEST_ASSERT_GREATER_THAN(0, countOn());                 // step 0 NoteOn emitted

    // Advance via the internal-clock tick path (pendingTicks + tick) one pulse
    // at a time until the playhead has moved off step 0 AND a note is sounding,
    // so the upcoming Stop has a note to silence.
    for (int i = 0; i < 96 &&
         (berlin.engine().playhead() == 0 || berlin.engine().soundingNote() < 0); ++i) {
        out.pendingTicks = 1;
        shell.tick(1000 + i);
    }
    TEST_ASSERT_GREATER_THAN(0, berlin.engine().playhead());
    TEST_ASSERT_GREATER_OR_EQUAL(0, berlin.engine().soundingNote());
    const int heldPlayhead = berlin.engine().playhead();

    // Incoming Stop → Pause: not playing, an immediate NoteOff, playhead kept.
    const int offBefore = countOff();
    core::MidiMessage stop{}; stop.type = core::MidiType::Stop;
    shell.onMidiIn(stop);
    TEST_ASSERT_FALSE(berlin.engine().isPlaying());
    TEST_ASSERT_GREATER_THAN(offBefore, countOff());        // silenced immediately
    TEST_ASSERT_GREATER_THAN(0, berlin.engine().playhead());
    TEST_ASSERT_EQUAL_INT(heldPlayhead, berlin.engine().playhead());

    // Incoming Continue → Play from the held position (not rewound to 0).
    core::MidiMessage cont{}; cont.type = core::MidiType::Continue;
    shell.onMidiIn(cont);
    TEST_ASSERT_TRUE(berlin.engine().isPlaying());
    TEST_ASSERT_EQUAL_INT(heldPlayhead, berlin.engine().playhead());

    // Nothing was re-emitted downstream: Receive consumes transport.
    TEST_ASSERT_EQUAL_INT(0, out.starts + out.continues + out.stops);
}

// Under Send (default), incoming transport is ignored and nothing is re-emitted.
static void test_transport_ignored_unless_receive() {
    core::AppShell shell;
    core::BerlinMode berlin(shell);
    FakeMidiOutput out;
    berlin.setMidiOutput(&out);
    shell.setMidiOutput(&out);
    shell.addMode(&berlin);
    shell.begin();                                          // default Send, Internal clock

    core::MidiMessage start{}; start.type = core::MidiType::Start;
    shell.onMidiIn(start);
    TEST_ASSERT_FALSE(berlin.engine().isPlaying());         // Berlin stays stopped
    TEST_ASSERT_EQUAL_INT(0, out.starts + out.continues + out.stops);  // nothing re-emitted
}

// Safety under Off + External clock: an incoming Stop still silences the engine
// (so a tick-scheduled gate-off cannot hang), and nothing is re-emitted.
static void test_external_stop_safety_under_off() {
    core::AppShell shell;
    core::BerlinMode berlin(shell);
    FakeMidiOutput out;
    berlin.setMidiOutput(&out);
    shell.setMidiOutput(&out);
    shell.addMode(&berlin);
    shell.begin();
    shell.setTransportMode(core::TransportMode::Off);
    shell.setClockSource(core::ClockSource::External);

    // Play Berlin and drive it (via external Clock pulses) until a note sounds.
    berlin.onRawInput({core::RawInput::Kind::Latch, 1, 0, false});   // absorb first delivery
    berlin.onRawInput({core::RawInput::Kind::Latch, 1, 0, true});    // play
    core::MidiMessage clk{}; clk.type = core::MidiType::Clock;
    for (int i = 0; i < 96 && berlin.engine().soundingNote() < 0; ++i) shell.onMidiIn(clk);
    TEST_ASSERT_GREATER_OR_EQUAL(0, berlin.engine().soundingNote());

    int offBefore = 0;
    for (const auto& e : out.events) { if (!e.isOn) ++offBefore; }

    core::MidiMessage stop{}; stop.type = core::MidiType::Stop;
    shell.onMidiIn(stop);

    int offAfter = 0;
    for (const auto& e : out.events) { if (!e.isOn) ++offAfter; }
    TEST_ASSERT_GREATER_THAN(offBefore, offAfter);          // silenced
    TEST_ASSERT_EQUAL_INT(0, out.stops);                    // nothing re-emitted
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_realtime_types_are_not_channel_voice);
    RUN_TEST(test_follower_derives_120bpm);
    RUN_TEST(test_follower_clamps);
    RUN_TEST(test_external_clock_follows_without_echo);
    RUN_TEST(test_follower_gap_reset);
    RUN_TEST(test_bpm_readonly_under_external_clock);
    RUN_TEST(test_clock_source_switch_mid_note);
    RUN_TEST(test_receive_start_continue_stop_drive_berlin);
    RUN_TEST(test_transport_ignored_unless_receive);
    RUN_TEST(test_external_stop_safety_under_off);
    return UNITY_END();
}
