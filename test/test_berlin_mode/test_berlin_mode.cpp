#include <unity.h>

#include "core/app/AppShell.h"
#include "core/BerlinGen.h"           // berlinBaseRoot (Bass-invariance check)
#include "core/modes/BerlinMode.h"
#include "core/render/BerlinLayout.h" // kBerlinKbW
#include "support/FakeMidiOutput.h"
#include "support/FakeStorage.h"
#include "support/StubDisplay.h"

void setUp()    {}
void tearDown() {}

// ---------------------------------------------------------------------------
// Latch transport: Latch1 = Play/Pause (click toggle), Latch2 = Stop (rewind).
// Each flip is one click; the switch POSITION carries no meaning. The first
// delivery is absorbed, so the very first flip toggles from stopped → playing.
// ---------------------------------------------------------------------------
static void test_latch1_play_pause_latch2_stop() {
    core::AppShell shell;
    core::BerlinMode berlin(shell);
    FakeMidiOutput out; berlin.setMidiOutput(&out);
    berlin.onEnter();
    TEST_ASSERT_FALSE(berlin.engine().isPlaying());

    berlin.onRawInput({core::RawInput::Kind::Latch, 1, 0, false});  // prime: absorb first delivery
    berlin.onRawInput({core::RawInput::Kind::Latch, 1, 0, true});   // flip → play
    TEST_ASSERT_TRUE(berlin.engine().isPlaying());
    berlin.onRawInput({core::RawInput::Kind::Latch, 1, 0, true});   // same level → no click, still playing
    TEST_ASSERT_TRUE(berlin.engine().isPlaying());
    berlin.onRawInput({core::RawInput::Kind::Latch, 1, 0, false});  // flip → pause
    TEST_ASSERT_FALSE(berlin.engine().isPlaying());

    berlin.onRawInput({core::RawInput::Kind::Latch, 2, 0, true});   // stop → rewind
    TEST_ASSERT_EQUAL_INT(0, berlin.engine().playhead());
}

// ---------------------------------------------------------------------------
// Latch1 is a stateless CLICK toggle: each flip toggles Play/Pause by the
// engine state, the switch POSITION is meaningless, and holding the same level
// across frames never re-triggers. Proven under both edge-style and
// hardware-style (held-level) frame delivery.
// ---------------------------------------------------------------------------
static void test_berlin_latch1_click_toggles() {
    core::AppShell shell;
    core::BerlinMode berlin(shell);
    FakeMidiOutput out; berlin.setMidiOutput(&out);
    berlin.onEnter();

    berlin.onRawInput({core::RawInput::Kind::Latch, 1, 0, false});  // prime: absorb first delivery
    TEST_ASSERT_FALSE(berlin.engine().isPlaying());

    berlin.onRawInput({core::RawInput::Kind::Latch, 1, 0, true});   // flip ON → playing
    TEST_ASSERT_TRUE(berlin.engine().isPlaying());

    // Hardware delivers the SAME level every frame — no re-trigger, stays playing.
    for (int i = 0; i < 10; ++i)
        berlin.onRawInput({core::RawInput::Kind::Latch, 1, 0, true});
    TEST_ASSERT_TRUE(berlin.engine().isPlaying());

    berlin.onRawInput({core::RawInput::Kind::Latch, 1, 0, false});  // flip OFF → paused
    TEST_ASSERT_FALSE(berlin.engine().isPlaying());

    // Held OFF level frames after the toggle leave the state unchanged.
    for (int i = 0; i < 10; ++i)
        berlin.onRawInput({core::RawInput::Kind::Latch, 1, 0, false});
    TEST_ASSERT_FALSE(berlin.engine().isPlaying());

    berlin.onRawInput({core::RawInput::Kind::Latch, 1, 0, true});   // flip ON → playing again
    TEST_ASSERT_TRUE(berlin.engine().isPlaying());

    // Direction-independence: a fresh mode primed ON, then a flip to OFF must
    // TOGGLE from stopped → playing (a level-driven impl would pause instead).
    core::BerlinMode berlin2(shell);
    FakeMidiOutput out2; berlin2.setMidiOutput(&out2);
    berlin2.onEnter();
    TEST_ASSERT_FALSE(berlin2.engine().isPlaying());
    berlin2.onRawInput({core::RawInput::Kind::Latch, 1, 0, true});   // prime ON (absorbed)
    TEST_ASSERT_FALSE(berlin2.engine().isPlaying());
    berlin2.onRawInput({core::RawInput::Kind::Latch, 1, 0, false});  // flip ON→OFF toggles → playing
    TEST_ASSERT_TRUE(berlin2.engine().isPlaying());
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
    berlin.onRawInput({core::RawInput::Kind::Latch, 1, 0, false});  // prime: absorb first delivery
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

    // Density clamps 0..100 (step = 5) — now Enc3 on Structure.
    for (int i = 0; i < 40; ++i) s.onEncoder(3, +1);
    TEST_ASSERT_EQUAL_INT(100, berlin.params().density);
    for (int i = 0; i < 40; ++i) s.onEncoder(3, -1);
    TEST_ASSERT_EQUAL_INT(0, berlin.params().density);
}

// ---------------------------------------------------------------------------
// Character + Behavior screen encoder edits + clamping.
// ---------------------------------------------------------------------------
static void test_character_behavior_screens_edit_clamp() {
    core::AppShell shell;
    core::BerlinMode berlin(shell);
    core::Screen& ch = berlin.screen(1);
    core::Screen& bh = berlin.screen(4);

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
    // Behavior is global (canonical in voices_[0]); set it there.
    berlin.params(core::BerlinMode::kBass).behavior = core::BerlinBehavior::Evolve;
    for (int i = 0; i < 20; ++i) bh.onEncoder(3, +1);
    TEST_ASSERT_EQUAL_INT(8, berlin.params().evolveRate);
    for (int i = 0; i < 20; ++i) bh.onEncoder(3, -1);
    TEST_ASSERT_EQUAL_INT(1, berlin.params().evolveRate);
}

// ---------------------------------------------------------------------------
// Dynamics screen (index 3): velocity/humanize/accent are GLOBAL (one knob
// writes every voice). Scatter moved to Structure Enc4 (the AlgoPrm cell): it
// edits only under DrunkardWalk; the knob is locked under the other algorithms.
// ---------------------------------------------------------------------------
static void test_dynamics_screen() {
    core::AppShell shell;
    core::BerlinMode berlin(shell);
    core::Screen& dyn = berlin.screen(3);          // Dynamics now at index 3
    core::Screen& str = berlin.screen(0);          // Structure carries AlgoPrm

    for (int i = 0; i < 200; ++i) dyn.onEncoder(1, +1);
    TEST_ASSERT_EQUAL_INT(126, berlin.params().velocityBase);
    for (int i = 0; i < 50; ++i) dyn.onEncoder(3, -1);
    TEST_ASSERT_EQUAL_INT(0, berlin.params().accent);

    // Scatter is now Structure Enc4 (edit voice is the default High, melodic).
    berlin.params().algorithm = core::BerlinAlgorithm::DrunkardWalk;
    for (int i = 0; i < 20; ++i) str.onEncoder(4, +1);
    TEST_ASSERT_EQUAL_INT(7, berlin.params().scatter);          // clamps 1..7
    berlin.params().algorithm = core::BerlinAlgorithm::GatePitchPhasing;
    uint8_t scBefore = berlin.params().scatter;
    for (int i = 0; i < 5; ++i) str.onEncoder(4, -1);
    TEST_ASSERT_EQUAL_UINT8(scBefore, berlin.params().scatter); // locked under Phase
    berlin.params().algorithm = core::BerlinAlgorithm::DegreeWeighted;
    for (int i = 0; i < 5; ++i) str.onEncoder(4, -1);
    TEST_ASSERT_EQUAL_UINT8(scBefore, berlin.params().scatter); // locked under Degree
}

// ---------------------------------------------------------------------------
// Structure Enc4 = AlgoPrm GATELEN cell: edits only under GatePitchPhasing,
// locked otherwise. Behavior Enc3 (Evolve rate) is GLOBAL and edits only under
// the Evolve behavior (behavior itself is global, canonical in voices_[0]).
// ---------------------------------------------------------------------------
static void test_behavior_screen_gatelen_and_evolve_locks() {
    core::AppShell shell;
    core::BerlinMode berlin(shell);
    core::Screen& str = berlin.screen(0);   // GateLen lives on Structure Enc4
    core::Screen& bh  = berlin.screen(4);   // Evolve rate on Behavior Enc3

    // GateLen edits + clamps under Phase only (edit voice = default High).
    berlin.params().algorithm = core::BerlinAlgorithm::GatePitchPhasing;
    for (int i = 0; i < 30; ++i) str.onEncoder(4, +1);
    TEST_ASSERT_EQUAL_INT(16, berlin.params().gateLen);
    for (int i = 0; i < 30; ++i) str.onEncoder(4, -1);
    TEST_ASSERT_EQUAL_INT(3, berlin.params().gateLen);
    berlin.params().algorithm = core::BerlinAlgorithm::DrunkardWalk;
    for (int i = 0; i < 5; ++i) str.onEncoder(4, +1);
    TEST_ASSERT_EQUAL_INT(3, berlin.params().gateLen);          // locked under Walk

    // Evolve rate edits only under the Evolve behavior. Behavior is global, so
    // set it on the canonical voice (Bass = voices_[0]).
    berlin.params(core::BerlinMode::kBass).behavior = core::BerlinBehavior::Locked;
    uint8_t evBefore = berlin.params().evolveRate;
    for (int i = 0; i < 3; ++i) bh.onEncoder(3, +1);
    TEST_ASSERT_EQUAL_UINT8(evBefore, berlin.params().evolveRate);  // locked
    berlin.params(core::BerlinMode::kBass).behavior = core::BerlinBehavior::Evolve;
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
    berlin.onRawInput({core::RawInput::Kind::Latch, 1, 0, false});  // prime: absorb first delivery
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
    berlin.onRawInput({core::RawInput::Kind::Latch, 1, 0, false});  // prime: absorb first Latch1 delivery
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
// Algorithm dispatch: switching the EDIT voice's (High) algorithm before a
// generate causes its engine to use the corresponding generator (reflected in
// the produced sequence). The Bass voice ignores the Algorithm knob entirely:
// it always uses the BassAnchorGenerator (step 0 = the bass root), so cycling
// ALGO and regenerating must never change Bass's generator.
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

    // The Bass voice always uses the BassAnchorGenerator, regardless of the
    // Algorithm knob: its step 0 stays the active bass root after every regen.
    const core::BerlinStep& bass0 =
        berlin.engine(core::BerlinMode::kBass).sequence().step(0);
    TEST_ASSERT_TRUE(bass0.active);
    TEST_ASSERT_EQUAL_INT(core::berlinBaseRoot(shell.scale(),
                                               berlin.params(core::BerlinMode::kBass)),
                          bass0.note);
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
    berlin.onRawInput({core::RawInput::Kind::Latch, 1, 0, false});  // prime: absorb first Latch1 delivery
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
    // Behavior is global (canonical in voices_[0]); set it there so live()==false.
    berlin.params(core::BerlinMode::kBass).behavior = core::BerlinBehavior::Locked;
    core::BerlinSequence before = berlin.engine().sequence();
    berlin.screen(3).onEncoder(1, +1);                // velocity base +1 (live)
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
    berlin.screen(0).onEncoder(3, +1);                // density +5 (structural, Enc3) but Locked
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
    berlin.onRawInput({core::RawInput::Kind::Latch, 1, 0, false});  // prime: absorb first Latch1 delivery
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
    for (int i = 0; i < 8; ++i) structure.onEncoder(3, +1);        // density (Enc3) up to 80

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
    // Behavior + resolution are now GLOBAL (canonical in voices_[0]).
    berlin.params(core::BerlinMode::kBass).behavior = core::BerlinBehavior::Locked;  // not Live
    berlin.params(core::BerlinMode::kBass).resolution = core::BerlinResolution::Sixteenth;
    berlin.syncGlobals();
    berlin.onRawInput({core::RawInput::Kind::Latch, 3, 0, false});  // prime latch absorb
    berlin.onRawInput({core::RawInput::Kind::Latch, 3, 0, true});   // generate

    // Capture notes (must not change) and the first active step's gateTicks.
    core::BerlinSequence snap = berlin.engine().sequence();
    int firstActive = -1;
    for (int i = 0; i < snap.length(); ++i) { if (snap.step(i).active) { firstActive = i; break; } }
    TEST_ASSERT_TRUE(firstActive >= 0);
    const uint16_t gateBefore = snap.step(firstActive).gateTicks;

    berlin.screen(3).onEncoder(4, +1);                 // Resolution (Dynamics Enc4) 16th → 8th

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

// ---------------------------------------------------------------------------
// Presets: a saved slot restores params AND the exact realized sequence.
// ---------------------------------------------------------------------------
static void test_berlin_preset_restores_sequence_exactly() {
    core::AppShell shell;
    FakeStorage st;
    shell.setStorage(&st);
    core::BerlinMode berlin(shell);
    berlin.onEnter();                                  // generates a sequence

    core::BerlinSequence snapshot = berlin.engine().sequence();
    const int savedLength = berlin.params().length;

    core::Screen& presets = berlin.screen(5);
    TEST_ASSERT_EQUAL_STRING("presets", presets.name());
    presets.update(1000);
    presets.onEncoderSw(1);                            // Save
    presets.onEncoder(1, +4);                          // slot index 4 (shown 05)
    presets.onEncoderSw(1);                            // confirm
    TEST_ASSERT_TRUE(st.exists("berlin.s05"));

    // Mutate the live state: Live length edit sculpts the sequence in place.
    berlin.screen(0).onEncoder(2, +8);
    TEST_ASSERT_NOT_EQUAL(savedLength, berlin.params().length);

    presets.onEncoderSw(2);                            // Load (slot remembered)
    presets.onEncoderSw(2);                            // confirm
    TEST_ASSERT_EQUAL_INT(savedLength, berlin.params().length);
    const core::BerlinSequence& got = berlin.engine().sequence();
    TEST_ASSERT_EQUAL_INT(snapshot.length(), got.length());
    for (int i = 0; i < snapshot.length(); ++i) {
        TEST_ASSERT_EQUAL_INT(snapshot.step(i).active ? 1 : 0, got.step(i).active ? 1 : 0);
        TEST_ASSERT_EQUAL_INT(snapshot.step(i).note, got.step(i).note);
        TEST_ASSERT_EQUAL_INT(snapshot.step(i).velocity, got.step(i).velocity);
        TEST_ASSERT_EQUAL_INT(snapshot.step(i).gateTicks, got.step(i).gateTicks);
    }
}

// ---------------------------------------------------------------------------
// Presets: loading mid-play keeps playing; the playhead wraps into the new
// (shorter) length instead of resetting.
// ---------------------------------------------------------------------------
static void test_berlin_preset_load_mid_play_keeps_playing() {
    core::AppShell shell;
    FakeStorage st;
    shell.setStorage(&st);
    core::BerlinMode berlin(shell);
    FakeMidiOutput out; berlin.setMidiOutput(&out);
    berlin.onEnter();

    core::Screen& presets = berlin.screen(5);
    presets.update(1000);
    presets.onEncoderSw(1);                            // save length-16 preset to slot 0
    presets.onEncoderSw(1);

    berlin.screen(0).onEncoder(2, +16);                // Live: extend to length 32
    TEST_ASSERT_EQUAL_INT(32, berlin.params().length);

    berlin.onRawInput({core::RawInput::Kind::Latch, 1, 0, false});  // prime
    berlin.onRawInput({core::RawInput::Kind::Latch, 1, 0, true});   // play
    TEST_ASSERT_TRUE(berlin.engine().isPlaying());
    for (int i = 0; i < 12 * 20; ++i) berlin.onClockTick();         // playhead ~20
    TEST_ASSERT_TRUE(berlin.engine().playhead() >= 16);

    presets.onEncoderSw(2);                            // load the length-16 preset
    presets.onEncoderSw(2);
    TEST_ASSERT_EQUAL_INT(16, berlin.params().length);
    TEST_ASSERT_TRUE(berlin.engine().isPlaying());                  // still running
    TEST_ASSERT_TRUE(berlin.engine().playhead() < 16);              // wrapped, not reset
}

// ---------------------------------------------------------------------------
// Three voices tick together and phase: Mid is 15 steps against 16, so after
// 16 steps Bass/High wrap to 0 while Mid has drifted to 1.
// ---------------------------------------------------------------------------
static void test_three_voices_tick_and_phase() {
    core::AppShell shell;
    core::BerlinMode berlin(shell);
    FakeMidiOutput out; berlin.setMidiOutput(&out);
    berlin.onEnter();
    berlin.onRawInput({core::RawInput::Kind::Latch, 1, 0, false});  // prime
    berlin.onRawInput({core::RawInput::Kind::Latch, 1, 0, true});   // play all
    TEST_ASSERT_TRUE(berlin.engine(core::BerlinMode::kBass).isPlaying());
    TEST_ASSERT_TRUE(berlin.engine(core::BerlinMode::kMid).isPlaying());
    TEST_ASSERT_TRUE(berlin.engine(core::BerlinMode::kHigh).isPlaying());
    for (int i = 0; i < 12 * 16; ++i) berlin.onClockTick();         // 16 steps
    TEST_ASSERT_EQUAL_INT(0, berlin.engine(core::BerlinMode::kBass).playhead());
    TEST_ASSERT_EQUAL_INT(1, berlin.engine(core::BerlinMode::kMid).playhead());
    TEST_ASSERT_EQUAL_INT(0, berlin.engine(core::BerlinMode::kHigh).playhead());
}

// ---------------------------------------------------------------------------
// Each voice emits on its own channel (defaults 1/2/3): play() emits every
// voice's step 0 (Bass anchor + the melodic step-0 root are always active).
// ---------------------------------------------------------------------------
static void test_voices_emit_on_their_channels() {
    core::AppShell shell;
    core::BerlinMode berlin(shell);
    FakeMidiOutput out; berlin.setMidiOutput(&out);
    berlin.onEnter();
    berlin.onRawInput({core::RawInput::Kind::Latch, 1, 0, false});
    berlin.onRawInput({core::RawInput::Kind::Latch, 1, 0, true});
    bool ch1 = false, ch2 = false, ch3 = false, other = false;
    for (const auto& ev : out.events) {
        if (!ev.isOn) continue;
        if      (ev.channel == 1) ch1 = true;
        else if (ev.channel == 2) ch2 = true;
        else if (ev.channel == 3) ch3 = true;
        else                      other = true;
    }
    TEST_ASSERT_TRUE(ch1);
    TEST_ASSERT_TRUE(ch2);
    TEST_ASSERT_TRUE(ch3);
    TEST_ASSERT_FALSE(other);
}

// ---------------------------------------------------------------------------
// The edit-voice accessors target one voice; setEditVoice switches them.
// ---------------------------------------------------------------------------
static void test_edit_voice_targets_one_voice() {
    core::AppShell shell;
    core::BerlinMode berlin(shell);
    TEST_ASSERT_EQUAL_INT(core::BerlinMode::kHigh, berlin.editVoice());
    berlin.params().density = 77;                       // edits High only
    TEST_ASSERT_EQUAL_INT(77, berlin.params(core::BerlinMode::kHigh).density);
    TEST_ASSERT_EQUAL_INT(30, berlin.params(core::BerlinMode::kBass).density);
    berlin.setEditVoice(core::BerlinMode::kBass);
    TEST_ASSERT_EQUAL_INT(30, berlin.params().density);
}

// ---------------------------------------------------------------------------
// Screen order: 0 structure, 1 character, 2 voices, 3 dynamics, 4 behavior,
// 5 presets — six screens total.
// ---------------------------------------------------------------------------
static void test_berlin_screen_order_with_voices() {
    core::AppShell shell;
    core::BerlinMode berlin(shell);
    TEST_ASSERT_EQUAL_INT(6, berlin.screenCount());
    TEST_ASSERT_EQUAL_STRING("structure", berlin.screen(0).name());
    TEST_ASSERT_EQUAL_STRING("character", berlin.screen(1).name());
    TEST_ASSERT_EQUAL_STRING("voices",    berlin.screen(2).name());
    TEST_ASSERT_EQUAL_STRING("dynamics",  berlin.screen(3).name());
    TEST_ASSERT_EQUAL_STRING("behavior",  berlin.screen(4).name());
    TEST_ASSERT_EQUAL_STRING("presets",   berlin.screen(5).name());
}

// Mixer: rotate = per-voice channel (clamped 1..16), press = mute toggle.
static void test_voices_screen_channel_and_mute() {
    core::AppShell shell;
    core::BerlinMode berlin(shell);
    FakeMidiOutput out; berlin.setMidiOutput(&out);
    berlin.onEnter();
    core::Screen& mixer = berlin.screen(2);

    mixer.onEncoder(2, +3);                                // Mid: 2 -> 5
    TEST_ASSERT_EQUAL_INT(5, berlin.voiceChannel(core::BerlinMode::kMid));
    mixer.onEncoder(1, -5);                                // Bass clamps at 1
    TEST_ASSERT_EQUAL_INT(1, berlin.voiceChannel(core::BerlinMode::kBass));

    TEST_ASSERT_FALSE(berlin.engine(core::BerlinMode::kHigh).muted());
    mixer.onEncoderSw(3);                                  // mute High
    TEST_ASSERT_TRUE(berlin.engine(core::BerlinMode::kHigh).muted());
    mixer.onEncoderSw(3);                                  // unmute
    TEST_ASSERT_FALSE(berlin.engine(core::BerlinMode::kHigh).muted());

    StubDisplay d;
    mixer.render(d);
    TEST_ASSERT_TRUE(d.drewText("BASS"));
    TEST_ASSERT_TRUE(d.drewText("MID"));
    TEST_ASSERT_TRUE(d.drewText("HIGH"));
}

// Muting a voice from the mixer silences only that voice; it keeps phase and
// resumes exactly where the others are.
static void test_mute_keeps_phase_other_voices_sound() {
    core::AppShell shell;
    core::BerlinMode berlin(shell);
    FakeMidiOutput out; berlin.setMidiOutput(&out);
    berlin.onEnter();
    berlin.onRawInput({core::RawInput::Kind::Latch, 1, 0, false});
    berlin.onRawInput({core::RawInput::Kind::Latch, 1, 0, true});
    berlin.screen(2).onEncoderSw(1);                        // mute Bass
    const size_t mark = out.events.size();
    for (int i = 0; i < 12 * 8; ++i) berlin.onClockTick();
    bool otherOn = false;
    for (size_t i = mark; i < out.events.size(); ++i) {
        // Muted bass: FULLY silent — no NoteOns and no stray gate NoteOffs.
        TEST_ASSERT_NOT_EQUAL(1, out.events[i].channel);
        if (out.events[i].isOn) otherOn = true;
    }
    TEST_ASSERT_TRUE(otherOn);
    // Muted engine kept running in phase with the others.
    TEST_ASSERT_EQUAL_INT(berlin.engine(core::BerlinMode::kHigh).playhead(),
                          berlin.engine(core::BerlinMode::kBass).playhead());
}

// ---------------------------------------------------------------------------
// On the per-voice screens (structure/character) each of Enc1/2/3 selects a
// voice directly (Bass/Mid/High); Enc4 toggles mute of the selected voice.
// Global screens (dynamics/behavior) ignore the press.
// ---------------------------------------------------------------------------
static void test_encoder_press_selects_voice_and_mutes() {
    core::AppShell shell;
    core::BerlinMode berlin(shell);
    TEST_ASSERT_EQUAL_INT(core::BerlinMode::kHigh, berlin.editVoice());

    berlin.screen(0).onEncoderSw(1);                       // structure Enc1 -> Bass
    TEST_ASSERT_EQUAL_INT(core::BerlinMode::kBass, berlin.editVoice());
    berlin.screen(1).onEncoderSw(2);                       // character Enc2 -> Mid
    TEST_ASSERT_EQUAL_INT(core::BerlinMode::kMid, berlin.editVoice());
    berlin.screen(0).onEncoderSw(3);                       // structure Enc3 -> High
    TEST_ASSERT_EQUAL_INT(core::BerlinMode::kHigh, berlin.editVoice());

    // Enc4 toggles mute of the SELECTED voice (High here).
    TEST_ASSERT_FALSE(berlin.engine(core::BerlinMode::kHigh).muted());
    berlin.screen(0).onEncoderSw(4);
    TEST_ASSERT_TRUE(berlin.engine(core::BerlinMode::kHigh).muted());
    berlin.screen(1).onEncoderSw(4);                       // character Enc4 also toggles
    TEST_ASSERT_FALSE(berlin.engine(core::BerlinMode::kHigh).muted());

    // Global screens ignore the press: voice stays High, no mute change.
    berlin.screen(3).onEncoderSw(1);                       // dynamics
    TEST_ASSERT_EQUAL_INT(core::BerlinMode::kHigh, berlin.editVoice());
    berlin.screen(4).onEncoderSw(4);                       // behavior
    TEST_ASSERT_FALSE(berlin.engine(core::BerlinMode::kHigh).muted());
}

// Structure Enc3 is now Density, Enc4 the algorithm-specific parameter
// (Scatter under Walk / GateLen under Phase); ALGO and ALGOPRM are locked
// for the Bass voice.
static void test_structure_new_layout_and_bass_locks() {
    core::AppShell shell;
    core::BerlinMode berlin(shell);
    berlin.setEditVoice(core::BerlinMode::kHigh);
    berlin.screen(0).onEncoder(3, +2);                     // density +10
    TEST_ASSERT_EQUAL_INT(60, berlin.params(core::BerlinMode::kHigh).density);
    berlin.screen(0).onEncoder(4, +2);                     // Walk: scatter 3 -> 5
    TEST_ASSERT_EQUAL_INT(5, berlin.params(core::BerlinMode::kHigh).scatter);
    berlin.params(core::BerlinMode::kHigh).algorithm =
        core::BerlinAlgorithm::GatePitchPhasing;
    berlin.screen(0).onEncoder(4, +2);                     // Phase: gateLen 6 -> 8
    TEST_ASSERT_EQUAL_INT(8, berlin.params(core::BerlinMode::kHigh).gateLen);

    berlin.setEditVoice(core::BerlinMode::kBass);
    const auto algoBefore = berlin.params(core::BerlinMode::kBass).algorithm;
    berlin.screen(0).onEncoder(1, +1);                     // ALGO locked for Bass
    TEST_ASSERT_EQUAL_INT(static_cast<int>(algoBefore),
        static_cast<int>(berlin.params(core::BerlinMode::kBass).algorithm));
    berlin.screen(0).onEncoder(4, +2);                     // ALGOPRM locked for Bass
    TEST_ASSERT_EQUAL_INT(3, berlin.params(core::BerlinMode::kBass).scatter);
}

// Dynamics and behavior are global: one knob writes every voice's params.
static void test_dynamics_and_behavior_are_global() {
    core::AppShell shell;
    core::BerlinMode berlin(shell);
    berlin.onEnter();
    berlin.screen(3).onEncoder(1, +10);                    // velocity 100 -> 110
    for (int v = 0; v < core::BerlinMode::kVoices; ++v)
        TEST_ASSERT_EQUAL_INT(110, berlin.params(v).velocityBase);
    berlin.screen(3).onEncoder(4, +1);                     // resolution 8th -> 16th
    for (int v = 0; v < core::BerlinMode::kVoices; ++v)
        TEST_ASSERT_EQUAL_INT(static_cast<int>(core::BerlinResolution::Sixteenth),
                              static_cast<int>(berlin.params(v).resolution));
    berlin.screen(4).onEncoder(1, +1);                     // behavior Live -> Lock
    for (int v = 0; v < core::BerlinMode::kVoices; ++v)
        TEST_ASSERT_EQUAL_INT(static_cast<int>(core::BerlinBehavior::Locked),
                              static_cast<int>(berlin.params(v).behavior));
}

// Phasing is visible: voices of different lengths get different column
// widths, so after a few steps their playhead rects sit at different x.
static void test_roll_playheads_drift_apart() {
    core::AppShell shell;
    core::BerlinMode berlin(shell);
    FakeMidiOutput out; berlin.setMidiOutput(&out);
    berlin.onEnter();
    berlin.onRawInput({core::RawInput::Kind::Latch, 1, 0, false});
    berlin.onRawInput({core::RawInput::Kind::Latch, 1, 0, true});
    for (int i = 0; i < 12 * 10; ++i) berlin.onClockTick();   // 10 steps in
    // Mid (len 15) and High (len 16) are both at playhead 10 but their
    // column widths differ, so the playhead x positions must differ.
    const int kbW = core::kBerlinKbW;
    const int rollW = 320 - kbW;
    const int phMid  = berlin.engine(core::BerlinMode::kMid).playhead();
    const int phHigh = berlin.engine(core::BerlinMode::kHigh).playhead();
    const int xMid  = kbW + phMid  * (rollW / 15);
    const int xHigh = kbW + phHigh * (rollW / 16);
    TEST_ASSERT_TRUE(xMid != xHigh);
    StubDisplay d;
    berlin.screen(0).render(d);
    bool sawMid = false, sawHigh = false;
    for (const auto& r : d.rects) {
        if (r.x == xMid)  sawMid  = true;
        if (r.x == xHigh) sawHigh = true;
    }
    TEST_ASSERT_TRUE(sawMid);
    TEST_ASSERT_TRUE(sawHigh);
}

// Per-voice cells show all three voice values stacked (Bass/Mid/High); the
// active voice's value is white, the other two greyed. Uses the OCT cell,
// whose three values (Bass C1 / Mid C3 / High C4) are all distinct.
static void test_per_voice_cells_show_all_three_highlighted() {
    core::AppShell shell;
    core::BerlinMode berlin(shell);            // edit voice defaults to High
    core::Screen& character = berlin.screen(1);

    StubDisplay d;
    character.render(d);
    TEST_ASSERT_TRUE(d.drewText("C1"));        // Bass octave shown
    TEST_ASSERT_TRUE(d.drewText("C3"));        // Mid
    TEST_ASSERT_TRUE(d.drewText("C4"));        // High
    TEST_ASSERT_EQUAL_HEX16(core::color::White,    d.textColor("C4"));   // High active
    TEST_ASSERT_EQUAL_HEX16(core::color::DarkGray, d.textColor("C1"));   // Bass dimmed

    character.onEncoderSw(1);                   // select Bass
    StubDisplay d2;
    character.render(d2);
    TEST_ASSERT_EQUAL_HEX16(core::color::White,    d2.textColor("C1"));  // Bass now active
    TEST_ASSERT_EQUAL_HEX16(core::color::DarkGray, d2.textColor("C4"));  // High dimmed
}

// The combined roll labels the edited voice; rendering any param screen
// draws it (StubDisplay sees the voice name drawn by the roll).
static void test_param_screens_draw_multi_roll_with_voice_label() {
    core::AppShell shell;
    core::BerlinMode berlin(shell);
    berlin.onEnter();
    StubDisplay d;
    berlin.screen(0).render(d);
    TEST_ASSERT_TRUE(d.drewText("HIGH"));                  // edit-voice label
    berlin.screen(0).onEncoderSw(1);                       // cycle to Bass
    StubDisplay d2;
    berlin.screen(1).render(d2);
    TEST_ASSERT_TRUE(d2.drewText("BASS"));
}

// An incoming NoteOn transposes all three voices diatonically (latched): the
// global scale defaults to C major, so D (62) is +1 degree above the
// reference tonic at MIDI 60. NoteOff is ignored; the reference tonic returns
// home; onEnter resets to home.
static void test_midi_in_transposes_all_voices() {
    core::AppShell shell;
    core::BerlinMode berlin(shell);
    FakeMidiOutput out; berlin.setMidiOutput(&out);
    berlin.onEnter();

    core::MidiMessage on{}; on.type = core::MidiType::NoteOn;
    on.channel = 1; on.data1 = 62; on.data2 = 100;     // D above tonic 60
    berlin.onMidiIn(on);
    for (int v = 0; v < core::BerlinMode::kVoices; ++v)
        TEST_ASSERT_EQUAL_INT(1, berlin.engine(v).transposeDegrees());

    // NoteOff is ignored (latched).
    core::MidiMessage off{}; off.type = core::MidiType::NoteOff;
    off.channel = 1; off.data1 = 62;
    berlin.onMidiIn(off);
    TEST_ASSERT_EQUAL_INT(1, berlin.engine(core::BerlinMode::kHigh).transposeDegrees());

    // A NoteOn with velocity 0 is also a note-off -> ignored.
    core::MidiMessage on0{}; on0.type = core::MidiType::NoteOn;
    on0.channel = 1; on0.data1 = 64; on0.data2 = 0;
    berlin.onMidiIn(on0);
    TEST_ASSERT_EQUAL_INT(1, berlin.engine(core::BerlinMode::kHigh).transposeDegrees());

    // Playing the reference tonic (60) returns home.
    core::MidiMessage home{}; home.type = core::MidiType::NoteOn;
    home.channel = 1; home.data1 = 60; home.data2 = 100;
    berlin.onMidiIn(home);
    for (int v = 0; v < core::BerlinMode::kVoices; ++v)
        TEST_ASSERT_EQUAL_INT(0, berlin.engine(v).transposeDegrees());

    // Transpose, then re-enter the mode -> reset to home.
    berlin.onMidiIn(on);
    TEST_ASSERT_EQUAL_INT(1, berlin.engine(core::BerlinMode::kHigh).transposeDegrees());
    berlin.onEnter();
    TEST_ASSERT_EQUAL_INT(0, berlin.engine(core::BerlinMode::kHigh).transposeDegrees());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_roll_playheads_drift_apart);
    RUN_TEST(test_param_screens_draw_multi_roll_with_voice_label);
    RUN_TEST(test_three_voices_tick_and_phase);
    RUN_TEST(test_voices_emit_on_their_channels);
    RUN_TEST(test_edit_voice_targets_one_voice);
    RUN_TEST(test_latch1_play_pause_latch2_stop);
    RUN_TEST(test_berlin_preset_restores_sequence_exactly);
    RUN_TEST(test_berlin_preset_load_mid_play_keeps_playing);
    RUN_TEST(test_berlin_latch1_click_toggles);
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
    RUN_TEST(test_berlin_screen_order_with_voices);
    RUN_TEST(test_voices_screen_channel_and_mute);
    RUN_TEST(test_mute_keeps_phase_other_voices_sound);
    RUN_TEST(test_encoder_press_selects_voice_and_mutes);
    RUN_TEST(test_per_voice_cells_show_all_three_highlighted);
    RUN_TEST(test_structure_new_layout_and_bass_locks);
    RUN_TEST(test_dynamics_and_behavior_are_global);
    RUN_TEST(test_midi_in_transposes_all_voices);
    return UNITY_END();
}
