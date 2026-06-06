// Tests for core::ArpEngine — clock-driven step scheduler.
//
// bpm=120, Quarter=24 ticks → msPerStep = 24*60000/(120*24) = 500 ms
// bpm=120, Sixteenth=6 ticks → msPerStep = 6*60000/(120*24) = 125 ms
//
// UpDown ping-pong (endpoints NOT doubled):
//   seq=[60,64,67] → 60,64,67,64,60,64,67,...
//
// Random: LCG state=0x12345, no-immediate-repeat rule: if raw%seqLen == prevIdx
//   then advance by 1 (mod seqLen). seqLen>1 guarantees no consecutive repeats.

#include <unity.h>

#include "core/ArpEngine.h"
#include "core/ArpTypes.h"
#include "core/Scale.h"
#include "support/FakeMidiOutput.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static FakeMidiOutput* g_out = nullptr;
static core::Scale*    g_scale = nullptr;
static core::ArpEngine* g_eng = nullptr;

void setUp() {
    g_out   = new FakeMidiOutput();
    g_scale = new core::Scale(core::Scale::Type::Major, 0);  // C major
    g_eng   = new core::ArpEngine();
    g_eng->setOutput(g_out);
    g_eng->setScale(g_scale);
    g_eng->setBpm(120);
    g_eng->setOutChannel(1);
}

void tearDown() {
    delete g_eng;   g_eng   = nullptr;
    delete g_scale; g_scale = nullptr;
    delete g_out;   g_out   = nullptr;
}

// Grab only note-on/note-off events (ignore any transport noise) from position `from`.
static std::vector<FakeMidiOutput::Ev> noteEventsFrom(int from) {
    return std::vector<FakeMidiOutput::Ev>(
        g_out->events.begin() + from, g_out->events.end());
}

// ---------------------------------------------------------------------------
// Test: stepping/Up direction — immediate noteOn, correct timing
// ---------------------------------------------------------------------------
static void test_stepping_up_sequence() {
    core::ArpParams p;
    p.steps         = 3;
    p.rate          = core::ArpRate::Quarter;    // 500 ms/step at 120 BPM
    p.gatePercent   = 100;                        // NoteOff at the last possible moment
    p.direction     = core::ArpDirection::Up;
    p.velocityMode  = core::ArpVelocityMode::Fixed;
    p.fixedVelocity = 100;
    p.swingPercent  = 50;                         // no swing
    g_eng->setParams(p);

    // noteOn at t=0 → immediate NoteOn(ch1, 60, 100)
    g_eng->noteOn(60, 100, 0);
    TEST_ASSERT_EQUAL_INT(1, (int)g_out->events.size());
    TEST_ASSERT_TRUE(g_out->events[0].isOn);
    TEST_ASSERT_EQUAL_INT(1,   g_out->events[0].channel);
    TEST_ASSERT_EQUAL_INT(60,  g_out->events[0].note);
    TEST_ASSERT_EQUAL_INT(100, g_out->events[0].vel);

    // t=499 → no new events
    g_eng->tick(499);
    TEST_ASSERT_EQUAL_INT(1, (int)g_out->events.size());

    // t=500 → NoteOff(60) + NoteOn(64)
    g_eng->tick(500);
    {
        auto ev = noteEventsFrom(1);
        // At minimum: NoteOff(60) then NoteOn(64)
        bool hasNoteOff60 = false, hasNoteOn64 = false;
        int off60idx = -1, on64idx = -1;
        for (int i = 0; i < (int)ev.size(); ++i) {
            if (!ev[i].isOn && ev[i].note == 60) { hasNoteOff60 = true; off60idx = i; }
            if ( ev[i].isOn && ev[i].note == 64) { hasNoteOn64  = true; on64idx  = i; }
        }
        TEST_ASSERT_TRUE(hasNoteOff60);
        TEST_ASSERT_TRUE(hasNoteOn64);
        TEST_ASSERT_TRUE(off60idx < on64idx);  // NoteOff before NoteOn
    }

    // t=1000 → NoteOff(64) + NoteOn(67)
    g_eng->tick(1000);
    {
        bool found67 = false;
        for (auto& e : g_out->events) {
            if (e.isOn && e.note == 67) { found67 = true; break; }
        }
        TEST_ASSERT_TRUE(found67);
    }

    // t=1500 → NoteOff(67) + NoteOn(60) (wrap back to start)
    g_eng->tick(1500);
    {
        // Count NoteOns for note 60
        int count60 = 0;
        for (auto& e : g_out->events) {
            if (e.isOn && e.note == 60) ++count60;
        }
        TEST_ASSERT_EQUAL_INT(2, count60);  // first at t=0, again at t=1500
    }
}

// ---------------------------------------------------------------------------
// Test: gate percent — NoteOff fires early, next NoteOn fires at step boundary
// ---------------------------------------------------------------------------
static void test_gate_50_percent() {
    core::ArpParams p;
    p.steps         = 3;
    p.rate          = core::ArpRate::Quarter;   // 500 ms
    p.gatePercent   = 50;                        // NoteOff at 250 ms
    p.direction     = core::ArpDirection::Up;
    p.velocityMode  = core::ArpVelocityMode::Fixed;
    p.fixedVelocity = 100;
    p.swingPercent  = 50;
    g_eng->setParams(p);

    g_eng->noteOn(60, 100, 0);
    // Immediately: NoteOn(60) at t=0
    TEST_ASSERT_EQUAL_INT(1, (int)g_out->events.size());
    TEST_ASSERT_TRUE(g_out->events[0].isOn);

    // t=250 → NoteOff(60) fires (gate=50% of 500ms=250ms), but NO NoteOn yet
    g_eng->tick(250);
    {
        bool hasOff60 = false, hasNewOn = false;
        for (int i = 1; i < (int)g_out->events.size(); ++i) {
            if (!g_out->events[i].isOn && g_out->events[i].note == 60) hasOff60 = true;
            if ( g_out->events[i].isOn) hasNewOn = true;
        }
        TEST_ASSERT_TRUE(hasOff60);
        TEST_ASSERT_FALSE(hasNewOn);
    }

    // t=500 → NoteOn(64) starts (step boundary)
    g_eng->tick(500);
    {
        bool has64 = false;
        for (auto& e : g_out->events) {
            if (e.isOn && e.note == 64) { has64 = true; break; }
        }
        TEST_ASSERT_TRUE(has64);
    }
}

// ---------------------------------------------------------------------------
// Test: Direction=Down — highest note first
// ---------------------------------------------------------------------------
static void test_direction_down() {
    core::ArpParams p;
    p.steps         = 3;
    p.rate          = core::ArpRate::Quarter;
    p.gatePercent   = 80;
    p.direction     = core::ArpDirection::Down;
    p.velocityMode  = core::ArpVelocityMode::Fixed;
    p.fixedVelocity = 100;
    p.swingPercent  = 50;
    g_eng->setParams(p);

    g_eng->noteOn(60, 100, 0);
    g_eng->tick(500);
    g_eng->tick(1000);

    // Collect only NoteOn events in order
    std::vector<uint8_t> noteOns;
    for (auto& e : g_out->events) {
        if (e.isOn) noteOns.push_back(e.note);
    }
    TEST_ASSERT_EQUAL_INT(3, (int)noteOns.size());
    TEST_ASSERT_EQUAL_INT(67, noteOns[0]);  // highest first
    TEST_ASSERT_EQUAL_INT(64, noteOns[1]);
    TEST_ASSERT_EQUAL_INT(60, noteOns[2]);  // root last
}

// ---------------------------------------------------------------------------
// Test: Direction=UpDown — ping-pong, endpoints not doubled
// Sequence: 60,64,67,64,60,64,67,...
// ---------------------------------------------------------------------------
static void test_direction_updown() {
    core::ArpParams p;
    p.steps         = 3;
    p.rate          = core::ArpRate::Sixteenth;  // 125 ms
    p.gatePercent   = 80;
    p.direction     = core::ArpDirection::UpDown;
    p.velocityMode  = core::ArpVelocityMode::Fixed;
    p.fixedVelocity = 100;
    p.swingPercent  = 50;
    g_eng->setParams(p);

    g_eng->noteOn(60, 100, 0);
    // Drive 4 more steps: t=125,250,375,500
    g_eng->tick(125);
    g_eng->tick(250);
    g_eng->tick(375);
    g_eng->tick(500);

    // Collect NoteOn notes: should be 60,64,67,64,60
    std::vector<uint8_t> noteOns;
    for (auto& e : g_out->events) {
        if (e.isOn) noteOns.push_back(e.note);
    }
    TEST_ASSERT_EQUAL_INT(5, (int)noteOns.size());
    TEST_ASSERT_EQUAL_INT(60, noteOns[0]);
    TEST_ASSERT_EQUAL_INT(64, noteOns[1]);
    TEST_ASSERT_EQUAL_INT(67, noteOns[2]);
    TEST_ASSERT_EQUAL_INT(64, noteOns[3]);
    TEST_ASSERT_EQUAL_INT(60, noteOns[4]);
}

// ---------------------------------------------------------------------------
// Test: Direction=Random — no two consecutive NoteOns are the same note
// ---------------------------------------------------------------------------
static void test_direction_random_no_immediate_repeat() {
    core::ArpParams p;
    p.steps         = 3;
    p.rate          = core::ArpRate::Sixteenth;  // 125 ms
    p.gatePercent   = 80;
    p.direction     = core::ArpDirection::Random;
    p.velocityMode  = core::ArpVelocityMode::Fixed;
    p.fixedVelocity = 100;
    p.swingPercent  = 50;
    g_eng->setParams(p);

    g_eng->noteOn(60, 100, 0);
    // Drive 29 more steps (total 30)
    for (int i = 1; i <= 29; ++i) {
        g_eng->tick(static_cast<uint32_t>(i * 125));
    }

    // Collect NoteOn notes
    std::vector<uint8_t> noteOns;
    for (auto& e : g_out->events) {
        if (e.isOn) noteOns.push_back(e.note);
    }
    TEST_ASSERT_EQUAL_INT(30, (int)noteOns.size());

    // No two consecutive are equal (seqLen=3 so this is always achievable)
    bool hasRepeat = false;
    for (int i = 0; i + 1 < (int)noteOns.size(); ++i) {
        if (noteOns[i] == noteOns[i + 1]) { hasRepeat = true; break; }
    }
    TEST_ASSERT_FALSE(hasRepeat);
}

// ---------------------------------------------------------------------------
// Test: Velocity=Fixed — all steps emit fixedVelocity
// ---------------------------------------------------------------------------
static void test_velocity_fixed() {
    core::ArpParams p;
    p.steps         = 3;
    p.rate          = core::ArpRate::Sixteenth;
    p.gatePercent   = 80;
    p.direction     = core::ArpDirection::Up;
    p.velocityMode  = core::ArpVelocityMode::Fixed;
    p.fixedVelocity = 77;
    p.swingPercent  = 50;
    g_eng->setParams(p);

    g_eng->noteOn(60, 99, 0);  // input vel != fixedVelocity
    g_eng->tick(125);
    g_eng->tick(250);

    for (auto& e : g_out->events) {
        if (e.isOn) {
            TEST_ASSERT_EQUAL_INT(77, e.vel);
        }
    }
}

// ---------------------------------------------------------------------------
// Test: Velocity=FollowInput — all steps use rootVel_
// ---------------------------------------------------------------------------
static void test_velocity_follow_input() {
    core::ArpParams p;
    p.steps         = 3;
    p.rate          = core::ArpRate::Sixteenth;
    p.gatePercent   = 80;
    p.direction     = core::ArpDirection::Up;
    p.velocityMode  = core::ArpVelocityMode::FollowInput;
    p.fixedVelocity = 100;
    p.swingPercent  = 50;
    g_eng->setParams(p);

    g_eng->noteOn(60, 77, 0);  // input velocity = 77
    g_eng->tick(125);
    g_eng->tick(250);

    for (auto& e : g_out->events) {
        if (e.isOn) {
            TEST_ASSERT_EQUAL_INT(77, e.vel);
        }
    }
}

// ---------------------------------------------------------------------------
// Test: Velocity=Accent — step 0 is louder than other steps
// ---------------------------------------------------------------------------
static void test_velocity_accent() {
    core::ArpParams p;
    p.steps         = 3;
    p.rate          = core::ArpRate::Sixteenth;
    p.gatePercent   = 80;
    p.direction     = core::ArpDirection::Up;
    p.velocityMode  = core::ArpVelocityMode::Accent;
    p.fixedVelocity = 80;
    p.swingPercent  = 50;
    g_eng->setParams(p);

    g_eng->noteOn(60, 100, 0);
    g_eng->tick(125);
    g_eng->tick(250);

    // Collect NoteOn velocities
    std::vector<uint8_t> vels;
    for (auto& e : g_out->events) {
        if (e.isOn) vels.push_back(e.vel);
    }
    TEST_ASSERT_EQUAL_INT(3, (int)vels.size());
    // Step 0 (accent) must be louder than steps 1 and 2 (base)
    TEST_ASSERT_GREATER_THAN(vels[1], vels[0]);
    TEST_ASSERT_EQUAL_INT(vels[1], vels[2]);
}

// ---------------------------------------------------------------------------
// Test: stop() sends NoteOff for sounding note and sets active=false
// ---------------------------------------------------------------------------
static void test_stop_kills_active_note() {
    core::ArpParams p;
    p.steps         = 3;
    p.rate          = core::ArpRate::Quarter;
    p.gatePercent   = 100;
    p.direction     = core::ArpDirection::Up;
    p.velocityMode  = core::ArpVelocityMode::Fixed;
    p.fixedVelocity = 100;
    p.swingPercent  = 50;
    g_eng->setParams(p);

    g_eng->noteOn(60, 100, 0);
    TEST_ASSERT_TRUE(g_eng->isPlaying());

    g_eng->stop();
    TEST_ASSERT_FALSE(g_eng->isPlaying());

    // There should be a NoteOff for note 60
    bool hasOff60 = false;
    for (auto& e : g_out->events) {
        if (!e.isOn && e.note == 60) { hasOff60 = true; break; }
    }
    TEST_ASSERT_TRUE(hasOff60);
}

// ---------------------------------------------------------------------------
// Test: steps=1 + UpDown — no crash, only note 60 emitted
// Fix-1 guard: seqLen_==1 → nextSeqIndex returns 0 immediately, no OOB.
// ---------------------------------------------------------------------------
static void test_updown_single_step_no_crash() {
    core::ArpParams p;
    p.steps         = 1;
    p.rate          = core::ArpRate::Quarter;  // 500 ms/step
    p.gatePercent   = 80;
    p.direction     = core::ArpDirection::UpDown;
    p.velocityMode  = core::ArpVelocityMode::Fixed;
    p.fixedVelocity = 100;
    p.swingPercent  = 50;
    g_eng->setParams(p);

    g_eng->noteOn(60, 100, 0);
    g_eng->tick(500);
    g_eng->tick(1000);
    g_eng->tick(1500);

    // Every NoteOn must be note 60 (the only step in the sequence)
    int countOn = 0;
    for (auto& e : g_out->events) {
        if (e.isOn) {
            TEST_ASSERT_EQUAL_INT(60, e.note);
            ++countOn;
        }
    }
    // 4 steps fired (t=0, 500, 1000, 1500)
    TEST_ASSERT_EQUAL_INT(4, countOn);
}

// ---------------------------------------------------------------------------
// Test: steps=1 + DownUp — no crash, only note 60 emitted
// ---------------------------------------------------------------------------
static void test_downup_single_step_no_crash() {
    core::ArpParams p;
    p.steps         = 1;
    p.rate          = core::ArpRate::Quarter;  // 500 ms/step
    p.gatePercent   = 80;
    p.direction     = core::ArpDirection::DownUp;
    p.velocityMode  = core::ArpVelocityMode::Fixed;
    p.fixedVelocity = 100;
    p.swingPercent  = 50;
    g_eng->setParams(p);

    g_eng->noteOn(60, 100, 0);
    g_eng->tick(500);
    g_eng->tick(1000);
    g_eng->tick(1500);

    // Every NoteOn must be note 60 (the only step in the sequence)
    int countOn = 0;
    for (auto& e : g_out->events) {
        if (e.isOn) {
            TEST_ASSERT_EQUAL_INT(60, e.note);
            ++countOn;
        }
    }
    // 4 steps fired (t=0, 500, 1000, 1500)
    TEST_ASSERT_EQUAL_INT(4, countOn);
}

// ---------------------------------------------------------------------------
// Test: Direction=DownUp happy-path (steps=3)
// seq=[60,64,67], starts at top (67), bounce: 67,64,60,64,67
// ---------------------------------------------------------------------------
static void test_direction_downup() {
    core::ArpParams p;
    p.steps         = 3;
    p.rate          = core::ArpRate::Sixteenth;  // 125 ms
    p.gatePercent   = 80;
    p.direction     = core::ArpDirection::DownUp;
    p.velocityMode  = core::ArpVelocityMode::Fixed;
    p.fixedVelocity = 100;
    p.swingPercent  = 50;
    g_eng->setParams(p);

    g_eng->noteOn(60, 100, 0);
    g_eng->tick(125);
    g_eng->tick(250);
    g_eng->tick(375);
    g_eng->tick(500);

    std::vector<uint8_t> noteOns;
    for (auto& e : g_out->events) {
        if (e.isOn) noteOns.push_back(e.note);
    }
    TEST_ASSERT_EQUAL_INT(5, (int)noteOns.size());
    TEST_ASSERT_EQUAL_INT(67, noteOns[0]);
    TEST_ASSERT_EQUAL_INT(64, noteOns[1]);
    TEST_ASSERT_EQUAL_INT(60, noteOns[2]);
    TEST_ASSERT_EQUAL_INT(64, noteOns[3]);
    TEST_ASSERT_EQUAL_INT(67, noteOns[4]);
}

// ---------------------------------------------------------------------------
// Test: Swing delays odd steps
// bpm=120, Quarter=500ms/step, swingPercent=75
// swing offset = (75-50)*500/100 = 125 ms
// Even step 0 fires at t=0 (no swing).
// Odd step 1 fires at t=500+125=625 ms (swing delay applied at step 0's nextStepMs_).
// At t=500: only step 0 (note 60) has been emitted.
// At t=625: step 1 (note 64) fires.
// ---------------------------------------------------------------------------
static void test_swing_delays_odd_steps() {
    core::ArpParams p;
    p.steps         = 3;
    p.rate          = core::ArpRate::Quarter;  // 500 ms/step
    p.gatePercent   = 50;
    p.direction     = core::ArpDirection::Up;
    p.velocityMode  = core::ArpVelocityMode::Fixed;
    p.fixedVelocity = 100;
    p.swingPercent  = 75;  // odd-step delay = (75-50)*500/100 = 125 ms
    g_eng->setParams(p);

    g_eng->noteOn(60, 100, 0);  // step 0 fires immediately (stepCount_=0 is even)

    // At t=500: step 0's un-swung boundary, but nextStepMs_ was pushed to 625
    // because stepCount_ was 0 (even? no — swing applies when (stepCount_ & 1)).
    // stepCount_ is incremented AFTER the swing check inside beginStep().
    // At step 0: stepCount_=0, (0 & 1)==0 → no swing → nextStepMs_=500.
    // At step 1 (fires at t=500): stepCount_=1, (1 & 1)==1 → swing applied →
    //   nextStepMs_ for step 2 = 500+500+125=1125.
    // So the 2nd NoteOn (note 64) appears at t=500; the 3rd (note 67) at t=1125.
    // But the task asks: does t=500 NOT have note 64 yet?
    // Re-check: step 0 fires at t=0 (beginStep called from noteOn at t=0).
    //   stepCount_ incremented to 1 after. nextStepMs_=0+500=500 (stepCount_was 0, even).
    // t=500: tick fires beginStep (step 1).
    //   stepCount_=1, (1&1)==1 → swing: nextStepMs_=500+500+125=1125.
    //   NoteOn(64) fires at t=500.
    // So at t=500 note 64 IS present. The swing affects step 2's boundary, not step 1's.
    //
    // For the test to check swing delays the odd-indexed step's OWN start,
    // we need to check step 2 (stepCount_=2, even → no swing on its OWN boundary;
    // that was set when step 1 ran with swing=125 → step2 fires at 1125 not 1000).
    // Assert: at t=1000 note 67 has NOT fired; at t=1125 it has.

    g_eng->tick(500);   // step 1 (note 64) fires here; stepCount_ was 1 → swing sets step2 at 1125

    // Count NoteOns so far: 2 (notes 60 and 64)
    {
        int countOn = 0;
        for (auto& e : g_out->events) { if (e.isOn) ++countOn; }
        TEST_ASSERT_EQUAL_INT(2, countOn);
    }

    // t=1000: un-swung boundary for step 2, but swing pushed it to 1125
    g_eng->tick(1000);
    {
        bool has67 = false;
        for (auto& e : g_out->events) { if (e.isOn && e.note == 67) has67 = true; }
        TEST_ASSERT_FALSE(has67);  // note 67 should not have fired yet
    }

    // t=1125: swing boundary — note 67 must now be present
    g_eng->tick(1125);
    {
        bool has67 = false;
        for (auto& e : g_out->events) { if (e.isOn && e.note == 67) has67 = true; }
        TEST_ASSERT_TRUE(has67);
    }
}

// ---------------------------------------------------------------------------
// Helper: collect only NoteOn notes in emission order.
// ---------------------------------------------------------------------------
static std::vector<uint8_t> noteOnSequence() {
    std::vector<uint8_t> result;
    for (auto& e : g_out->events) {
        if (e.isOn) result.push_back(e.note);
    }
    return result;
}

// Helper: make a standard 3-step Up Quarter params struct.
static core::ArpParams makeStdParams() {
    core::ArpParams p;
    p.steps         = 3;
    p.rate          = core::ArpRate::Quarter;   // 500 ms/step at 120 BPM
    p.gatePercent   = 80;
    p.direction     = core::ArpDirection::Up;
    p.velocityMode  = core::ArpVelocityMode::Fixed;
    p.fixedVelocity = 100;
    p.swingPercent  = 50;
    p.latch         = false;
    return p;
}

// ---------------------------------------------------------------------------
// Task-5 test: loop while held
//   noteOn(60) — never release.  Drive 7 steps (t=0,500,...,3000).
//   C major triad from 60 with steps=3: seq=[60,64,67] (Up).
//   Expected NoteOn order: 60,64,67,60,64,67,60  (wrap at cycle boundary).
// ---------------------------------------------------------------------------
static void test_loop_while_held() {
    g_eng->setParams(makeStdParams());

    g_eng->noteOn(60, 100, 0);      // t=0: step0=60
    g_eng->tick(500);               // t=500: step1=64
    g_eng->tick(1000);              // t=1000: step2=67  → cycle 1 done, still held → loop
    g_eng->tick(1500);              // t=1500: step0=60
    g_eng->tick(2000);              // t=2000: step1=64
    g_eng->tick(2500);              // t=2500: step2=67  → cycle 2 done, still held → loop
    g_eng->tick(3000);              // t=3000: step0=60

    auto seq = noteOnSequence();
    TEST_ASSERT_EQUAL_INT(7, (int)seq.size());
    TEST_ASSERT_EQUAL_INT(60, seq[0]);
    TEST_ASSERT_EQUAL_INT(64, seq[1]);
    TEST_ASSERT_EQUAL_INT(67, seq[2]);
    TEST_ASSERT_EQUAL_INT(60, seq[3]);
    TEST_ASSERT_EQUAL_INT(64, seq[4]);
    TEST_ASSERT_EQUAL_INT(67, seq[5]);
    TEST_ASSERT_EQUAL_INT(60, seq[6]);
}

// ---------------------------------------------------------------------------
// Task-5 test: release finishes current cycle then goes idle
//   noteOn(60) at t=0; noteOff(60,500) mid-cycle; continue ticking.
//   The arp must play through step2=67 at t=1000 to complete the cycle,
//   then at t=1500 it must NOT emit a new NoteOn (idle).
//   Also a NoteOff for the last sounding note must be emitted after t=1000.
// ---------------------------------------------------------------------------
static void test_release_finishes_cycle_then_idle() {
    g_eng->setParams(makeStdParams());

    g_eng->noteOn(60, 100, 0);          // step0=60 at t=0
    g_eng->tick(500);                   // step1=64 at t=500
    g_eng->noteOff(60, 500);            // released after step1 — cycle not done yet
    g_eng->tick(1000);                  // step2=67 at t=1000 → cycle complete

    // 67 must have been emitted after the noteOff
    {
        auto seq = noteOnSequence();
        bool has67 = false;
        for (auto n : seq) if (n == 67) { has67 = true; break; }
        TEST_ASSERT_TRUE_MESSAGE(has67, "step2=67 must still play after noteOff");
    }

    // Tick past gate NoteOff for 67, then the next step boundary
    g_eng->tick(1400);  // gate NoteOff for 67 fires here (80%*500=400ms after t=1000 → t=1400)
    g_eng->tick(1500);  // next step boundary — should NOT produce a new NoteOn

    {
        auto seq = noteOnSequence();
        // Only 3 NoteOns: 60, 64, 67
        TEST_ASSERT_EQUAL_INT_MESSAGE(3, (int)seq.size(), "no 4th NoteOn after cycle complete + release");
    }

    // Engine must be idle (not playing)
    TEST_ASSERT_FALSE(g_eng->isPlaying());

    // A NoteOff for 67 must have been emitted (either from gate or from dequeue)
    bool hasOff67 = false;
    for (auto& e : g_out->events) {
        if (!e.isOn && e.note == 67) { hasOff67 = true; break; }
    }
    TEST_ASSERT_TRUE_MESSAGE(hasOff67, "NoteOff for final note 67 must be emitted");
}

// ---------------------------------------------------------------------------
// Task-5 test: FIFO two staccato notes
//   noteOn(60) then noteOff(60) immediately (staccato, before anything plays).
//   noteOn(67) then noteOff(67) immediately.
//   Active is 60 → plays one full cycle (60,64,67), then dequeues.
//   67 becomes active → plays its triad.
//   C major from 67 with steps=3: 67,71,74.
//   Expected NoteOn prefix: 60,64,67,67,71,74.
// ---------------------------------------------------------------------------
static void test_fifo_two_staccato_notes() {
    g_eng->setParams(makeStdParams());

    g_eng->noteOn(60, 100, 0);
    g_eng->noteOff(60, 0);      // staccato — already !held when active
    g_eng->noteOn(67, 100, 0);  // appended to FIFO while 60 is active
    g_eng->noteOff(67, 0);      // staccato too

    // Drive 60's cycle: steps at t=500 and t=1000
    g_eng->tick(500);   // step1=64
    g_eng->tick(1000);  // step2=67 → 60's cycle complete → dequeue 60, promote 67

    // Drive 67's cycle: at t=1500 the new active note's first loop step
    // Wait — when 67 becomes active it fires its step0 immediately at t=1000 (same tick).
    // Then step1 at t=1500, step2 at t=2000.
    g_eng->tick(1500);  // 67's step1=71
    g_eng->tick(2000);  // 67's step2=74 → cycle complete, !held → dequeue → idle

    auto seq = noteOnSequence();
    // Expect at least 6: 60,64,67,67,71,74
    TEST_ASSERT_TRUE_MESSAGE((int)seq.size() >= 6, "expected >=6 NoteOns");
    TEST_ASSERT_EQUAL_INT(60, seq[0]);
    TEST_ASSERT_EQUAL_INT(64, seq[1]);
    TEST_ASSERT_EQUAL_INT(67, seq[2]);
    TEST_ASSERT_EQUAL_INT(67, seq[3]);
    TEST_ASSERT_EQUAL_INT(71, seq[4]);
    TEST_ASSERT_EQUAL_INT(74, seq[5]);
}

// ---------------------------------------------------------------------------
// Task-5 test: held note, then queue another, release first → switch at boundary
//   noteOn(60) held; let it complete one cycle; noteOn(67) while 60 held.
//   60 keeps looping. Then noteOff(60); after the CURRENT cycle of 60 completes,
//   67 becomes active.  Assert 67's triad appears only after a 60-cycle boundary
//   following the release.
//
//   Timeline (500ms/step, steps=3):
//     t=0:    noteOn(60)  → 60 fires
//     t=500:  tick        → 64 fires
//     t=1000: tick        → 67 fires  (60's cycle 1 complete, still held → loop)
//     t=1000: noteOn(67)  → queued
//     t=1500: tick        → 60 fires  (60's cycle 2, step0)
//     t=1500: noteOff(60) → released mid-cycle2
//     t=2000: tick        → 64 fires  (60's cycle 2, step1)
//     t=2500: tick        → 67 fires  (60's cycle 2, step2 → cycle complete → dequeue)
//     t=2500: 67 becomes active, fires its step0 (=67)
//     t=3000: tick        → 71
//     t=3500: tick        → 74
// ---------------------------------------------------------------------------
static void test_held_then_queue_switch() {
    g_eng->setParams(makeStdParams());

    g_eng->noteOn(60, 100, 0);      // t=0
    g_eng->tick(500);               // t=500: 64
    g_eng->tick(1000);              // t=1000: 67 — cycle1 done, still held
    g_eng->noteOn(67, 100, 1000);   // queue 67 while 60 is active
    g_eng->tick(1500);              // t=1500: 60 (cycle2 start)
    g_eng->noteOff(60, 1500);       // release 60 mid cycle2
    g_eng->tick(2000);              // t=2000: 64
    g_eng->tick(2500);              // t=2500: 67 (cycle2 complete → dequeue → 67 fires)
    g_eng->tick(3000);              // t=3000: 71
    g_eng->tick(3500);              // t=3500: 74

    auto seq = noteOnSequence();
    // Expected: 60,64,67, 60,64,67, 67,71,74
    //   (60's cycle1, 60's cycle2, then 67's cycle)
    TEST_ASSERT_TRUE_MESSAGE((int)seq.size() >= 9, "expected >=9 NoteOns");
    // 60 must not appear after position 5 (index 5 = last step of 60's cycle2)
    for (int i = 6; i < (int)seq.size(); ++i) {
        TEST_ASSERT_NOT_EQUAL_MESSAGE(60, seq[i], "60 must not play after it was dequeued");
    }
    // 67,71,74 must appear after 60 is done
    TEST_ASSERT_EQUAL_INT(67, seq[6]);
    TEST_ASSERT_EQUAL_INT(71, seq[7]);
    TEST_ASSERT_EQUAL_INT(74, seq[8]);
}

// ---------------------------------------------------------------------------
// Task-5 test: latch replaces at cycle boundary
//   latch=true. noteOn(60); noteOff(60) → latch ignores it, 60 keeps looping.
//   noteOn(67) → queued as pending replacement.
//   At next cycle boundary after noteOn(67), 67 becomes active; 60 stops.
//   Assert: 67's triad appears at a cycle boundary, and 60 does not play after.
//
//   Timeline (500ms/step, steps=3):
//     t=0:    noteOn(60) → 60 fires (latch mode)
//     t=500:  tick       → 64
//     t=1000: tick       → 67 (60's cycle1 complete, latch→keep looping 60)
//     t=1000: noteOff(60) → latch ignores
//     t=1000: noteOn(67) → pending replacement
//     t=1500: tick       → 60 fires (cycle2, step0)
//     t=2000: tick       → 64
//     t=2500: tick       → 67 (cycle2 complete → replace → 67 becomes active, fires step0=67)
//     t=3000: tick       → 71
//     t=3500: tick       → 74
// ---------------------------------------------------------------------------
static void test_latch_replaces_at_boundary() {
    core::ArpParams p = makeStdParams();
    p.latch = true;
    g_eng->setParams(p);

    g_eng->noteOn(60, 100, 0);
    g_eng->tick(500);
    g_eng->tick(1000);             // cycle1 complete
    g_eng->noteOff(60, 1000);      // latch: ignore
    g_eng->noteOn(67, 100, 1000);  // pending replacement
    g_eng->tick(1500);             // 60 cycle2 step0
    g_eng->tick(2000);             // 60 cycle2 step1
    g_eng->tick(2500);             // 60 cycle2 step2 → boundary → replace with 67
    g_eng->tick(3000);             // 67 step1
    g_eng->tick(3500);             // 67 step2

    auto seq = noteOnSequence();
    // 60 plays: cycle1 (60,64,67) + cycle2 (60,64,67) = 6 times
    // then 67 plays: 67,71,74
    TEST_ASSERT_TRUE_MESSAGE((int)seq.size() >= 9, "expected >=9 NoteOns");

    // After position 5 (0-indexed), 60 must not appear
    for (int i = 6; i < (int)seq.size(); ++i) {
        TEST_ASSERT_NOT_EQUAL_MESSAGE(60, seq[i], "60 must not play after latch replacement");
    }
    // 67's triad must follow
    TEST_ASSERT_EQUAL_INT(67, seq[6]);
    TEST_ASSERT_EQUAL_INT(71, seq[7]);
    TEST_ASSERT_EQUAL_INT(74, seq[8]);
}

// ---------------------------------------------------------------------------
// Task-5 test: stop() clears queue and silences everything
//   Queue two staccato notes (60, 67). After stop():
//     - isPlaying() == false
//     - A NoteOff is emitted for any sounding note
//     - Further ticks emit nothing new
// ---------------------------------------------------------------------------
static void test_stop_clears_queue() {
    g_eng->setParams(makeStdParams());

    g_eng->noteOn(60, 100, 0);   // active
    g_eng->noteOff(60, 0);       // staccato
    g_eng->noteOn(67, 100, 0);   // queued
    g_eng->noteOff(67, 0);       // staccato

    // 60 is playing step0 right now
    TEST_ASSERT_TRUE(g_eng->isPlaying());

    int eventsBefore = (int)g_out->events.size();
    g_eng->stop();

    TEST_ASSERT_FALSE(g_eng->isPlaying());

    // At least one NoteOff must have been emitted by stop()
    int noteOffsAdded = 0;
    for (int i = eventsBefore; i < (int)g_out->events.size(); ++i) {
        if (!g_out->events[i].isOn) ++noteOffsAdded;
    }
    TEST_ASSERT_GREATER_THAN(0, noteOffsAdded);

    // Further ticks must not emit anything
    int eventsAfterStop = (int)g_out->events.size();
    g_eng->tick(500);
    g_eng->tick(1000);
    g_eng->tick(1500);
    TEST_ASSERT_EQUAL_INT(eventsAfterStop, (int)g_out->events.size());
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main() {
    UNITY_BEGIN();
    RUN_TEST(test_stepping_up_sequence);
    RUN_TEST(test_gate_50_percent);
    RUN_TEST(test_direction_down);
    RUN_TEST(test_direction_updown);
    RUN_TEST(test_direction_random_no_immediate_repeat);
    RUN_TEST(test_velocity_fixed);
    RUN_TEST(test_velocity_follow_input);
    RUN_TEST(test_velocity_accent);
    RUN_TEST(test_stop_kills_active_note);
    RUN_TEST(test_updown_single_step_no_crash);
    RUN_TEST(test_downup_single_step_no_crash);
    RUN_TEST(test_direction_downup);
    RUN_TEST(test_swing_delays_odd_steps);
    // Task-5: FIFO queue + loop-while-held + latch + transport
    RUN_TEST(test_loop_while_held);
    RUN_TEST(test_release_finishes_cycle_then_idle);
    RUN_TEST(test_fifo_two_staccato_notes);
    RUN_TEST(test_held_then_queue_switch);
    RUN_TEST(test_latch_replaces_at_boundary);
    RUN_TEST(test_stop_clears_queue);
    return UNITY_END();
}
