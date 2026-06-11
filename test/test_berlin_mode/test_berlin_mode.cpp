#include <unity.h>

#include "core/app/AppShell.h"
#include "core/modes/BerlinMode.h"
#include "support/FakeMidiOutput.h"

void setUp()    {}
void tearDown() {}

// ---------------------------------------------------------------------------
// Latch transport: Latch1 = Play/Pause (level), Latch2 = Stop (rewind, edge).
// ---------------------------------------------------------------------------
static void test_latch1_play_pause_latch2_stop() {
    core::AppShell shell;
    core::BerlinMode berlin(shell);
    FakeMidiOutput out; berlin.setMidiOutput(&out);
    berlin.onEnter();
    TEST_ASSERT_FALSE(berlin.engine().isPlaying());

    berlin.onRawInput({core::RawInput::Kind::Latch, 1, 0, true});   // play
    TEST_ASSERT_TRUE(berlin.engine().isPlaying());
    berlin.onRawInput({core::RawInput::Kind::Latch, 1, 0, true});   // same level → still playing
    TEST_ASSERT_TRUE(berlin.engine().isPlaying());
    berlin.onRawInput({core::RawInput::Kind::Latch, 1, 0, false});  // pause
    TEST_ASSERT_FALSE(berlin.engine().isPlaying());

    berlin.onRawInput({core::RawInput::Kind::Latch, 2, 0, true});   // stop → rewind
    TEST_ASSERT_EQUAL_INT(0, berlin.engine().playhead());
}

// ---------------------------------------------------------------------------
// Latch2/Latch3 are CHANGE-detected: the shell delivers the latch level every
// main-loop frame, so a held-ON latch must NOT re-trigger per frame. We prove
// this on Latch2 (Stop) by advancing the playhead, then holding Latch2 ON
// across many frames: the first ON rewinds, and subsequent (still-ON) frames
// are no-ops — playhead stays at 0 and the engine remains stopped.
// ---------------------------------------------------------------------------
static void test_held_latch_edge_detect() {
    core::AppShell shell;
    core::BerlinMode berlin(shell);
    FakeMidiOutput out; berlin.setMidiOutput(&out);
    berlin.onEnter();

    // Run a few ticks so the playhead moves off step 0.
    berlin.onRawInput({core::RawInput::Kind::Latch, 1, 0, true});   // play
    for (int i = 0; i < 24; ++i) berlin.onClockTick();
    TEST_ASSERT_TRUE(berlin.engine().playhead() > 0 || berlin.engine().isPlaying());

    // Prime: the first Latch2 delivery after onEnter() is absorbed (first-frame
    // resync), so send the opposite (OFF) level first to consume the absorb.
    berlin.onRawInput({core::RawInput::Kind::Latch, 2, 0, false});

    // Hold Latch2 (Stop) ON across many frames — only the rising edge acts.
    for (int i = 0; i < 10; ++i)
        berlin.onRawInput({core::RawInput::Kind::Latch, 2, 0, true});
    TEST_ASSERT_FALSE(berlin.engine().isPlaying());
    TEST_ASSERT_EQUAL_INT(0, berlin.engine().playhead());

    // Releasing then re-asserting Latch2 is a fresh edge (still a no-op on an
    // already-stopped engine, but must not throw / advance).
    berlin.onRawInput({core::RawInput::Kind::Latch, 2, 0, false});
    berlin.onRawInput({core::RawInput::Kind::Latch, 2, 0, true});
    TEST_ASSERT_EQUAL_INT(0, berlin.engine().playhead());
}

// ---------------------------------------------------------------------------
// test_latch3_resync_on_reentry (N9)
//   lastLatch_ must be re-synced on onEnter() so a stale shadow does not fire a
//   phantom Generate that destroys a Locked sequence. Scenario: Latch3 happens
//   to be ON; we leave (onExit) and re-enter (onEnter); the first delivery on
//   re-entry carries a DIFFERENT level than the stale shadow — this must be
//   ABSORBED (no regenerate). A subsequent genuine flip DOES regenerate.
// ---------------------------------------------------------------------------
static int berlinSeqDiffN9(const core::BerlinSequence& a, const core::BerlinSequence& b);
static void test_latch3_resync_on_reentry() {
    core::AppShell shell;
    core::BerlinMode berlin(shell);
    FakeMidiOutput out; berlin.setMidiOutput(&out);
    berlin.onEnter();

    // Latch3 ON once → one generate (and syncs the shadow ON for index 3).
    berlin.onRawInput({core::RawInput::Kind::Latch, 3, 0, true});

    // Leave and re-enter — onEnter() must clear the per-index sync flags so the
    // first delivery is absorbed regardless of the stale ON shadow.
    berlin.onExit();
    berlin.onEnter();
    core::BerlinSequence afterEnter = berlin.engine().sequence();

    // First delivery on re-entry carries the OPPOSITE level (OFF) — different
    // from the stale ON shadow. With resync this is absorbed: NO regenerate.
    berlin.onRawInput({core::RawInput::Kind::Latch, 3, 0, false});
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, berlinSeqDiffN9(afterEnter, berlin.engine().sequence()),
        "first Latch3 delivery on re-entry must be absorbed, not regenerate");

    // A subsequent genuine flip (OFF → ON) DOES regenerate.
    berlin.onRawInput({core::RawInput::Kind::Latch, 3, 0, true});
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, berlinSeqDiffN9(afterEnter, berlin.engine().sequence()),
        "a genuine flip after the absorb must regenerate");
}

// ---------------------------------------------------------------------------
// Under the default Send transport mode, Berlin's local latches also emit MIDI
// transport so a DAW can follow: Latch1 ON → Start, Latch1 OFF → Stop, Latch1
// ON again (resuming from paused) → Continue. The emission is flip-gated, so
// the first-frame sync adoption is silent (primed with the opposite level).
// ---------------------------------------------------------------------------
static void test_berlin_latches_emit_transport_under_send() {
    core::AppShell shell;
    core::BerlinMode berlin(shell);
    FakeMidiOutput out;
    berlin.setMidiOutput(&out);
    shell.setMidiOutput(&out);          // notify emission goes through the shell
    berlin.onEnter();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(core::TransportMode::Send),
                          static_cast<int>(shell.transportMode()));

    berlin.onRawInput({core::RawInput::Kind::Latch, 1, 0, false});  // prime: absorb first delivery
    berlin.onRawInput({core::RawInput::Kind::Latch, 1, 0, true});   // flip ON → Start
    TEST_ASSERT_EQUAL_INT(1, out.starts);
    TEST_ASSERT_EQUAL_INT(0, out.stops);

    berlin.onRawInput({core::RawInput::Kind::Latch, 1, 0, false});  // flip OFF → Stop
    TEST_ASSERT_EQUAL_INT(1, out.stops);

    berlin.onRawInput({core::RawInput::Kind::Latch, 1, 0, true});   // flip ON from paused → Continue
    TEST_ASSERT_EQUAL_INT(1, out.continues);
    TEST_ASSERT_EQUAL_INT(1, out.starts);                          // no extra Start
}

// ---------------------------------------------------------------------------
// Latch2 (Stop) flip emits a MIDI Stop under Send.
// ---------------------------------------------------------------------------
static void test_berlin_latch2_emits_stop_under_send() {
    core::AppShell shell;
    core::BerlinMode berlin(shell);
    FakeMidiOutput out;
    berlin.setMidiOutput(&out);
    shell.setMidiOutput(&out);
    berlin.onEnter();

    berlin.onRawInput({core::RawInput::Kind::Latch, 2, 0, false});  // prime
    berlin.onRawInput({core::RawInput::Kind::Latch, 2, 0, true});   // flip → Stop
    TEST_ASSERT_EQUAL_INT(1, out.stops);
}

// ---------------------------------------------------------------------------
// With TransportMode::Off the same latch flips perform their local engine
// action but emit NOTHING to the wire.
// ---------------------------------------------------------------------------
static void test_berlin_latches_silent_under_off() {
    core::AppShell shell;
    core::BerlinMode berlin(shell);
    FakeMidiOutput out;
    berlin.setMidiOutput(&out);
    shell.setMidiOutput(&out);
    shell.setTransportMode(core::TransportMode::Off);
    berlin.onEnter();

    berlin.onRawInput({core::RawInput::Kind::Latch, 1, 0, false});  // prime
    berlin.onRawInput({core::RawInput::Kind::Latch, 1, 0, true});   // play (engine)
    TEST_ASSERT_TRUE(berlin.engine().isPlaying());                  // local action still happens
    berlin.onRawInput({core::RawInput::Kind::Latch, 1, 0, false});  // pause
    berlin.onRawInput({core::RawInput::Kind::Latch, 2, 0, false});  // prime latch2
    berlin.onRawInput({core::RawInput::Kind::Latch, 2, 0, true});   // stop

    TEST_ASSERT_EQUAL_INT(0, out.starts);
    TEST_ASSERT_EQUAL_INT(0, out.continues);
    TEST_ASSERT_EQUAL_INT(0, out.stops);
}

// ---------------------------------------------------------------------------
// Structure screen encoder edits + clamping.
// ---------------------------------------------------------------------------
static void test_structure_screen_edits_and_clamps() {
    core::AppShell shell;
    core::BerlinMode berlin(shell);
    core::Screen& s = berlin.screen(0);

    // Length clamps 3..32
    for (int i = 0; i < 40; ++i) s.onEncoder(2, +1);
    TEST_ASSERT_EQUAL_INT(32, berlin.params().length);
    for (int i = 0; i < 40; ++i) s.onEncoder(2, -1);
    TEST_ASSERT_EQUAL_INT(3, berlin.params().length);

    // Density clamps 0..100 (step = 5)
    for (int i = 0; i < 40; ++i) s.onEncoder(4, +1);
    TEST_ASSERT_EQUAL_INT(100, berlin.params().density);
    for (int i = 0; i < 40; ++i) s.onEncoder(4, -1);
    TEST_ASSERT_EQUAL_INT(0, berlin.params().density);
}

// ---------------------------------------------------------------------------
// Character + Behavior screen encoder edits + clamping.
// ---------------------------------------------------------------------------
static void test_character_behavior_screens_edit_clamp() {
    core::AppShell shell;
    core::BerlinMode berlin(shell);
    core::Screen& ch = berlin.screen(1);
    core::Screen& bh = berlin.screen(3);

    // --- CharacterScreen ---
    // Gate clamps 40..99 (Enc1)
    for (int i = 0; i < 100; ++i) ch.onEncoder(1, +1);
    TEST_ASSERT_EQUAL_INT(99, berlin.params().gatePercent);
    for (int i = 0; i < 100; ++i) ch.onEncoder(1, -1);
    TEST_ASSERT_EQUAL_INT(40, berlin.params().gatePercent);

    // OctaveRange clamps 1..3 (Enc4)
    for (int i = 0; i < 10; ++i) ch.onEncoder(4, +1);
    TEST_ASSERT_EQUAL_INT(3, berlin.params().octaveRange);
    for (int i = 0; i < 10; ++i) ch.onEncoder(4, -1);
    TEST_ASSERT_EQUAL_INT(1, berlin.params().octaveRange);

    // --- BehaviorScreen ---
    // Morph clamps 0..100 (Enc2, step 5)
    for (int i = 0; i < 40; ++i) bh.onEncoder(2, +1);
    TEST_ASSERT_EQUAL_INT(100, berlin.params().morph);
    for (int i = 0; i < 40; ++i) bh.onEncoder(2, -1);
    TEST_ASSERT_EQUAL_INT(0, berlin.params().morph);

    // EvolveRate clamps 1..8 (Enc3) — editable only under the Evolve behavior.
    berlin.params().behavior = core::BerlinBehavior::Evolve;
    for (int i = 0; i < 20; ++i) bh.onEncoder(3, +1);
    TEST_ASSERT_EQUAL_INT(8, berlin.params().evolveRate);
    for (int i = 0; i < 20; ++i) bh.onEncoder(3, -1);
    TEST_ASSERT_EQUAL_INT(1, berlin.params().evolveRate);
}

// ---------------------------------------------------------------------------
// Dynamics screen (index 2): velocity/humanize/accent + fixed SCATTER cell.
// Scatter edits only under DrunkardWalk; the knob is locked (cell dimmed)
// under the other algorithms.
// ---------------------------------------------------------------------------
static void test_dynamics_screen() {
    core::AppShell shell;
    core::BerlinMode berlin(shell);
    core::Screen& dyn = berlin.screen(2);          // Dynamics now at index 2

    for (int i = 0; i < 200; ++i) dyn.onEncoder(1, +1);
    TEST_ASSERT_EQUAL_INT(126, berlin.params().velocityBase);
    for (int i = 0; i < 50; ++i) dyn.onEncoder(3, -1);
    TEST_ASSERT_EQUAL_INT(0, berlin.params().accent);

    berlin.params().algorithm = core::BerlinAlgorithm::DrunkardWalk;
    for (int i = 0; i < 20; ++i) dyn.onEncoder(4, +1);
    TEST_ASSERT_EQUAL_INT(7, berlin.params().scatter);          // clamps 1..7
    berlin.params().algorithm = core::BerlinAlgorithm::GatePitchPhasing;
    uint8_t scBefore = berlin.params().scatter;
    for (int i = 0; i < 5; ++i) dyn.onEncoder(4, -1);
    TEST_ASSERT_EQUAL_UINT8(scBefore, berlin.params().scatter); // locked under Phase
    berlin.params().algorithm = core::BerlinAlgorithm::DegreeWeighted;
    for (int i = 0; i < 5; ++i) dyn.onEncoder(4, -1);
    TEST_ASSERT_EQUAL_UINT8(scBefore, berlin.params().scatter); // locked under Degree
}

// ---------------------------------------------------------------------------
// Behavior screen Enc4 = fixed GATELEN cell: edits only under GatePitchPhasing,
// locked (dimmed) otherwise. Enc3 (Evolve rate) edits only under Evolve.
// ---------------------------------------------------------------------------
static void test_behavior_screen_gatelen_and_evolve_locks() {
    core::AppShell shell;
    core::BerlinMode berlin(shell);
    core::Screen& bh = berlin.screen(3);

    // GateLen edits + clamps under Phase only.
    berlin.params().algorithm = core::BerlinAlgorithm::GatePitchPhasing;
    for (int i = 0; i < 30; ++i) bh.onEncoder(4, +1);
    TEST_ASSERT_EQUAL_INT(16, berlin.params().gateLen);
    for (int i = 0; i < 30; ++i) bh.onEncoder(4, -1);
    TEST_ASSERT_EQUAL_INT(3, berlin.params().gateLen);
    berlin.params().algorithm = core::BerlinAlgorithm::DrunkardWalk;
    for (int i = 0; i < 5; ++i) bh.onEncoder(4, +1);
    TEST_ASSERT_EQUAL_INT(3, berlin.params().gateLen);          // locked under Walk

    // Evolve rate edits only under the Evolve behavior.
    berlin.params().behavior = core::BerlinBehavior::Locked;
    uint8_t evBefore = berlin.params().evolveRate;
    for (int i = 0; i < 3; ++i) bh.onEncoder(3, +1);
    TEST_ASSERT_EQUAL_UINT8(evBefore, berlin.params().evolveRate);  // locked
    berlin.params().behavior = core::BerlinBehavior::Evolve;
    for (int i = 0; i < 20; ++i) bh.onEncoder(3, +1);
    TEST_ASSERT_EQUAL_INT(8, berlin.params().evolveRate);           // editable + clamped
}

// ---------------------------------------------------------------------------
// onExit() silences a sounding note so voices do not hang on mode switch.
// ---------------------------------------------------------------------------
static void test_onexit_silences_sounding_note() {
    core::AppShell shell;
    core::BerlinMode berlin(shell);
    FakeMidiOutput out; berlin.setMidiOutput(&out);
    berlin.onEnter();

    // Latch1 ON → play; advance a tick so the engine emits a NoteOn for step 0.
    berlin.onRawInput({core::RawInput::Kind::Latch, 1, 0, true});
    berlin.onClockTick();   // emitStep(0) → NoteOn queued; noteSounding_ = true
    TEST_ASSERT_TRUE(berlin.engine().isPlaying());

    // Count NoteOff events before calling onExit().
    int noteOffsBefore = 0;
    for (const auto& e : out.events) { if (!e.isOn) ++noteOffsBefore; }

    berlin.onExit();

    // Engine must be stopped and the sounding note silenced.
    TEST_ASSERT_FALSE(berlin.engine().isPlaying());
    int noteOffsAfter = 0;
    for (const auto& e : out.events) { if (!e.isOn) ++noteOffsAfter; }
    TEST_ASSERT_GREATER_THAN(noteOffsBefore, noteOffsAfter);
}

// ---------------------------------------------------------------------------
// Latch3 (Generate) fires on EACH flip (up or down), so one toggle = one
// regenerate (the sim/hardware latch toggles, so rising-only would need an
// off-then-on to re-trigger). generate() rewinds the playhead to 0, so we
// prove each flip acted by advancing the playhead and checking a flip resets it.
// ---------------------------------------------------------------------------
static void test_latch3_generate_on_each_flip() {
    core::AppShell shell;
    core::BerlinMode berlin(shell);
    FakeMidiOutput out; berlin.setMidiOutput(&out);
    berlin.onEnter();
    berlin.onRawInput({core::RawInput::Kind::Latch, 3, 0, false});  // prime: absorb first Latch3 delivery after onEnter
    berlin.onRawInput({core::RawInput::Kind::Latch, 1, 0, true});   // play

    for (int i = 0; i < 12; ++i) berlin.onClockTick();              // 8th step → playhead 1
    TEST_ASSERT_EQUAL_INT(1, berlin.engine().playhead());
    berlin.onRawInput({core::RawInput::Kind::Latch, 3, 0, true});   // flip ON → generate
    TEST_ASSERT_EQUAL_INT(0, berlin.engine().playhead());           // rewound

    for (int i = 0; i < 12; ++i) berlin.onClockTick();              // playhead 1 again
    TEST_ASSERT_EQUAL_INT(1, berlin.engine().playhead());
    berlin.onRawInput({core::RawInput::Kind::Latch, 3, 0, false});  // flip OFF → also generates
    TEST_ASSERT_EQUAL_INT(0, berlin.engine().playhead());           // rewound again
}

// ---------------------------------------------------------------------------
// Algorithm dispatch: switching params_.algorithm before a generate causes the
// engine to use the corresponding generator (reflected in the produced sequence).
// ---------------------------------------------------------------------------
static int activeCount(const core::BerlinSequence& s) {
    int n = 0; for (int i = 0; i < s.length(); ++i) if (s.step(i).active) ++n; return n;
}

static void test_algorithm_dispatch() {
    core::AppShell shell;
    core::BerlinMode berlin(shell);
    FakeMidiOutput out; berlin.setMidiOutput(&out);
    berlin.onEnter();
    berlin.onRawInput({core::RawInput::Kind::Latch, 3, 0, false});  // prime: absorb first Latch3 delivery after onEnter

    // Phasing: length 8 × gateLen 6 → realized length lcm = 24 (≠ the 16 Walk uses).
    berlin.params().algorithm = core::BerlinAlgorithm::GatePitchPhasing;
    berlin.params().length = 8; berlin.params().gateLen = 6;
    berlin.onRawInput({core::RawInput::Kind::Latch, 3, 0, true});   // generate
    TEST_ASSERT_EQUAL_INT(24, berlin.engine().sequence().length());

    // Degree-Weighted: back to a length-16 sequence, all in scale.
    berlin.params().algorithm = core::BerlinAlgorithm::DegreeWeighted;
    berlin.params().length = 16;
    berlin.onRawInput({core::RawInput::Kind::Latch, 3, 0, false});  // generate (other flip)
    TEST_ASSERT_EQUAL_INT(16, berlin.engine().sequence().length());
    TEST_ASSERT_TRUE(activeCount(berlin.engine().sequence()) >= 1);
}

// ---------------------------------------------------------------------------
// Live behavior: structural edits SCULPT the existing sequence in place —
// only the touched parameter's effect is applied, the rest of the sequence and
// the playhead stay. A full regeneration happens only on Generate (Latch3).
// ---------------------------------------------------------------------------
static int berlinSeqDiff(const core::BerlinSequence& a, const core::BerlinSequence& b) {
    if (a.length() != b.length()) return 999;
    int d = 0;
    for (int i = 0; i < a.length(); ++i) {
        if (a.step(i).active   != b.step(i).active   ||
            a.step(i).note     != b.step(i).note      ||
            a.step(i).velocity != b.step(i).velocity) {
            ++d;
        }
    }
    return d;
}

// Wrapper so test_latch3_resync_on_reentry (declared earlier) can reuse the
// sequence-diff logic without duplicating it.
static int berlinSeqDiffN9(const core::BerlinSequence& a, const core::BerlinSequence& b) {
    return berlinSeqDiff(a, b);
}

// Live now SCULPTS in place: a Length edit truncates/extends the existing
// sequence, keeps the surviving prefix byte-identical, and the playhead keeps
// running (it wraps on shorten, never resets to 0).
static bool berlinStepEqual(const core::BerlinStep& a, const core::BerlinStep& b) {
    return a.active == b.active && a.note == b.note && a.velocity == b.velocity &&
           a.accent == b.accent && a.gateTicks == b.gateTicks;
}

static void test_live_length_edit_sculpts_in_place() {
    core::AppShell shell;
    core::BerlinMode berlin(shell);
    FakeMidiOutput out; berlin.setMidiOutput(&out);
    berlin.onEnter();
    berlin.params().behavior = core::BerlinBehavior::Live;
    berlin.params().length = 16;
    berlin.onRawInput({core::RawInput::Kind::Latch, 3, 0, false});  // prime latch absorb
    berlin.onRawInput({core::RawInput::Kind::Latch, 3, 0, true});   // generate at length 16
    TEST_ASSERT_EQUAL_INT(16, berlin.engine().sequence().length());

    // Snapshot, then run the playhead off step 0.
    core::BerlinSequence snap = berlin.engine().sequence();
    berlin.onRawInput({core::RawInput::Kind::Latch, 1, 0, true});   // play
    for (int i = 0; i < 36; ++i) berlin.onClockTick();              // advance several steps
    const int phBefore = berlin.engine().playhead();
    TEST_ASSERT_TRUE(phBefore > 0);                                 // moved off 0

    // Turn Length down by 2 — sculpt in place (no full regen).
    core::Screen& structure = berlin.screen(0);
    structure.onEncoder(2, -1);                                     // 16 → 15
    structure.onEncoder(2, -1);                                     // 15 → 14
    TEST_ASSERT_EQUAL_INT(14, berlin.engine().sequence().length());

    // The kept prefix is byte-identical (truncation, not regeneration).
    for (int i = 0; i < 14; ++i) {
        TEST_ASSERT_TRUE_MESSAGE(berlinStepEqual(snap.step(i), berlin.engine().sequence().step(i)),
            "kept prefix must be byte-identical after a Live length shorten");
    }
    // Playhead did NOT reset to 0; it kept/wrapped its running position.
    const int phAfter = berlin.engine().playhead();
    TEST_ASSERT_EQUAL_INT(phBefore % 14, phAfter);
}

static void test_velocity_edit_applies_live_and_locked_never_regens() {
    core::AppShell shell;
    core::BerlinMode berlin(shell);
    FakeMidiOutput out; berlin.setMidiOutput(&out);
    berlin.onEnter();

    // Velocity is now a LIVE performance parameter: a Dynamics Enc1 edit re-stamps
    // the sequence's velocities at once, in EVERY behavior — even under Locked.
    berlin.params().behavior = core::BerlinBehavior::Locked;
    core::BerlinSequence before = berlin.engine().sequence();
    berlin.screen(2).onEncoder(1, +1);                // velocity base +1 (live)
    bool anyVelChanged = false;
    for (int i = 0; i < before.length(); ++i) {
        if (before.step(i).active &&
            before.step(i).velocity != berlin.engine().sequence().step(i).velocity) {
            anyVelChanged = true;
        }
        // Notes/rhythm untouched — only the velocity re-stamps.
        TEST_ASSERT_EQUAL_UINT8(before.step(i).note,   berlin.engine().sequence().step(i).note);
        TEST_ASSERT_EQUAL_INT(static_cast<int>(before.step(i).active),
                              static_cast<int>(berlin.engine().sequence().step(i).active));
    }
    TEST_ASSERT_TRUE_MESSAGE(anyVelChanged, "velocity edit must apply live even under Locked");

    // Locked + STRUCTURAL edit (Density) stays staged — no in-place edit.
    core::BerlinSequence base2 = berlin.engine().sequence();
    berlin.screen(0).onEncoder(4, +1);                // density +5 (structural) but Locked
    TEST_ASSERT_EQUAL_INT(0, berlinSeqDiff(base2, berlin.engine().sequence()));
}

// Live octave edit transposes the existing sequence in place: every active note
// shifts by +12 (or folds), the melody contour (note-to-note deltas) is
// preserved, and the playhead is untouched. Morph is irrelevant now.
static void test_live_octave_edit_transposes_in_place() {
    core::AppShell shell;
    core::BerlinMode berlin(shell);
    FakeMidiOutput out; berlin.setMidiOutput(&out);
    berlin.onEnter();
    berlin.params().behavior = core::BerlinBehavior::Live;
    berlin.params().morph = 0;                         // irrelevant for in-place sculpting
    berlin.params().octaveBase = 36;                   // room to shift up without clamping
    berlin.params().octaveRange = 3;                   // wide register so +12 doesn't fold
    berlin.onRawInput({core::RawInput::Kind::Latch, 3, 0, false});  // prime latch absorb
    berlin.onRawInput({core::RawInput::Kind::Latch, 3, 0, true});   // generate

    core::BerlinSequence snap = berlin.engine().sequence();
    berlin.onRawInput({core::RawInput::Kind::Latch, 1, 0, true});   // play
    for (int i = 0; i < 24; ++i) berlin.onClockTick();
    const int phBefore = berlin.engine().playhead();

    berlin.screen(1).onEncoder(3, +1);                 // Character Enc3: OctaveBase +12

    // Every active note shifted up by exactly 12 (wide register, no folding).
    const core::BerlinSequence& now = berlin.engine().sequence();
    for (int i = 0; i < snap.length(); ++i) {
        if (snap.step(i).active) {
            TEST_ASSERT_EQUAL_INT(snap.step(i).note + 12, now.step(i).note);
        }
    }
    // Playhead untouched (no regen).
    TEST_ASSERT_EQUAL_INT(phBefore, berlin.engine().playhead());
}

// Live density up adds notes in place: the active count grows and the
// previously-active steps are unchanged.
static void test_live_density_edit_in_place() {
    core::AppShell shell;
    core::BerlinMode berlin(shell);
    FakeMidiOutput out; berlin.setMidiOutput(&out);
    berlin.onEnter();
    berlin.params().behavior = core::BerlinBehavior::Live;
    berlin.params().length = 16;
    berlin.params().density = 40;
    berlin.onRawInput({core::RawInput::Kind::Latch, 3, 0, false});  // prime latch absorb
    berlin.onRawInput({core::RawInput::Kind::Latch, 3, 0, true});   // generate at density 40

    core::BerlinSequence snap = berlin.engine().sequence();
    const int activeBefore = activeCount(snap);

    core::Screen& structure = berlin.screen(0);
    for (int i = 0; i < 8; ++i) structure.onEncoder(4, +1);        // density up to 80

    const core::BerlinSequence& now = berlin.engine().sequence();
    TEST_ASSERT_TRUE(activeCount(now) > activeBefore);             // grew
    // Previously-active steps are byte-identical (only inactive steps were filled).
    for (int i = 0; i < snap.length(); ++i) {
        if (snap.step(i).active) {
            TEST_ASSERT_TRUE_MESSAGE(berlinStepEqual(snap.step(i), now.step(i)),
                "previously-active steps must stay identical after a Live density add");
        }
    }
}

// The Algorithm knob does NOT regenerate under Live (it parameterizes how a
// sequence is created, applied at the next Generate). A subsequent Latch3 flip
// DOES regenerate using the new algorithm.
static void test_live_algorithm_knob_does_not_regen() {
    core::AppShell shell;
    core::BerlinMode berlin(shell);
    FakeMidiOutput out; berlin.setMidiOutput(&out);
    berlin.onEnter();
    berlin.params().behavior = core::BerlinBehavior::Live;
    berlin.onRawInput({core::RawInput::Kind::Latch, 3, 0, false});  // prime latch absorb
    berlin.onRawInput({core::RawInput::Kind::Latch, 3, 0, true});   // generate

    core::BerlinSequence snap = berlin.engine().sequence();
    berlin.screen(0).onEncoder(1, +1);                 // Algorithm change — no regen
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, berlinSeqDiff(snap, berlin.engine().sequence()),
        "Algorithm knob must not regenerate under Live");

    // A subsequent Generate DOES regenerate with the new algorithm.
    berlin.onRawInput({core::RawInput::Kind::Latch, 3, 0, false});  // flip → generate
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, berlinSeqDiff(snap, berlin.engine().sequence()),
        "a Generate after the Algorithm change must regenerate");
}

// Resolution is inherently live in every behavior: turning it re-stamps the
// per-step gateTicks (for the piano-roll widths) without changing the notes or
// the playhead. We assert on a non-Live behavior to prove it is unconditional.
static void test_resolution_restamps_gate_without_regen() {
    core::AppShell shell;
    core::BerlinMode berlin(shell);
    FakeMidiOutput out; berlin.setMidiOutput(&out);
    berlin.onEnter();
    berlin.params().behavior = core::BerlinBehavior::Locked;  // not Live
    berlin.params().resolution = core::BerlinResolution::Sixteenth;
    berlin.onRawInput({core::RawInput::Kind::Latch, 3, 0, false});  // prime latch absorb
    berlin.onRawInput({core::RawInput::Kind::Latch, 3, 0, true});   // generate

    // Capture notes (must not change) and the first active step's gateTicks.
    core::BerlinSequence snap = berlin.engine().sequence();
    int firstActive = -1;
    for (int i = 0; i < snap.length(); ++i) { if (snap.step(i).active) { firstActive = i; break; } }
    TEST_ASSERT_TRUE(firstActive >= 0);
    const uint16_t gateBefore = snap.step(firstActive).gateTicks;

    berlin.screen(0).onEncoder(3, +1);                 // Resolution 16th → 8th

    const core::BerlinSequence& now = berlin.engine().sequence();
    // Notes unchanged (active flags + pitches identical).
    for (int i = 0; i < snap.length(); ++i) {
        TEST_ASSERT_EQUAL_INT(snap.step(i).active, now.step(i).active);
        TEST_ASSERT_EQUAL_INT(snap.step(i).note,   now.step(i).note);
    }
    // gateTicks re-stamped for the wider 8th-note resolution.
    TEST_ASSERT_TRUE(now.step(firstActive).gateTicks != gateBefore);
}

// ---------------------------------------------------------------------------
// N6 safety: external Stop silences the engine. With clockSource=External the
// engine only closes gates in onClockTick(); a DAW that stops sending clock
// when its transport stops would leave a note hanging forever. Under the
// default Send mode the safety branch maps the incoming Stop to onTransport
// (Pause), which silences the sounding note (NoteOff) and halts playback. The
// Stop is CONSUMED, never re-emitted downstream.
// ---------------------------------------------------------------------------
static void test_external_stop_silences_engine() {
    core::AppShell shell;
    core::BerlinMode berlin(shell);
    FakeMidiOutput out;
    berlin.setMidiOutput(&out);
    shell.setMidiOutput(&out);
    shell.addMode(&berlin);
    shell.begin();
    shell.setClockSource(core::ClockSource::External);

    // Prime the first-frame latch absorb with the opposite level, then play.
    berlin.onRawInput({core::RawInput::Kind::Latch, 1, 0, false});
    berlin.onRawInput({core::RawInput::Kind::Latch, 1, 0, true});   // play
    TEST_ASSERT_TRUE(berlin.engine().isPlaying());

    // Feed external Clock pulses until a note is actively sounding (gate open).
    core::MidiMessage clk{}; clk.type = core::MidiType::Clock;
    for (int i = 0; i < 96 && berlin.engine().soundingNote() < 0; ++i) {
        shell.onMidiIn(clk);
    }
    TEST_ASSERT_GREATER_OR_EQUAL(0, berlin.engine().soundingNote());   // a note is sounding

    int noteOffsBefore = 0;
    for (const auto& e : out.events) { if (!e.isOn) ++noteOffsBefore; }

    // The DAW presses Stop: an external Stop message arrives via the shell.
    // Under External clock the gate-off never arrives via onClockTick(), so the
    // safety branch maps it to onTransport(Pause) — flushing the sounding note
    // (a fresh NoteOff) and halting playback.
    core::MidiMessage stop{}; stop.type = core::MidiType::Stop;
    shell.onMidiIn(stop);

    int noteOffsAfter = 0;
    for (const auto& e : out.events) { if (!e.isOn) ++noteOffsAfter; }
    TEST_ASSERT_GREATER_THAN(noteOffsBefore, noteOffsAfter);   // NoteOff was sent
    TEST_ASSERT_FALSE(berlin.engine().isPlaying());            // engine stopped
    // The incoming Stop is CONSUMED — never re-emitted downstream.
    TEST_ASSERT_EQUAL_INT(0, out.stops);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_latch1_play_pause_latch2_stop);
    RUN_TEST(test_berlin_latches_emit_transport_under_send);
    RUN_TEST(test_berlin_latch2_emits_stop_under_send);
    RUN_TEST(test_berlin_latches_silent_under_off);
    RUN_TEST(test_external_stop_silences_engine);
    RUN_TEST(test_held_latch_edge_detect);
    RUN_TEST(test_latch3_generate_on_each_flip);
    RUN_TEST(test_latch3_resync_on_reentry);
    RUN_TEST(test_structure_screen_edits_and_clamps);
    RUN_TEST(test_character_behavior_screens_edit_clamp);
    RUN_TEST(test_dynamics_screen);
    RUN_TEST(test_behavior_screen_gatelen_and_evolve_locks);
    RUN_TEST(test_onexit_silences_sounding_note);
    RUN_TEST(test_algorithm_dispatch);
    RUN_TEST(test_live_length_edit_sculpts_in_place);
    RUN_TEST(test_velocity_edit_applies_live_and_locked_never_regens);
    RUN_TEST(test_live_octave_edit_transposes_in_place);
    RUN_TEST(test_live_density_edit_in_place);
    RUN_TEST(test_live_algorithm_knob_does_not_regen);
    RUN_TEST(test_resolution_restamps_gate_without_regen);
    return UNITY_END();
}
