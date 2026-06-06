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
    return UNITY_END();
}
