#include <unity.h>

#include "core/Scale.h"
#include "core/app/AppShell.h"
#include "core/modes/BpmMode.h"
#include "core/modes/DebugMode.h"
#include "support/Fakes.h"
#include "support/FakeStorage.h"
#include "support/StubDisplay.h"
#include "support/FakeMidiOutput.h"

void setUp() {}
void tearDown() {}

static void test_fake_mode_screen_dispatch() {
    FakeMode m("arp", 2);
    TEST_ASSERT_EQUAL_INT(2, m.screenCount());
    m.screen(0).onEncoder(1, +1);
    auto& fs = static_cast<FakeScreen&>(m.screen(0));
    TEST_ASSERT_EQUAL_INT(1, static_cast<int>(fs.encoders.size()));
}

static void test_enc1to4_route_to_active_screen() {
    core::AppShell shell;
    FakeMode a("a", 2), b("b", 1);
    shell.addMode(&a); shell.addMode(&b);
    shell.begin();
    shell.onEncoderKnob(1, +3);
    shell.onEncoderSw(2);
    auto& s0 = static_cast<FakeScreen&>(a.screen(0));
    TEST_ASSERT_EQUAL_INT(1, static_cast<int>(s0.encoders.size()));
    TEST_ASSERT_EQUAL_INT(3, s0.encoders[0].second);
    TEST_ASSERT_EQUAL_INT(1, static_cast<int>(s0.sws.size()));
    TEST_ASSERT_EQUAL_INT(1, s0.encoders[0].first);  // index preserved
    TEST_ASSERT_EQUAL_INT(2, s0.sws[0]);             // switch index preserved
}

static void test_enc5_switches_screen_with_wrap() {
    core::AppShell shell;
    FakeMode a("a", 3);
    shell.addMode(&a);
    shell.begin();
    TEST_ASSERT_EQUAL_INT(0, shell.activeScreenIndex());
    shell.onEncoderKnob(5, +1);
    TEST_ASSERT_EQUAL_INT(1, shell.activeScreenIndex());
    shell.onEncoderKnob(5, -1);
    shell.onEncoderKnob(5, -1);
    TEST_ASSERT_EQUAL_INT(2, shell.activeScreenIndex());  // wrapped past 0
    shell.onEncoderKnob(5, +1);                       // 2 -> 0, forward wrap
    TEST_ASSERT_EQUAL_INT(0, shell.activeScreenIndex());
    // Each screen switch must fire the lifecycle pair on the screens involved.
    auto& s1 = static_cast<FakeScreen&>(a.screen(1));
    TEST_ASSERT_TRUE(s1.enters >= 1);
    TEST_ASSERT_TRUE(s1.exits  >= 1);
}

static void test_midi_in_reaches_active_mode() {
    core::AppShell shell;
    FakeMode a("a", 1);
    shell.addMode(&a);
    shell.begin();
    core::MidiMessage m{};
    shell.onMidiIn(m);
    TEST_ASSERT_EQUAL_INT(1, a.midiCount);
}

static void test_raw_input_tap_fires_for_all_controls() {
    core::AppShell shell;
    FakeMode a("a", 1);
    shell.addMode(&a);
    shell.begin();
    shell.onEncoderKnob(5, +1);
    shell.onLatch(2, true);
    TEST_ASSERT_EQUAL_INT(2, a.rawCount);
}

static void test_overlay_open_select_confirm() {
    core::AppShell shell;
    FakeMode a("a", 1), b("b", 1), c("c", 1);
    shell.addMode(&a); shell.addMode(&b); shell.addMode(&c);
    shell.begin();
    shell.onEncoderSw(5);                       // open overlay
    TEST_ASSERT_TRUE(shell.overlayOpen());
    shell.onEncoderKnob(5, +2);                 // Enc5 selects index 2 (c)
    TEST_ASSERT_EQUAL_INT(2, shell.overlayChoice());
    shell.onEncoderSw(5);                        // confirm
    TEST_ASSERT_FALSE(shell.overlayOpen());
    TEST_ASSERT_EQUAL_INT(2, shell.activeModeIndex());
    TEST_ASSERT_EQUAL_INT(1, c.enters);          // entered once on confirm
}

static void test_overlay_timeout_reverts() {
    core::AppShell shell;
    FakeMode a("a", 1), b("b", 1);
    shell.addMode(&a); shell.addMode(&b);
    shell.begin();
    shell.tick(1000);
    shell.onEncoderSw(5);                        // open at t=1000
    shell.onEncoderKnob(5, +1);                  // Enc5 selects b at t=1000
    shell.tick(1000 + 3000);                      // exactly timeout
    TEST_ASSERT_FALSE(shell.overlayOpen());
    TEST_ASSERT_EQUAL_INT(0, shell.activeModeIndex());  // unchanged
    TEST_ASSERT_EQUAL_INT(0, b.enters);
}

static void test_overlay_rotation_resets_timeout() {
    core::AppShell shell;
    FakeMode a("a", 1), b("b", 1);
    shell.addMode(&a); shell.addMode(&b);
    shell.begin();
    shell.tick(1000);
    shell.onEncoderSw(5);
    shell.tick(3500);                             // overlay still open (opened at 1000, <4000)
    shell.onEncoderKnob(5, +1);                   // Enc5 rotate at t=3500 resets timer
    shell.tick(3500 + 2999);                       // <3s since last rotate
    TEST_ASSERT_TRUE(shell.overlayOpen());
}

// ---------------------------------------------------------------------------
// Overlay carousel animation: the tape position (1/256 index units) eases
// toward the choice instead of jumping, and settles exactly on it.
// ---------------------------------------------------------------------------
static void test_overlay_carousel_animates_toward_choice() {
    core::AppShell shell;
    FakeMode a("a", 1), b("b", 1), c("c", 1);
    shell.addMode(&a); shell.addMode(&b); shell.addMode(&c);
    shell.begin();
    shell.tick(1000);
    shell.onEncoderSw(5);                          // open: tape parked on choice
    TEST_ASSERT_EQUAL_INT(0, shell.overlayAnimPos256());
    shell.onEncoderKnob(5, +1);                    // choice 1 -> target 256
    shell.tick(1010);                              // one 10 ms tick: partway only
    const int mid = shell.overlayAnimPos256();
    TEST_ASSERT_TRUE(mid > 0 && mid < 256);
    for (uint32_t t = 1020; t <= 2000; t += 10) shell.tick(t);
    TEST_ASSERT_EQUAL_INT(256, shell.overlayAnimPos256());   // settled exactly
}

// Rotating one detent backwards from index 0 must slide through the wrap
// (0 -> n-1 the short way), not travel forward across the whole row.
static void test_overlay_carousel_wraps_shortest_path() {
    core::AppShell shell;
    FakeMode a("a", 1), b("b", 1), c("c", 1);
    shell.addMode(&a); shell.addMode(&b); shell.addMode(&c);
    shell.begin();
    shell.tick(1000);
    shell.onEncoderSw(5);
    shell.onEncoderKnob(5, -1);                    // choice 2 -> target 512 of span 768
    shell.tick(1010);
    TEST_ASSERT_TRUE(shell.overlayAnimPos256() > 512);   // moved backwards via wrap
    for (uint32_t t = 1020; t <= 2000; t += 10) shell.tick(t);
    TEST_ASSERT_EQUAL_INT(512, shell.overlayAnimPos256());
}

static void test_latch1_toggles_play_pause_and_sends_realtime() {
    core::AppShell shell;
    FakeMidiOutput out;
    FakeMode a("a", 1);
    shell.addMode(&a);
    shell.setMidiOutput(&out);
    shell.begin();
    shell.onLatch(1, false);                 // prime: first delivery per index is absorbed
    shell.onLatch(1, true);                  // Play (rising)
    TEST_ASSERT_EQUAL_INT(1, out.starts);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(core::Transport::Play), static_cast<int>(a.transports.back()));
    shell.onLatch(1, false);                 // change -> Pause
    TEST_ASSERT_EQUAL_INT(1, out.stops);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(core::Transport::Pause), static_cast<int>(a.transports.back()));
    shell.onLatch(1, true);                  // change -> Play (continue)
    TEST_ASSERT_EQUAL_INT(1, out.continues);
}

static void test_latch2_stop_latch3_reset() {
    core::AppShell shell;
    FakeMidiOutput out;
    FakeMode a("a", 1);
    shell.addMode(&a);
    shell.setMidiOutput(&out);
    shell.begin();
    shell.onLatch(2, false);                 // prime: absorb first delivery for index 2
    shell.onLatch(3, false);                 // prime: absorb first delivery for index 3
    shell.onLatch(2, true);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(core::Transport::Stop), static_cast<int>(a.transports.back()));
    shell.onLatch(3, true);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(core::Transport::Reset), static_cast<int>(a.transports.back()));
    TEST_ASSERT_EQUAL_INT(2, out.stops);     // stop + reset both send 0xFC
}

// ---------------------------------------------------------------------------
// test_capturing_mode_skips_global_transport
//   A mode with capturesTransport_=true must NOT trigger global transport
//   (i.e., no MIDI Start sent), but MUST still receive the raw latch event.
// ---------------------------------------------------------------------------
static void test_capturing_mode_skips_global_transport() {
    core::AppShell shell;
    FakeMidiOutput out;
    FakeMode fm("arp", 1);
    fm.capturesTransport_ = true;

    shell.setMidiOutput(&out);
    shell.addMode(&fm);
    shell.begin();

    // Latch 1 rising edge: in non-capturing mode this would send MIDI Start.
    // In capturing mode, shell skips global transport but mode still sees the raw event.
    shell.onLatch(1, true);

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, out.starts,
        "capturing mode: no MIDI Start should be sent by the shell");
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, fm.rawCount,
        "capturing mode: mode must still receive the raw latch event");
}

// ---------------------------------------------------------------------------
// test_non_capturing_mode_sends_global_transport (regression guard)
//   Confirm existing behaviour: a non-capturing mode still triggers MIDI Start.
// ---------------------------------------------------------------------------
static void test_non_capturing_mode_sends_global_transport() {
    core::AppShell shell;
    FakeMidiOutput out;
    FakeMode fm("mon", 1);
    // capturesTransport_ defaults to false

    shell.setMidiOutput(&out);
    shell.addMode(&fm);
    shell.begin();

    shell.onLatch(1, false);                 // prime: absorb first delivery for index 1
    shell.onLatch(1, true);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, out.starts,
        "non-capturing mode: MIDI Start must be sent");
}

// ---------------------------------------------------------------------------
// test_boot_with_latch_on_no_phantom_transport (N7)
//   On hardware the shell receives the latch LEVEL every main-loop frame. A
//   switch that is physically ON at boot must NOT fire a phantom MIDI Start on
//   the first frame (lastLatchOn_ starts false, so the first delivery looks
//   like an edge unless the first delivery is absorbed). A subsequent REAL
//   change (off → on) DOES Play.
// ---------------------------------------------------------------------------
static void test_boot_with_latch_on_no_phantom_transport() {
    core::AppShell shell;
    FakeMidiOutput out;
    FakeMode a("mon", 1);   // non-capturing
    shell.setMidiOutput(&out);
    shell.addMode(&a);
    shell.begin();

    // Latch1 physically ON, delivered every frame from boot.
    for (int i = 0; i < 5; ++i) shell.onLatch(1, true);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, out.starts,
        "boot-with-ON: first-frame absorb means no phantom Start");

    // A real change: release then re-assert → genuine rising edge → Play.
    shell.onLatch(1, false);
    shell.onLatch(1, true);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, out.starts,
        "a real off→on edge after absorb must Play");
}

// ---------------------------------------------------------------------------
// test_leave_capturing_mode_no_phantom_transport (N7)
//   Hold Latch1 ON in a capturing mode (shell suppresses global transport but
//   never updates its shadow). Switch via the overlay to a non-capturing mode
//   while the latch is still held ON across frames → the shell must NOT see a
//   false edge and fire a phantom Start.
// ---------------------------------------------------------------------------
static void test_leave_capturing_mode_no_phantom_transport() {
    core::AppShell shell;
    FakeMidiOutput out;
    FakeMode cap("arp", 1);   // capturing
    cap.capturesTransport_ = true;
    FakeMode mon("mon", 1);   // non-capturing
    shell.setMidiOutput(&out);
    shell.addMode(&cap);
    shell.addMode(&mon);
    shell.begin();            // active = cap (index 0)

    // Latch1 held ON while in the capturing mode — shell suppresses transport.
    for (int i = 0; i < 5; ++i) shell.onLatch(1, true);
    TEST_ASSERT_EQUAL_INT(0, out.starts);

    // Switch to the non-capturing mode via the overlay.
    shell.onEncoderSw(5);          // open overlay
    shell.onEncoderKnob(5, +1);    // select index 1 (mon)
    shell.onEncoderSw(5);          // confirm → enterMode(1)
    TEST_ASSERT_EQUAL_INT(1, shell.activeModeIndex());

    // Keep delivering the held-ON level — must NOT fire a phantom Start.
    for (int i = 0; i < 5; ++i) shell.onLatch(1, true);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, out.starts,
        "leaving a capturing mode with latch held ON must not phantom-Play");
}

// ---------------------------------------------------------------------------
// test_global_latch_transport_gated_by_mode
//   The global Latch1 Play still updates the playback state (mode notified),
//   but emits a MIDI Start only when TransportMode == Send. Under Off and
//   Receive nothing reaches the wire.
// ---------------------------------------------------------------------------
static void test_global_latch_transport_gated_by_mode() {
    // Off: state changes, no Start on the wire.
    {
        core::AppShell shell;
        FakeMidiOutput out;
        FakeMode a("mon", 1);          // non-capturing
        shell.setMidiOutput(&out);
        shell.addMode(&a);
        shell.begin();
        shell.setTransportMode(core::TransportMode::Off);
        shell.onLatch(1, false);       // prime: absorb first delivery
        shell.onLatch(1, true);        // Play
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, out.starts, "Off: no MIDI Start");
        TEST_ASSERT_EQUAL_INT(static_cast<int>(core::Transport::Play),
                              static_cast<int>(a.transports.back()));  // mode still notified
    }
    // Receive: state changes, no Start on the wire.
    {
        core::AppShell shell;
        FakeMidiOutput out;
        FakeMode a("mon", 1);
        shell.setMidiOutput(&out);
        shell.addMode(&a);
        shell.begin();
        shell.setTransportMode(core::TransportMode::Receive);
        shell.onLatch(1, false);       // prime
        shell.onLatch(1, true);        // Play
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, out.starts, "Receive: no MIDI Start");
        TEST_ASSERT_EQUAL_INT(static_cast<int>(core::Transport::Play),
                              static_cast<int>(a.transports.back()));
    }
}

// ---------------------------------------------------------------------------
// Settings persistence (design: docs/specs/2026-06-12-settings-persistence.md)
//   Blob "settings": 'M','O','P','S', version 1, scaleType, scaleRoot, outCh,
//   inCh, clockSource, transportMode, bpm lo, bpm hi (13 bytes).
// ---------------------------------------------------------------------------
static void seedSettingsBlob(FakeStorage& st, uint8_t scaleType, uint8_t root,
                             uint8_t outCh, uint8_t inCh, uint8_t clock,
                             uint8_t transport, uint16_t bpm,
                             uint8_t version = 1, char magic0 = 'M') {
    st.data["settings"] = {
        static_cast<uint8_t>(magic0), 'O', 'P', 'S', version,
        scaleType, root, outCh, inCh, clock, transport,
        static_cast<uint8_t>(bpm & 0xFF), static_cast<uint8_t>(bpm >> 8)};
}

static void test_settings_loaded_at_boot() {
    FakeStorage st;
    seedSettingsBlob(st, /*Minor*/1, /*root D*/2, /*out*/5, /*in*/3,
                     /*Internal*/0, /*Receive*/2, /*bpm*/96);
    core::AppShell shell;
    FakeMode a("a", 1);
    shell.addMode(&a);
    shell.setStorage(&st);
    shell.begin();
    TEST_ASSERT_EQUAL_INT(5, shell.midiOutChannel());
    TEST_ASSERT_EQUAL_INT(3, shell.midiInChannel());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(core::Scale::Type::Minor),
                          static_cast<int>(shell.scale().type()));
    TEST_ASSERT_EQUAL_INT(2, shell.scale().root());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(core::TransportMode::Receive),
                          static_cast<int>(shell.transportMode()));
    TEST_ASSERT_EQUAL_INT(96, shell.bpm());
}

static void test_corrupt_or_out_of_range_blob_keeps_defaults() {
    // Bad magic, bad version, and an out-of-range field must each leave the
    // defaults fully untouched (never a partial apply).
    FakeStorage bad1, bad2, bad3;
    seedSettingsBlob(bad1, 1, 2, 5, 3, 0, 2, 96, /*version*/1, /*magic*/'X');
    seedSettingsBlob(bad2, 1, 2, 5, 3, 0, 2, 96, /*version*/9);
    seedSettingsBlob(bad3, 1, 2, /*outCh out of range*/0, 3, 0, 2, 96);
    FakeStorage* cases[3] = {&bad1, &bad2, &bad3};
    for (FakeStorage* st : cases) {
        core::AppShell shell;
        FakeMode a("a", 1);
        shell.addMode(&a);
        shell.setStorage(st);
        shell.begin();
        TEST_ASSERT_EQUAL_INT(1, shell.midiOutChannel());
        TEST_ASSERT_EQUAL_INT(0, shell.midiInChannel());
        TEST_ASSERT_EQUAL_INT(static_cast<int>(core::Scale::Type::Major),
                              static_cast<int>(shell.scale().type()));
        TEST_ASSERT_EQUAL_INT(120, shell.bpm());
    }
}

static void test_out_of_range_scale_type_keeps_default() {
    // scaleType = 255 is way beyond the highest valid enum value — shell must
    // reject the entire blob and keep all defaults.
    FakeStorage st;
    seedSettingsBlob(st, /*scaleType*/255, /*root*/0, /*outCh*/1, /*inCh*/0,
                     /*clock*/0, /*transport*/1, /*bpm*/120);
    core::AppShell shell;
    FakeMode a("a", 1);
    shell.addMode(&a);
    shell.setStorage(&st);
    shell.begin();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(core::Scale::Type::Major),
                          static_cast<int>(shell.scale().type()));
}

static void test_settings_edit_saves_once_after_debounce() {
    FakeStorage st;
    core::AppShell shell;
    FakeMode a("a", 1);
    shell.addMode(&a);
    shell.setStorage(&st);
    shell.begin();
    shell.tick(1000);
    shell.setMidiOutChannel(7);                     // dirty at t=1000
    shell.tick(1100);
    TEST_ASSERT_EQUAL_INT(0, st.saves);             // not immediate
    shell.tick(2999);
    TEST_ASSERT_EQUAL_INT(0, st.saves);             // still inside the window
    shell.tick(3000);
    TEST_ASSERT_EQUAL_INT(1, st.saves);             // exactly one write
    shell.tick(9000);
    TEST_ASSERT_EQUAL_INT(1, st.saves);             // no repeats
    // The written blob round-trips.
    core::AppShell shell2;
    FakeMode b("b", 1);
    shell2.addMode(&b);
    shell2.setStorage(&st);
    shell2.begin();
    TEST_ASSERT_EQUAL_INT(7, shell2.midiOutChannel());
}

static void test_settings_rapid_edits_collapse_to_single_save() {
    FakeStorage st;
    core::AppShell shell;
    FakeMode a("a", 1);
    shell.addMode(&a);
    shell.setStorage(&st);
    shell.begin();
    shell.tick(1000);
    shell.setMidiOutChannel(7);                     // dirty at 1000
    shell.tick(2000);
    shell.setMidiOutChannel(8);                     // dirty re-stamped at 2000
    shell.tick(3500);
    TEST_ASSERT_EQUAL_INT(0, st.saves);             // window restarted
    shell.tick(4000);
    TEST_ASSERT_EQUAL_INT(1, st.saves);
}

static void test_factory_reset_restores_defaults_and_erases() {
    FakeStorage st;
    seedSettingsBlob(st, 1, 2, 5, 3, 0, 2, 96);
    core::AppShell shell;
    FakeMode a("a", 1);
    shell.addMode(&a);
    shell.setStorage(&st);
    shell.begin();
    TEST_ASSERT_EQUAL_INT(5, shell.midiOutChannel());   // stored applied
    shell.tick(1000);
    shell.factoryReset();
    TEST_ASSERT_EQUAL_INT(1, shell.midiOutChannel());
    TEST_ASSERT_EQUAL_INT(0, shell.midiInChannel());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(core::Scale::Type::Major),
                          static_cast<int>(shell.scale().type()));
    TEST_ASSERT_EQUAL_INT(0, shell.scale().root());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(core::TransportMode::Send),
                          static_cast<int>(shell.transportMode()));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(core::ClockSource::Internal),
                          static_cast<int>(shell.clockSource()));
    TEST_ASSERT_EQUAL_INT(120, shell.bpm());
    TEST_ASSERT_TRUE(st.removes >= 1);
    TEST_ASSERT_TRUE(st.data.find("settings") == st.data.end());
    shell.tick(9000);
    TEST_ASSERT_EQUAL_INT(0, st.saves);             // reset is not "dirty"
}

static void test_render_draws_top_bar_with_mode_and_screen() {
    core::AppShell shell;
    FakeMode a("mon", 2);
    shell.addMode(&a);
    shell.begin();
    StubDisplay d;
    shell.render(d);
    TEST_ASSERT_TRUE(d.drewText("mon"));
    TEST_ASSERT_EQUAL_INT(1, d.presents);
}

static void test_render_overlay_lists_modes() {
    core::AppShell shell;
    FakeMode a("a", 1), b("berlin", 1);
    shell.addMode(&a); shell.addMode(&b);
    shell.begin();
    shell.onEncoderSw(5);            // open overlay
    StubDisplay d;
    shell.render(d);
    TEST_ASSERT_TRUE(d.drewText("berlin"));
}

static void test_debug_mode_counts_raw_input() {
    core::AppShell shell;
    core::DebugMode dbg;
    shell.addMode(&dbg);
    shell.begin();
    shell.onEncoderKnob(5, +1);   // Enc5 rotate also reaches raw tap
    shell.onEncoderSw(3);
    shell.onLatch(2, true);
    StubDisplay d;
    shell.render(d);
    TEST_ASSERT_TRUE(d.drewText("ENC5"));
    TEST_ASSERT_TRUE(d.drewText("LATCH2"));
}

// ---------------------------------------------------------------------------
// test_debug_mode_latch_counts_flips_not_frames (Minor)
//   On hardware the shell delivers the latch LEVEL every main-loop frame, so a
//   per-frame counter spins uselessly. The Debug latch counter must count only
//   state CHANGES (flips). Deliver Latch2 ON 3×, then OFF 2× = exactly 2 flips.
// ---------------------------------------------------------------------------
static void test_debug_mode_latch_counts_flips_not_frames() {
    core::AppShell shell;
    core::DebugMode dbg;
    shell.addMode(&dbg);
    shell.begin();
    for (int i = 0; i < 3; ++i) shell.onLatch(2, true);   // 1 flip (off→on)
    for (int i = 0; i < 2; ++i) shell.onLatch(2, false);  // 1 flip (on→off)
    StubDisplay d;
    shell.render(d);
    TEST_ASSERT_TRUE_MESSAGE(d.drewText("LATCH2  #2"),
        "Debug latch counter must count flips (2), not frames (5)");
    TEST_ASSERT_FALSE_MESSAGE(d.drewText("LATCH2  #5"),
        "Debug latch counter must not count every frame");
}

static void test_bpm_mode_enc1_adjusts_tempo() {
    core::AppShell shell;
    core::BpmMode bpm(shell);
    shell.addMode(&bpm);
    shell.begin();
    shell.setBpm(120);
    shell.onEncoderKnob(1, +5);
    TEST_ASSERT_EQUAL_INT(125, shell.bpm());
    StubDisplay d;
    shell.render(d);
    TEST_ASSERT_TRUE(d.drewText("125"));
}

static void test_bpm_clamps() {
    core::AppShell shell;
    core::BpmMode bpm(shell);
    shell.addMode(&bpm);
    shell.begin();
    shell.setBpm(30);
    shell.onEncoderKnob(1, -10);
    TEST_ASSERT_EQUAL_INT(30, shell.bpm());
}

static void test_shell_settings_defaults_and_setters() {
    core::AppShell shell;
    TEST_ASSERT_EQUAL_INT(1, shell.midiOutChannel());
    TEST_ASSERT_EQUAL_INT(0, shell.midiInChannel());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(core::ClockSource::Internal),
                          static_cast<int>(shell.clockSource()));
    shell.setMidiOutChannel(10);
    shell.setMidiInChannel(5);
    shell.setClockSource(core::ClockSource::External);
    TEST_ASSERT_EQUAL_INT(10, shell.midiOutChannel());
    TEST_ASSERT_EQUAL_INT(5, shell.midiInChannel());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(core::ClockSource::External),
                          static_cast<int>(shell.clockSource()));
    shell.setMidiOutChannel(0);
    shell.setMidiOutChannel(17);
    TEST_ASSERT_EQUAL_INT(10, shell.midiOutChannel());   // out-of-range ignored
}

static void test_omni_passes_all_channels() {
    core::AppShell shell; FakeMode a("a", 1);
    shell.addMode(&a); shell.begin();
    core::MidiMessage m{}; m.type = core::MidiType::NoteOn; m.channel = 5; m.data1 = 60; m.data2 = 100;
    shell.onMidiIn(m);
    TEST_ASSERT_EQUAL_INT(1, a.midiCount);
}
static void test_channel_filter_drops_other_channels() {
    core::AppShell shell; FakeMode a("a", 1);
    shell.addMode(&a); shell.begin();
    shell.setMidiInChannel(3);
    core::MidiMessage on5{}; on5.type = core::MidiType::NoteOn; on5.channel = 5; on5.data1 = 60; on5.data2 = 100;
    shell.onMidiIn(on5);
    TEST_ASSERT_EQUAL_INT(0, a.midiCount);     // ch5 != 3 dropped
    core::MidiMessage on3{}; on3.type = core::MidiType::NoteOn; on3.channel = 3; on3.data1 = 60; on3.data2 = 100;
    shell.onMidiIn(on3);
    TEST_ASSERT_EQUAL_INT(1, a.midiCount);     // ch3 == 3 passes
}

static void test_shell_scale_defaults_cmajor_and_sets() {
    core::AppShell shell;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(core::Scale::Type::Major),
                          static_cast<int>(shell.scale().type()));
    TEST_ASSERT_EQUAL_INT(0, shell.scale().root());
    shell.setScaleRoot(7);
    shell.setScaleType(core::Scale::Type::Minor);
    TEST_ASSERT_EQUAL_INT(7, shell.scale().root());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(core::Scale::Type::Minor),
                          static_cast<int>(shell.scale().type()));
}

// begin(startMode) boots into the given mode (used to boot into Berlin);
// out-of-range values fall back to mode 0.
static void test_begin_starts_on_given_mode() {
    core::AppShell shell;
    FakeMode a("a", 1), b("b", 1), c("c", 1);
    shell.addMode(&a); shell.addMode(&b); shell.addMode(&c);
    shell.begin(2);
    shell.onEncoderKnob(1, +1);                  // routed to the ACTIVE mode's screen
    auto& sc = static_cast<FakeScreen&>(c.screen(0));
    TEST_ASSERT_EQUAL_INT(1, static_cast<int>(sc.encoders.size()));

    core::AppShell shell2;
    FakeMode d("d", 1), e("e", 1);
    shell2.addMode(&d); shell2.addMode(&e);
    shell2.begin(99);                            // out of range -> mode 0
    shell2.onEncoderKnob(1, +1);
    auto& sd = static_cast<FakeScreen&>(d.screen(0));
    TEST_ASSERT_EQUAL_INT(1, static_cast<int>(sd.encoders.size()));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_begin_starts_on_given_mode);
    RUN_TEST(test_fake_mode_screen_dispatch);
    RUN_TEST(test_enc1to4_route_to_active_screen);
    RUN_TEST(test_enc5_switches_screen_with_wrap);
    RUN_TEST(test_midi_in_reaches_active_mode);
    RUN_TEST(test_raw_input_tap_fires_for_all_controls);
    RUN_TEST(test_overlay_open_select_confirm);
    RUN_TEST(test_overlay_timeout_reverts);
    RUN_TEST(test_overlay_rotation_resets_timeout);
    RUN_TEST(test_overlay_carousel_animates_toward_choice);
    RUN_TEST(test_overlay_carousel_wraps_shortest_path);
    RUN_TEST(test_settings_loaded_at_boot);
    RUN_TEST(test_corrupt_or_out_of_range_blob_keeps_defaults);
    RUN_TEST(test_out_of_range_scale_type_keeps_default);
    RUN_TEST(test_settings_edit_saves_once_after_debounce);
    RUN_TEST(test_settings_rapid_edits_collapse_to_single_save);
    RUN_TEST(test_factory_reset_restores_defaults_and_erases);
    RUN_TEST(test_latch1_toggles_play_pause_and_sends_realtime);
    RUN_TEST(test_latch2_stop_latch3_reset);
    RUN_TEST(test_capturing_mode_skips_global_transport);
    RUN_TEST(test_non_capturing_mode_sends_global_transport);
    RUN_TEST(test_boot_with_latch_on_no_phantom_transport);
    RUN_TEST(test_leave_capturing_mode_no_phantom_transport);
    RUN_TEST(test_global_latch_transport_gated_by_mode);
    RUN_TEST(test_render_draws_top_bar_with_mode_and_screen);
    RUN_TEST(test_render_overlay_lists_modes);
    RUN_TEST(test_debug_mode_counts_raw_input);
    RUN_TEST(test_debug_mode_latch_counts_flips_not_frames);
    RUN_TEST(test_bpm_mode_enc1_adjusts_tempo);
    RUN_TEST(test_bpm_clamps);
    RUN_TEST(test_shell_scale_defaults_cmajor_and_sets);
    RUN_TEST(test_shell_settings_defaults_and_setters);
    RUN_TEST(test_omni_passes_all_channels);
    RUN_TEST(test_channel_filter_drops_other_channels);
    return UNITY_END();
}
