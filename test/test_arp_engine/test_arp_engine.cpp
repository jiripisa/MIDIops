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
//
// One-shot model (hold off, params_.latch == false):
//   Each played note plays exactly ONE cycle (seqLen_ steps), then the engine
//   advances the FIFO (next queued note) or goes idle. Physical hold state is
//   irrelevant; noteOff is a no-op in both modes.
//
// Hold (latch) model (params_.latch == true):
//   Loops forever; a new noteOn stores a pending replacement; at the next cycle
//   boundary the active note is replaced by the pending one (queue cleared).

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
// Test: stepping/Up direction — immediate noteOn, correct timing, ONE cycle
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

    // t=1000: cycle complete (3 steps: 60,64,67) → one-shot model → engine goes idle.
    // t=1500 → NO new NoteOn (the engine is idle after one cycle).
    g_eng->tick(1500);
    {
        // Exactly one NoteOn for note 60 (the first step), no wrap-around.
        int count60 = 0;
        for (auto& e : g_out->events) {
            if (e.isOn && e.note == 60) ++count60;
        }
        TEST_ASSERT_EQUAL_INT_MESSAGE(1, count60,
            "hold-off: note 60 must play only once (no wrap-around after idle)");
    }
    TEST_ASSERT_FALSE_MESSAGE(g_eng->isPlaying(),
        "engine must be idle after one cycle (hold-off one-shot)");
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
// Test: Direction=Down — highest note first (one cycle of 3 steps)
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

    // Collect only NoteOn events in order (one cycle = 3 steps)
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
// Test: Direction=UpDown one-shot — only 3 steps then idle.
// In UpDown mode one cycle = seqLen_=3 steps: 60,64,67.
// Further steps (64,60 etc.) belong to the next cycle which is NOT played
// in one-shot mode (the engine goes idle after the first 3 steps).
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
    p.latch         = false;
    g_eng->setParams(p);

    g_eng->noteOn(60, 100, 0);
    g_eng->tick(125);
    g_eng->tick(250);
    g_eng->tick(375);  // step boundary after cycle completes → idle
    g_eng->tick(500);  // no new events

    std::vector<uint8_t> noteOns;
    for (auto& e : g_out->events) {
        if (e.isOn) noteOns.push_back(e.note);
    }
    // One cycle = 3 steps: 60,64,67 then idle
    TEST_ASSERT_EQUAL_INT_MESSAGE(3, (int)noteOns.size(),
        "UpDown one-shot: exactly 3 NoteOns (one cycle)");
    TEST_ASSERT_EQUAL_INT(60, noteOns[0]);
    TEST_ASSERT_EQUAL_INT(64, noteOns[1]);
    TEST_ASSERT_EQUAL_INT(67, noteOns[2]);
    TEST_ASSERT_FALSE_MESSAGE(g_eng->isPlaying(), "idle after one cycle");
}

// ---------------------------------------------------------------------------
// Test: Direction=UpDown latch — verify the full ping-pong pattern across cycles.
// latch=true so the engine keeps looping; confirms 60,64,67,64,60 sequence.
// ---------------------------------------------------------------------------
static void test_direction_updown_latch() {
    core::ArpParams p;
    p.steps         = 3;
    p.rate          = core::ArpRate::Sixteenth;  // 125 ms
    p.gatePercent   = 80;
    p.direction     = core::ArpDirection::UpDown;
    p.velocityMode  = core::ArpVelocityMode::Fixed;
    p.fixedVelocity = 100;
    p.swingPercent  = 50;
    p.latch         = true;
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
    TEST_ASSERT_EQUAL_INT_MESSAGE(5, (int)noteOns.size(),
        "UpDown latch: 5 NoteOns over ping-pong");
    TEST_ASSERT_EQUAL_INT(60, noteOns[0]);
    TEST_ASSERT_EQUAL_INT(64, noteOns[1]);
    TEST_ASSERT_EQUAL_INT(67, noteOns[2]);
    TEST_ASSERT_EQUAL_INT(64, noteOns[3]);
    TEST_ASSERT_EQUAL_INT(60, noteOns[4]);
}

// ---------------------------------------------------------------------------
// Test: Direction=Random — no two consecutive NoteOns are the same note.
// Use latch=true to drive many steps (hold-off would stop after one cycle).
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
    p.latch         = true;  // latch: loops forever so we can observe many steps
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
// Test: steps=1 + UpDown — no crash, only note 60 emitted.
// One-shot: seqLen_==1 → one cycle == one step. Engine goes idle after 1 step.
// Use latch=true to verify looping and that every step is note 60.
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
    p.latch         = true;  // latch: loops forever so we can drive many steps
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
    p.latch         = true;  // latch: loops forever so we can drive many steps
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
// Test: Direction=DownUp one-shot (steps=3)
// seq=[60,64,67], starts at top (67), one cycle=3 steps: 67,64,60 → idle.
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
    p.latch         = false;
    g_eng->setParams(p);

    g_eng->noteOn(60, 100, 0);
    g_eng->tick(125);
    g_eng->tick(250);
    g_eng->tick(375);  // step boundary after cycle → idle
    g_eng->tick(500);  // no new events

    std::vector<uint8_t> noteOns;
    for (auto& e : g_out->events) {
        if (e.isOn) noteOns.push_back(e.note);
    }
    // One cycle = 3 steps: 67,64,60 then idle
    TEST_ASSERT_EQUAL_INT_MESSAGE(3, (int)noteOns.size(),
        "DownUp one-shot: exactly 3 NoteOns (one cycle)");
    TEST_ASSERT_EQUAL_INT(67, noteOns[0]);
    TEST_ASSERT_EQUAL_INT(64, noteOns[1]);
    TEST_ASSERT_EQUAL_INT(60, noteOns[2]);
    TEST_ASSERT_FALSE_MESSAGE(g_eng->isPlaying(), "idle after one cycle");
}

// ---------------------------------------------------------------------------
// Test: Direction=DownUp latch — verify the full DownUp ping-pong pattern.
// latch=true so the engine keeps looping; confirms 67,64,60,64,67 sequence.
// ---------------------------------------------------------------------------
static void test_direction_downup_latch() {
    core::ArpParams p;
    p.steps         = 3;
    p.rate          = core::ArpRate::Sixteenth;  // 125 ms
    p.gatePercent   = 80;
    p.direction     = core::ArpDirection::DownUp;
    p.velocityMode  = core::ArpVelocityMode::Fixed;
    p.fixedVelocity = 100;
    p.swingPercent  = 50;
    p.latch         = true;
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
    TEST_ASSERT_EQUAL_INT_MESSAGE(5, (int)noteOns.size(),
        "DownUp latch: 5 NoteOns over ping-pong");
    TEST_ASSERT_EQUAL_INT(67, noteOns[0]);
    TEST_ASSERT_EQUAL_INT(64, noteOns[1]);
    TEST_ASSERT_EQUAL_INT(60, noteOns[2]);
    TEST_ASSERT_EQUAL_INT(64, noteOns[3]);
    TEST_ASSERT_EQUAL_INT(67, noteOns[4]);
}

// ---------------------------------------------------------------------------
// Test: Swing delays odd steps.
// bpm=120, Quarter=500ms/step, swingPercent=75.
// swing offset = (75-50)*500/100 = 125 ms.
//
// Step 0 fires at t=0 (stepCount_=0, even → no swing).
// Step 1 fires at t=500 (stepCount_=1, odd → swing on step 2's boundary):
//   nextStepMs_ for step 2 = 500+500+125 = 1125.
// Step 2 fires at t=1125 (not 1000).
//
// The cycle completes at step 2 (the 3rd step, index 2) when the engine goes idle.
// We verify: at t=1000 note 67 NOT yet present; at t=1125 note 67 IS present.
// Use latch=true so the engine keeps looping and we can measure the delay.
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
    p.latch         = true;  // keep looping so we can observe swing
    g_eng->setParams(p);

    g_eng->noteOn(60, 100, 0);  // step 0 fires immediately (stepCount_=0 is even)

    g_eng->tick(500);   // step 1 (note 64) fires here; stepCount_=1 → swing sets step2 at 1125

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

// Helper: make a standard 3-step Up Quarter params struct (latch=false).
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
// TDD (Bug 2): hold OFF = one-shot per note.
//   noteOn(60) — NEVER call noteOff.  Drive ticks across several step boundaries.
//   C major triad from 60, steps=3, Up: seq=[60,64,67].
//   Expected: exactly 3 NoteOns (one cycle: 60,64,67), then idle.
//   A 4th NoteOn must NEVER appear.
// ---------------------------------------------------------------------------
static void test_holdoff_plays_one_cycle_then_idle() {
    g_eng->setParams(makeStdParams());  // latch=false

    g_eng->noteOn(60, 100, 0);      // t=0: step0=60; never call noteOff
    g_eng->tick(500);               // t=500: step1=64
    g_eng->tick(1000);              // t=1000: step2=67 → cycle complete → cyclePending_=true

    // Exactly 3 NoteOns: 60, 64, 67
    auto seq = noteOnSequence();
    TEST_ASSERT_EQUAL_INT_MESSAGE(3, (int)seq.size(),
        "hold-off: exactly one cycle (3 NoteOns)");
    TEST_ASSERT_EQUAL_INT(60, seq[0]);
    TEST_ASSERT_EQUAL_INT(64, seq[1]);
    TEST_ASSERT_EQUAL_INT(67, seq[2]);

    // Drive to the next step boundary — cyclePending_ resolves, queue empty → idle.
    // No 4th NoteOn must appear.
    g_eng->tick(1500);
    g_eng->tick(2000);
    TEST_ASSERT_EQUAL_INT_MESSAGE(3, (int)noteOnSequence().size(),
        "no 4th NoteOn after idle");

    // Engine must be idle after the boundary at t=1500 resolved the cycle.
    TEST_ASSERT_FALSE_MESSAGE(g_eng->isPlaying(),
        "hold-off: engine must be idle after the next boundary resolves the cycle");
}

// ---------------------------------------------------------------------------
// TDD (Bug 2): toggling latch ON→OFF while looping finishes current cycle then stops.
//   latch=true; noteOn(60); drive ≥2 full cycles confirming it loops.
//   Then setParams(latch=false). Continue ticking.
//   Within ONE more cycle the engine must go idle.
// ---------------------------------------------------------------------------
static void test_holdoff_after_latch_stops() {
    core::ArpParams p = makeStdParams();
    p.latch = true;
    g_eng->setParams(p);

    // Timeline (500ms/step, gatePercent=80, latch=true):
    //   t=0:    step0=60 fires; nextStepMs_=500
    //   t=500:  step1=64; nextStepMs_=1000
    //   t=1000: step2=67 → cycle1 complete → latch loops → nextStepMs_=1500
    //   t=1500: step0=60 (cycle2); nextStepMs_=2000
    //   t=2000: step1=64 (cycle2); nextStepMs_=2500
    //   t=2500: step2=67 → cycle2 complete → latch loops → nextStepMs_=3000
    //   t=3000: step0=60 (cycle3 starts) — still looping!
    g_eng->noteOn(60, 100, 0);      // t=0: step0=60 (latch: loops)
    g_eng->tick(500);               // t=500: step1=64
    g_eng->tick(1000);              // t=1000: step2=67 → cycle1 done → latch → loop
    g_eng->tick(1500);              // t=1500: step0=60 (cycle2)
    g_eng->tick(2000);              // t=2000: step1=64 (cycle2)
    g_eng->tick(2500);              // t=2500: step2=67 (cycle2) → cycle2 done → latch → loop
    g_eng->tick(3000);              // t=3000: step0=60 (cycle3 starts) — 7th NoteOn

    // Confirm it's still looping after 2+ full cycles (at least 7 NoteOns)
    {
        auto seq = noteOnSequence();
        TEST_ASSERT_TRUE_MESSAGE((int)seq.size() >= 7,
            "latch: must still be looping after 2 cycles (>=7 NoteOns)");
    }
    TEST_ASSERT_TRUE_MESSAGE(g_eng->isPlaying(),
        "latch: still playing after 2 cycles");

    // Now toggle hold off
    p.latch = false;
    g_eng->setParams(p);

    // Drive the remaining steps of cycle3 plus the cycle boundary.
    // cycle3 started at t=3000 (step0=60 already fired). Steps 1 and 2 at t=3500, t=4000.
    int nAfterToggle = (int)noteOnSequence().size();
    g_eng->tick(3500);              // step1=64 of cycle3
    g_eng->tick(4000);              // step2=67 of cycle3 → cyclePending_=true (latch=false path)

    // cyclePending_ defers the idle; engine still reports playing until the next boundary.
    // Drive to t=4500 — beginStep resolves cyclePending_, queue empty → idle (no new NoteOn).
    int nTotal = (int)noteOnSequence().size();
    g_eng->tick(4500);

    // After the next boundary the engine must be idle
    TEST_ASSERT_FALSE_MESSAGE(g_eng->isPlaying(),
        "after latch→off, engine must be idle at the boundary after the last step");

    // No new NoteOns must have appeared between t=4000 and t=4500 (boundary just resolved)
    g_eng->tick(5000);
    TEST_ASSERT_EQUAL_INT_MESSAGE(nTotal, (int)noteOnSequence().size(),
        "no new NoteOns after engine goes idle");

    // And we must have emitted some steps after the toggle (the remaining cycle)
    TEST_ASSERT_TRUE_MESSAGE(nTotal > nAfterToggle,
        "at least some steps must fire in the final cycle after toggle");
}

// ---------------------------------------------------------------------------
// Task-5 test: RENAMED — one-shot model (hold off plays ONE cycle then idles).
//   noteOn(60) — never call noteOff.  Drive across two cycles.
//   C major triad from 60 with steps=3: seq=[60,64,67] (Up).
//   Expected: exactly 3 NoteOns (one cycle), then idle.
// ---------------------------------------------------------------------------
static void test_loop_while_held() {
    // RENAMED BEHAVIOUR: "loop while held" is gone; hold-off = one-shot.
    // Kept as test_loop_while_held to preserve the RUN_TEST name for
    // continuity; the test now encodes the one-shot contract.
    g_eng->setParams(makeStdParams());

    g_eng->noteOn(60, 100, 0);      // t=0: step0=60; never release
    g_eng->tick(500);               // t=500: step1=64
    g_eng->tick(1000);              // t=1000: step2=67 → cycle complete → one-shot → idle

    auto seq = noteOnSequence();
    // One cycle: 3 NoteOns, then idle.
    TEST_ASSERT_EQUAL_INT_MESSAGE(3, (int)seq.size(),
        "hold-off one-shot: exactly 3 NoteOns (60,64,67), no loop");
    TEST_ASSERT_EQUAL_INT(60, seq[0]);
    TEST_ASSERT_EQUAL_INT(64, seq[1]);
    TEST_ASSERT_EQUAL_INT(67, seq[2]);

    // Drive past where old looping would have resumed — confirm idle
    g_eng->tick(1500);
    g_eng->tick(2000);
    g_eng->tick(2500);
    g_eng->tick(3000);
    TEST_ASSERT_EQUAL_INT_MESSAGE(3, (int)noteOnSequence().size(),
        "no additional NoteOns after cycle completes in one-shot mode");
    TEST_ASSERT_FALSE_MESSAGE(g_eng->isPlaying(),
        "engine must be idle after the one cycle");
}

// ---------------------------------------------------------------------------
// Task-5 test: one cycle then idle (previously "release finishes cycle").
//   noteOn(60) at t=0; noteOff is now a no-op, but the engine still plays
//   exactly ONE cycle (60,64,67) and then goes idle.
//   Calling noteOff mid-cycle must not change the outcome.
// ---------------------------------------------------------------------------
static void test_release_finishes_cycle_then_idle() {
    g_eng->setParams(makeStdParams());

    g_eng->noteOn(60, 100, 0);          // step0=60 at t=0
    g_eng->tick(500);                   // step1=64 at t=500
    g_eng->noteOff(60, 500);            // no-op under new model
    g_eng->tick(1000);                  // step2=67 at t=1000 → cycle complete → idle

    // 67 must have been emitted (cycle always completes)
    {
        auto seq = noteOnSequence();
        bool has67 = false;
        for (auto n : seq) if (n == 67) { has67 = true; break; }
        TEST_ASSERT_TRUE_MESSAGE(has67, "step2=67 must still play (one-shot, noteOff is no-op)");
    }

    // Tick past gate NoteOff for 67, then the next step boundary
    g_eng->tick(1400);  // gate NoteOff for 67 fires here (80%*500=400ms after t=1000 → t=1400)
    g_eng->tick(1500);  // next step boundary — must NOT produce a new NoteOn

    {
        auto seq = noteOnSequence();
        // Only 3 NoteOns: 60, 64, 67
        TEST_ASSERT_EQUAL_INT_MESSAGE(3, (int)seq.size(), "no 4th NoteOn after cycle complete");
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
//   noteOn(60) then noteOff(60) immediately (staccato).
//   noteOn(67) then noteOff(67) immediately.
//   Active is 60 → plays one full cycle (60,64,67), then dequeues.
//   67 becomes active → plays its triad.
//   C major from 67 with steps=3: 67,71,74.
//   Expected NoteOn prefix: 60,64,67,67,71,74.
//
//   Timeline (500ms/step):
//     t=0:    noteOn(60) → 60 fires; noteOn(67) queued
//     t=500:  64 fires (60's step1)
//     t=1000: 67 fires (60's step2); cycle complete → qPop 60, initSeqFromHead(67), RETURN
//             (67's step0 is NOT emitted yet — it fires at the next scheduled boundary)
//     t=1500: 67 fires (67's step0 at the next scheduled boundary)
//     t=2000: 71 fires (67's step1)
//     t=2500: 74 fires (67's step2) → cycle complete → idle
// ---------------------------------------------------------------------------
static void test_fifo_two_staccato_notes() {
    g_eng->setParams(makeStdParams());

    g_eng->noteOn(60, 100, 0);
    g_eng->noteOff(60, 0);      // staccato (no-op under new model; one-shot still works)
    g_eng->noteOn(67, 100, 0);  // appended to FIFO while 60 is active
    g_eng->noteOff(67, 0);      // staccato (no-op)

    // Drive 60's cycle: steps at t=500 and t=1000
    g_eng->tick(500);   // 60's step1=64
    g_eng->tick(1000);  // 60's step2=67; cycle complete → qPop 60, prepare 67 (no emit yet)
    g_eng->tick(1500);  // 67's step0=67 fires at the next scheduled boundary
    g_eng->tick(2000);  // 67's step1=71
    g_eng->tick(2500);  // 67's step2=74 → cycle complete → one-shot → idle

    auto seq = noteOnSequence();
    // Expect exactly 6: 60,64,67,67,71,74
    TEST_ASSERT_EQUAL_INT_MESSAGE(6, (int)seq.size(), "expected exactly 6 NoteOns");
    TEST_ASSERT_EQUAL_INT(60, seq[0]);
    TEST_ASSERT_EQUAL_INT(64, seq[1]);
    TEST_ASSERT_EQUAL_INT(67, seq[2]);
    TEST_ASSERT_EQUAL_INT(67, seq[3]);
    TEST_ASSERT_EQUAL_INT(71, seq[4]);
    TEST_ASSERT_EQUAL_INT(74, seq[5]);
}

// ---------------------------------------------------------------------------
// Task-5 test: FIFO ordering with a queued note.
//   noteOn(60) held; let it complete one cycle; noteOn(67) while 60 playing.
//   Under one-shot model: 60 plays ONE cycle (60,64,67) then dequeues.
//   67 becomes active at the NEXT step boundary after 60's cycle completes.
//   67's C major triad (steps=3, Up): 67,71,74.
//
//   Timeline (500ms/step, steps=3):
//     t=0:    noteOn(60)  → 60 fires
//     t=500:  tick        → 64 fires
//     t=500:  noteOn(67)  → queued in FIFO
//     t=1000: tick        → 67 fires (60's step2); cycle complete → qPop 60,
//                           initSeqFromHead(67), RETURN (67's step0 NOT yet emitted)
//     t=1500: tick        → 67's step0=67 fires at the next scheduled boundary
//     t=2000: tick        → 71 (67's step1)
//     t=2500: tick        → 74 (67's step2) → cycle complete → one-shot → idle
//
//   Expected seq: 60,64,67,67,71,74
// ---------------------------------------------------------------------------
static void test_held_then_queue_switch() {
    g_eng->setParams(makeStdParams());

    g_eng->noteOn(60, 100, 0);      // t=0: 60 fires, 60 is active
    g_eng->tick(500);               // t=500: 64
    g_eng->noteOn(67, 100, 500);    // queue 67 in FIFO while 60 is still in cycle1
    g_eng->tick(1000);              // t=1000: 67 (step2 of 60's cycle1 = 67)
                                    //         cycle1 complete → cyclePending_=true (deferred)
                                    //         (67's step0 fires at the next boundary)
    g_eng->tick(1500);              // t=1500: cyclePending_ resolved → promote 67;
                                    //         67's step0=67 fires
    g_eng->tick(2000);              // t=2000: 71 (67's step1)
    g_eng->tick(2500);              // t=2500: 74 (67's step2) → cyclePending_=true

    auto seq = noteOnSequence();
    // Expected: 60,64,67, 67,71,74
    TEST_ASSERT_EQUAL_INT_MESSAGE(6, (int)seq.size(), "expected exactly 6 NoteOns");
    TEST_ASSERT_EQUAL_INT(60, seq[0]);
    TEST_ASSERT_EQUAL_INT(64, seq[1]);
    TEST_ASSERT_EQUAL_INT(67, seq[2]);  // 60's cycle step2
    TEST_ASSERT_EQUAL_INT(67, seq[3]);  // 67's step0 (fires one step after 60's last note)
    TEST_ASSERT_EQUAL_INT(71, seq[4]);
    TEST_ASSERT_EQUAL_INT(74, seq[5]);

    // Drive to the next boundary (t=3000) — cyclePending_ resolves, queue empty → idle.
    // No 7th NoteOn must appear.
    g_eng->tick(3000);
    TEST_ASSERT_EQUAL_INT_MESSAGE(6, (int)noteOnSequence().size(),
        "no 7th NoteOn; engine resolves to idle at t=3000 boundary");
    TEST_ASSERT_FALSE_MESSAGE(g_eng->isPlaying(), "idle after 67's cycle completes");
}

// ---------------------------------------------------------------------------
// Task-5 test: latch replaces at cycle boundary
//   latch=true. noteOn(60); noteOff(60) → latch ignores it, 60 keeps looping.
//   noteOn(67) → queued as pending replacement.
//   At next cycle boundary after noteOn(67), 67 is installed and its step0
//   fires at the NEXT scheduled tick (not the same tick as the boundary).
//   Assert: 67's triad appears one step after the boundary, and 60 does not play after.
//
//   Timeline (500ms/step, steps=3):
//     t=0:    noteOn(60) → 60 fires (latch mode)
//     t=500:  tick       → 64
//     t=1000: tick       → 67 (60's cycle1 complete, latch → keep looping 60)
//     t=1000: noteOff(60) → latch ignores
//     t=1000: noteOn(67) → pending replacement
//     t=1500: tick       → 60 fires (cycle2, step0)
//     t=2000: tick       → 64
//     t=2500: tick       → 67 (cycle2 step2, boundary → install 67, RETURN, no emit yet)
//     t=3000: tick       → 67's step0=67 fires at the next scheduled boundary
//     t=3500: tick       → 71 (67's step1)
//     t=4000: tick       → 74 (67's step2)
// ---------------------------------------------------------------------------
static void test_latch_replaces_at_boundary() {
    core::ArpParams p = makeStdParams();
    p.latch = true;
    g_eng->setParams(p);

    g_eng->noteOn(60, 100, 0);
    g_eng->tick(500);
    g_eng->tick(1000);             // cycle1 complete
    g_eng->noteOff(60, 1000);      // latch: ignore (no-op)
    g_eng->noteOn(67, 100, 1000);  // pending replacement
    g_eng->tick(1500);             // 60 cycle2 step0
    g_eng->tick(2000);             // 60 cycle2 step1
    g_eng->tick(2500);             // 60 cycle2 step2 → boundary → install 67, RETURN (no emit yet)
    g_eng->tick(3000);             // 67's step0=67 fires at the next scheduled boundary
    g_eng->tick(3500);             // 67's step1=71
    g_eng->tick(4000);             // 67's step2=74

    auto seq = noteOnSequence();
    // 60 plays: cycle1 (60,64,67) + cycle2 (60,64,67) = 6 times
    // then 67 plays: 67,71,74  (starting one step after the boundary)
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
// Task-5 test: stop() clears queue and silences everything.
//   Queue two staccato notes (60, 67). After stop():
//     - isPlaying() == false
//     - A NoteOff is emitted for any sounding note
//     - Further ticks emit nothing new
// ---------------------------------------------------------------------------
static void test_stop_clears_queue() {
    g_eng->setParams(makeStdParams());

    g_eng->noteOn(60, 100, 0);   // active
    g_eng->noteOff(60, 0);       // no-op (one-shot: hold state irrelevant)
    g_eng->noteOn(67, 100, 0);   // queued
    g_eng->noteOff(67, 0);       // no-op

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
// Test: steps=1, three staccato notes pressed+released before any tick.
//
// With one-shot model and seqLen_==1:
//   Each cycle = 1 step → immediate dequeue after each step.
//   noteOn(60)  → step0 fires (NoteOn 60), cycle complete → dequeue 60.
//   62 is in FIFO → same-tick promotion: step0 fires (NoteOn 62), cycle complete → dequeue 62.
//   64 is in FIFO → same-tick promotion: step0 fires (NoteOn 64), cycle complete → dequeue 64.
//   Queue empty → idle.
//
// Total NoteOns = 3 (one per note, all at t=0 via same-tick promotion chain).
// NoteOn count == NoteOff count (no stuck note).
// ---------------------------------------------------------------------------
static void test_steps1_many_staccato_no_stuck_note() {
    core::ArpParams p;
    p.steps         = 1;
    p.rate          = core::ArpRate::Quarter;  // 500 ms/step at 120 BPM
    p.gatePercent   = 80;
    p.direction     = core::ArpDirection::Up;
    p.velocityMode  = core::ArpVelocityMode::Fixed;
    p.fixedVelocity = 100;
    p.swingPercent  = 50;
    p.latch         = false;
    g_eng->setParams(p);

    // With one-shot model, steps=1 (seqLen_=1), and the decide-at-start fix:
    //   noteOn(60,0) → active=false → starts fresh: fires 60, cyclePending_=true,
    //                                               active_ stays true.
    //   noteOff(60,0) → no-op.
    //   noteOn(62,0) → active=true → appended to FIFO (not a fresh start).
    //   noteOff(62,0) → no-op.
    //   noteOn(64,0) → active=true → appended to FIFO.
    //   noteOff(64,0) → no-op.
    //   tick(500):  beginStep resolves cyclePending_ → qPop(60), initSeqFromHead(62)
    //               → fires 62, cyclePending_=true.
    //   tick(1000): beginStep resolves cyclePending_ → qPop(62), initSeqFromHead(64)
    //               → fires 64, cyclePending_=true.
    //   tick(1500): beginStep resolves cyclePending_ → qPop(64), qCount_=0 → idle.
    // Total: 3 NoteOns in order 60 (t=0), 62 (t=500), 64 (t=1000).

    g_eng->noteOn (60, 100, 0);  // fires 60 at t=0; cyclePending_=true, active stays true
    g_eng->noteOff(60, 0);       // no-op
    g_eng->noteOn (62, 100, 0);  // active=true → queued in FIFO
    g_eng->noteOff(62, 0);       // no-op
    g_eng->noteOn (64, 100, 0);  // active=true → queued in FIFO
    g_eng->noteOff(64, 0);       // no-op

    // At t=0: only 60 has fired (62 and 64 are in the FIFO, not yet promoted).
    {
        std::vector<uint8_t> noteOns;
        for (auto& e : g_out->events) {
            if (e.isOn) noteOns.push_back(e.note);
        }
        TEST_ASSERT_EQUAL_INT_MESSAGE(1, (int)noteOns.size(),
            "at t=0: only note 60 has fired");
        TEST_ASSERT_EQUAL_INT(60, noteOns[0]);
    }

    // Drive to t=500: 62 fires; t=1000: 64 fires; t=1500: boundary resolves → idle.
    g_eng->tick(500);   // 62 fires
    g_eng->tick(1000);  // 64 fires
    g_eng->tick(1500);  // cyclePending_ of 64 resolves → queue empty → idle

    {
        std::vector<uint8_t> noteOns;
        for (auto& e : g_out->events) {
            if (e.isOn) noteOns.push_back(e.note);
        }
        TEST_ASSERT_EQUAL_INT_MESSAGE(3, (int)noteOns.size(),
            "expected 3 NoteOns total: 60 (t=0), 62 (t=500), 64 (t=1000)");
        TEST_ASSERT_EQUAL_INT(60, noteOns[0]);
        TEST_ASSERT_EQUAL_INT(62, noteOns[1]);
        TEST_ASSERT_EQUAL_INT(64, noteOns[2]);
    }

    // Engine must be idle after all notes complete.
    TEST_ASSERT_FALSE_MESSAGE(g_eng->isPlaying(),
        "engine must be idle after all queued notes are exhausted");

    // Drive ticks to flush any deferred gate NoteOff (gate = 80% of 500 ms = 400 ms).
    g_eng->tick(1900);
    g_eng->tick(2000);

    // NoteOn and NoteOff counts must be balanced (no stuck note).
    int ons = 0, offs = 0;
    for (auto& e : g_out->events) {
        if (e.isOn) ++ons; else ++offs;
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(ons, offs,
        "NoteOn count must equal NoteOff count (no stuck note)");
}

// ---------------------------------------------------------------------------
// Test: latch mode — when multiple noteOns arrive while active, the LAST one
// wins at the next cycle boundary (not the first one stored).
//
// We choose root notes so that 64's triad does NOT overlap with 60's or 67's
// triad.  Root 63 (Eb → quantizes to 64 in C major) would cause confusion, so
// we use 63 as the "loser" candidate (latch stores it then overwrites it) and
// 69 (A → quantizes to 69 in C major) as the "winner".
//
//   60's C major triad (steps=3, Up):  60, 64, 67
//   69's C major triad (steps=3, Up):  69, 72, 76
//
// Timeline (steps=3, Quarter=500ms/step):
//   t=0:    noteOn(60)  → active, 60 fires
//   t=200:  noteOn(63)  → latch pending = 63 (quantizes to 64, triad 64,67,71)
//   t=200:  noteOn(69)  → overwrites pending: latch pending = 69 (latest wins)
//   t=500:  step1=64 of 60's triad fires
//   t=1000: step2=67 of 60's triad fires; cycle boundary → install 69, RETURN (no emit yet)
//   t=1500: 69's step0=69 fires at the next scheduled boundary
//   t=2000: 69's step1=72
//   t=2500: 69's step2=76
//
// Assert: after the boundary 69's triad (69,72,76) plays; 64 (which would be
// the first note of 63's triad) must not appear AFTER the cycle boundary.
// (64 legally appears BEFORE the boundary as a step in 60's C major triad.)
// ---------------------------------------------------------------------------
static void test_latch_latest_wins() {
    core::ArpParams p;
    p.steps         = 3;
    p.rate          = core::ArpRate::Quarter;  // 500 ms/step at 120 BPM
    p.gatePercent   = 80;
    p.direction     = core::ArpDirection::Up;
    p.velocityMode  = core::ArpVelocityMode::Fixed;
    p.fixedVelocity = 100;
    p.swingPercent  = 50;
    p.latch         = true;
    g_eng->setParams(p);

    g_eng->noteOn(60, 100, 0);    // starts; 60 fires immediately
    g_eng->tick(500);             // step1=64 of 60's triad
    // Before cycle boundary: store two pending replacements; last one wins.
    g_eng->noteOn(63, 100, 500);  // first pending (would quantize to 64)
    g_eng->noteOn(69, 100, 500);  // overwrites pending — 69 is the winner
    g_eng->tick(1000);            // step2=67; cycle boundary → install 69, RETURN (no emit yet)
    // t=1000: boundary tick done; 69's step0 is NOT emitted yet.
    // Capture the event list size here — everything after this must be 69's sequence.
    int boundaryIdx = (int)g_out->events.size();
    g_eng->tick(1500);            // 69's step0=69 fires at the next scheduled boundary
    g_eng->tick(2000);            // 69's step1=72
    g_eng->tick(2500);            // 69's step2=76

    auto seq = noteOnSequence();

    // 69's triad (69, 72, 76) must be present overall.
    bool has69 = false, has72 = false, has76 = false;
    for (auto n : seq) {
        if (n == 69) has69 = true;
        if (n == 72) has72 = true;
        if (n == 76) has76 = true;
    }
    TEST_ASSERT_TRUE_MESSAGE(has69, "69 must appear after latch replacement");
    TEST_ASSERT_TRUE_MESSAGE(has72, "72 must appear in 69's triad");
    TEST_ASSERT_TRUE_MESSAGE(has76, "76 must appear in 69's triad");

    // After the boundary, the active arpeggio is 69's triad.
    // The first note of 63's triad (if 63 had won) would be 64; confirm 64
    // does NOT appear after the cycle boundary (it appeared before, as part of
    // 60's triad, which is correct and expected).
    for (int i = boundaryIdx; i < (int)g_out->events.size(); ++i) {
        if (g_out->events[i].isOn) {
            TEST_ASSERT_NOT_EQUAL_MESSAGE(64, g_out->events[i].note,
                "64 must not appear after cycle boundary — 69 overwrote the 63 pending slot");
        }
    }
}

// ---------------------------------------------------------------------------
// Test: push more notes than kQueueCap (16) — no crash, no stuck note.
//
// Notes beyond capacity are silently dropped by qPush.  We push 20 staccato
// notes (noteOn + immediate noteOff each), drive many ticks, and assert:
//   1. No crash.
//   2. NoteOn count == NoteOff count at the end (no stuck note).
// We do not assert which notes get dropped — that is an implementation detail.
// ---------------------------------------------------------------------------
static void test_queue_full_no_crash() {
    core::ArpParams p;
    p.steps         = 3;
    p.rate          = core::ArpRate::Quarter;  // 500 ms/step at 120 BPM
    p.gatePercent   = 80;
    p.direction     = core::ArpDirection::Up;
    p.velocityMode  = core::ArpVelocityMode::Fixed;
    p.fixedVelocity = 100;
    p.swingPercent  = 50;
    p.latch         = false;
    g_eng->setParams(p);

    // Push 20 staccato notes (kQueueCap=16; the extra 4 are silently dropped).
    // Use MIDI notes 48..67 — all diatonic or chromatically quantizable in C major.
    for (int i = 0; i < 20; ++i) {
        uint8_t n = static_cast<uint8_t>(48 + i);
        g_eng->noteOn (n, 100, 0);
        g_eng->noteOff(n, 0);
    }

    // Drive enough ticks for all 16 accepted notes to play through their 3-step cycles.
    // Worst case: 16 notes × 3 steps × 500 ms = 24 000 ms.
    for (int t = 500; t <= 25000; t += 500) {
        g_eng->tick(static_cast<uint32_t>(t));
    }

    // Engine must be idle (all cycles complete).
    TEST_ASSERT_FALSE_MESSAGE(g_eng->isPlaying(),
        "engine must be idle after all queued notes are exhausted");

    // NoteOn and NoteOff counts must be equal (no stuck note).
    int ons = 0, offs = 0;
    for (auto& e : g_out->events) {
        if (e.isOn) ++ons; else ++offs;
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(ons, offs,
        "NoteOn count must equal NoteOff count (no stuck note after overflow)");
}

// ---------------------------------------------------------------------------
// Test: setMuted — suppresses NoteOn to out, keeps NoteOff, echo still fires.
//
// Setup: steps=1, Quarter=500ms (simplest: every step is a cycle boundary).
//   Step 0 at t=0  : NoteOn(60) → out + echo fired.
//   setMuted(true)
//   Step 1 at t=500: NoteOff(60) sent to out (no stuck note),
//                    NoteOn(next) NOT sent to out,
//                    but echo IS still fired (visualisation runs).
//   setMuted(false)
//   Step 2 at t=1000: NoteOn fires to out again.
// ---------------------------------------------------------------------------

// File-static echo counter, reset in the test itself.
static int g_echoCount = 0;
static void echoCounter(void* /*user*/, bool /*isOn*/,
                         uint8_t /*ch*/, uint8_t /*note*/, uint8_t /*vel*/) {
    ++g_echoCount;
}

static void test_mute_suppresses_noteon_keeps_off_and_echo() {
    // Use a fresh engine (setUp already created g_eng but we want clean counts).
    // Reset the echo counter.
    g_echoCount = 0;
    g_eng->setEcho(&echoCounter, nullptr);

    core::ArpParams p;
    p.steps         = 1;
    p.rate          = core::ArpRate::Quarter;   // 500 ms/step at 120 BPM
    p.gatePercent   = 80;                        // gate NoteOff at 400 ms
    p.direction     = core::ArpDirection::Up;
    p.velocityMode  = core::ArpVelocityMode::Fixed;
    p.fixedVelocity = 100;
    p.swingPercent  = 50;
    p.latch         = true;                      // latch so it keeps looping
    g_eng->setParams(p);

    // t=0: step0 NoteOn(60) → out + echo
    g_eng->noteOn(60, 100, 0);
    TEST_ASSERT_EQUAL_INT(1, (int)g_out->events.size());
    TEST_ASSERT_TRUE(g_out->events[0].isOn);
    TEST_ASSERT_EQUAL_INT(1, g_echoCount);

    // Mute on
    g_eng->setMuted(true);

    // t=400: gate NoteOff for step0 fires to out (no stuck note), echo fires too
    g_eng->tick(400);
    // There must be a NoteOff for 60 in out
    {
        bool hasOff60 = false;
        for (auto& e : g_out->events) {
            if (!e.isOn && e.note == 60) { hasOff60 = true; break; }
        }
        TEST_ASSERT_TRUE_MESSAGE(hasOff60, "NoteOff for 60 must reach out even when muted");
    }
    int echoAfterGate = g_echoCount;  // echo fired for NoteOff

    // t=500: step1 fires — NoteOn must NOT be in out, but echo must fire
    int outSizeBefore = (int)g_out->events.size();
    g_eng->tick(500);

    // Count new NoteOn events added after t=500
    int newNoteOns = 0;
    for (int i = outSizeBefore; i < (int)g_out->events.size(); ++i) {
        if (g_out->events[i].isOn) ++newNoteOns;
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, newNoteOns,
        "NoteOn must NOT reach out while muted");

    // Echo must have fired for the step (NoteOn side of echo)
    TEST_ASSERT_GREATER_THAN_MESSAGE(echoAfterGate, g_echoCount,
        "echo must still fire while muted (visualisation)");

    // Unmute
    g_eng->setMuted(false);

    // t=900: gate NoteOff fires for the step that ran at t=500
    g_eng->tick(900);

    // t=1000: step2 fires — NoteOn MUST now appear in out
    int outSizeBefore2 = (int)g_out->events.size();
    g_eng->tick(1000);
    int newNoteOns2 = 0;
    for (int i = outSizeBefore2; i < (int)g_out->events.size(); ++i) {
        if (g_out->events[i].isOn) ++newNoteOns2;
    }
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, newNoteOns2,
        "NoteOn must resume in out after unmute");
}

// ---------------------------------------------------------------------------
// Test: promotion starts at the NEXT step boundary, not the same tick.
//
// bpm=120, Quarter=500ms/step, steps=3, latch=false (one-shot FIFO).
// Sequence A (root=60, C major Up): 60, 64, 67
// Sequence B (root=67, C major Up): 67, 71, 74 — queued while A is active.
//
// Timeline:
//   t=0:    noteOn(60)  → A fires step0=60
//   t=0:    noteOn(67)  → appended to FIFO (B queued)
//   t=500:  tick        → A step1=64
//   t=1000: tick        → A step2=67; cycle complete → one-shot → qPop 60,
//                          initSeqFromHead (B), RETURN — do NOT emit yet.
//           Only 3 NoteOns at this point: A's 60, 64, 67.
//   t=1500: tick fires at nextStepMs_ (set during A's step2) → B step0=67 emits.
//           Total NoteOns = 4 now.
// ---------------------------------------------------------------------------
static void test_promotion_starts_next_boundary_not_same_tick() {
    core::ArpEngine eng;
    FakeMidiOutput out;
    core::Scale sc(core::Scale::Type::Major, 0);  // C major
    core::ArpParams p;
    p.steps         = 3;
    p.rate          = core::ArpRate::Quarter;   // 500 ms/step at 120 BPM
    p.gatePercent   = 80;
    p.direction     = core::ArpDirection::Up;
    p.velocityMode  = core::ArpVelocityMode::Fixed;
    p.fixedVelocity = 100;
    p.swingPercent  = 50;
    p.latch         = false;
    eng.setOutput(&out);
    eng.setScale(&sc);
    eng.setBpm(120);
    eng.setParams(p);

    eng.noteOn(60, 100, 0);   // sequence A = 60,64,67
    eng.noteOn(67, 100, 0);   // sequence B queued (one-shot FIFO)

    eng.tick(500);   // A step1 = 64
    eng.tick(1000);  // A step2 = 67; cycle complete; B must NOT emit yet

    auto countNoteOns = [&]() {
        int n = 0;
        for (auto& e : out.events) { if (e.isOn) ++n; }
        return n;
    };

    // At t=1000: only A's three notes (60, 64, 67) have sounded — no B yet.
    TEST_ASSERT_EQUAL_INT_MESSAGE(3, countNoteOns(),
        "at t=1000 (A's cycle boundary): only A's 3 NoteOns must exist, B must not emit yet");

    eng.tick(1500);  // now B's step0 (67) plays at the next scheduled boundary

    // At t=1500: B's first note added — total = 4 NoteOns.
    TEST_ASSERT_EQUAL_INT_MESSAGE(4, countNoteOns(),
        "at t=1500: B's first note must play exactly one step after A's last note");
}

// ---------------------------------------------------------------------------
// Test: latch pending replacement starts at the NEXT step boundary.
//
// bpm=120, Quarter=500ms/step, steps=3, latch=true.
// A (root=60): loops. After cycle1 boundary (t=1000), noteOn(67) sets pending.
// Cycle2 runs (60,64,67 at t=1000,1500,2000). At t=2500 (cycle2 boundary):
//   → install 67 as new sequence, initSeqFromHead, RETURN — do NOT emit yet.
// At t=3000: 67's step0 fires.
// ---------------------------------------------------------------------------
static void test_latch_replacement_starts_next_boundary_not_same_tick() {
    core::ArpEngine eng;
    FakeMidiOutput out;
    core::Scale sc(core::Scale::Type::Major, 0);
    core::ArpParams p;
    p.steps         = 3;
    p.rate          = core::ArpRate::Quarter;
    p.gatePercent   = 80;
    p.direction     = core::ArpDirection::Up;
    p.velocityMode  = core::ArpVelocityMode::Fixed;
    p.fixedVelocity = 100;
    p.swingPercent  = 50;
    p.latch         = true;
    eng.setOutput(&out);
    eng.setScale(&sc);
    eng.setBpm(120);
    eng.setParams(p);

    eng.noteOn(60, 100, 0);   // A starts; 60 fires
    eng.tick(500);             // A step1=64
    eng.tick(1000);            // A step2=67; cycle1 boundary → latch loops

    eng.noteOn(67, 100, 1000); // pending replacement = 67

    eng.tick(1500);            // A cycle2 step0=60
    eng.tick(2000);            // A cycle2 step1=64
    eng.tick(2500);            // A cycle2 step2=67; boundary → install 67, RETURN (no emit)

    auto countNoteOns = [&]() {
        int n = 0;
        for (auto& e : out.events) { if (e.isOn) ++n; }
        return n;
    };

    // At t=2500: A has played 6 notes (cycle1 + cycle2), 67 not yet emitted for B.
    int nAt2500 = countNoteOns();
    TEST_ASSERT_EQUAL_INT_MESSAGE(6, nAt2500,
        "at t=2500 (latch replace boundary): only A's 6 NoteOns, B must not emit yet");

    eng.tick(3000);  // B step0=67 fires at next boundary

    int nAt3000 = countNoteOns();
    TEST_ASSERT_EQUAL_INT_MESSAGE(7, nAt3000,
        "at t=3000: B's first note (67) fires exactly one step after A's last note");

    // Collect the note sequence
    std::vector<uint8_t> noteOns;
    for (auto& e : out.events) { if (e.isOn) noteOns.push_back(e.note); }

    // The 7th NoteOn (index 6) must be 67 (B's root)
    TEST_ASSERT_EQUAL_INT_MESSAGE(67, noteOns[6],
        "B's first note must be 67 (root of C major from 67)");
}

// ---------------------------------------------------------------------------
// Test: keyboard note arriving during the last step must not cut that note.
//
// bpm=120, Quarter=500ms/step, gatePercent=80, steps=3, latch=false.
// Sequence for root=60 (C major Up): 60, 64, 67.
// Gate = 80% × 500 = 400 ms.
//
// Timeline:
//   t=0:    noteOn(60) → step0=60 fires (NoteOn), gate off at t=400
//   t=500:  tick       → step1=64 fires
//   t=1000: tick       → step2=67 fires (LAST note, gate off at t=1400)
//   t=1100: noteOn(72) during the last step's gate window (67 still sounding)
//
// BUG (current code): active_ is set to false immediately when 67 is emitted
//   because the cycle is complete; noteOn(72) sees active_=false, starts fresh,
//   and kills 67 early (emits NoteOff(67) at t=1100).
//
// FIX: active_ stays true until the next step boundary; noteOn(72) simply
//   appends to the FIFO. At t=1500 (next boundary) beginStep resolves the
//   boundary, 67's gate fires normally at t=1400, and 72's step0 fires at t=1500.
//
// Assertions:
//   At t=1100: exactly 3 NoteOns emitted so far (60,64,67 — no 4th yet).
//              No NoteOff for 67 yet (gate ends at 1400, not 1100).
//   At t=1500: 4th NoteOn emitted (72's step0).
//              The 4th NoteOn must be for note 72.
// ---------------------------------------------------------------------------
static void test_keyboard_note_during_last_step_not_cut() {
    core::ArpEngine eng;
    FakeMidiOutput out;
    core::Scale sc(core::Scale::Type::Major, 0);  // C major
    core::ArpParams p;
    p.steps         = 3;
    p.rate          = core::ArpRate::Quarter;   // 500 ms/step at 120 BPM
    p.gatePercent   = 80;                        // gate NoteOff = 400 ms into the step
    p.direction     = core::ArpDirection::Up;
    p.velocityMode  = core::ArpVelocityMode::Fixed;
    p.fixedVelocity = 100;
    p.swingPercent  = 50;
    p.latch         = false;
    eng.setOutput(&out);
    eng.setScale(&sc);
    eng.setBpm(120);
    eng.setParams(p);

    eng.noteOn(60, 100, 0);    // sequence: 60, 64, 67
    eng.tick(500);              // step1 = 64
    eng.tick(1000);             // step2 = 67 (LAST note of cycle); gate off at t=1400

    // New keyboard note arrives while 67 is still sounding (t=1100 < t=1400).
    eng.noteOn(72, 100, 1100);

    // Helper lambdas to inspect events.
    auto countNoteOns = [&]() {
        int n = 0;
        for (auto& e : out.events) { if (e.isOn) ++n; }
        return n;
    };
    auto hasNoteOff = [&](uint8_t note) {
        for (auto& e : out.events) { if (!e.isOn && e.note == note) return true; }
        return false;
    };
    auto lastNoteOnNote = [&]() -> uint8_t {
        uint8_t last = 0;
        for (auto& e : out.events) { if (e.isOn) last = e.note; }
        return last;
    };

    // At t=1100: exactly 3 NoteOns (60, 64, 67); 72 must not have started yet.
    TEST_ASSERT_EQUAL_INT_MESSAGE(3, countNoteOns(),
        "at t=1100: only 3 NoteOns (60,64,67); 72 must not start mid-step");

    // 67 must NOT have been cut: no NoteOff for 67 yet (gate ends at t=1400).
    TEST_ASSERT_FALSE_MESSAGE(hasNoteOff(67),
        "at t=1100: NoteOff for 67 must not be emitted yet (gate ends at t=1400)");

    // Drive to t=1500 (next step boundary); 67's gate fires at t=1400, then
    // the boundary at t=1500 resolves and 72's step0 fires.
    eng.tick(1500);

    // Now there must be 4 NoteOns total.
    TEST_ASSERT_EQUAL_INT_MESSAGE(4, countNoteOns(),
        "at t=1500: 4th NoteOn (72's step0) must have fired at the next boundary");

    // The 4th NoteOn must be note 72.
    TEST_ASSERT_EQUAL_INT_MESSAGE(72, lastNoteOnNote(),
        "the 4th NoteOn must be note 72 (start of 72's arpeggio)");
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
    RUN_TEST(test_direction_updown_latch);
    RUN_TEST(test_direction_random_no_immediate_repeat);
    RUN_TEST(test_velocity_fixed);
    RUN_TEST(test_velocity_follow_input);
    RUN_TEST(test_velocity_accent);
    RUN_TEST(test_stop_kills_active_note);
    RUN_TEST(test_updown_single_step_no_crash);
    RUN_TEST(test_downup_single_step_no_crash);
    RUN_TEST(test_direction_downup);
    RUN_TEST(test_direction_downup_latch);
    RUN_TEST(test_swing_delays_odd_steps);
    // Bug 2: TDD — new one-shot model tests (must FAIL before implementation)
    RUN_TEST(test_holdoff_plays_one_cycle_then_idle);
    RUN_TEST(test_holdoff_after_latch_stops);
    // Task-5: FIFO queue + one-shot + latch
    RUN_TEST(test_loop_while_held);
    RUN_TEST(test_release_finishes_cycle_then_idle);
    RUN_TEST(test_fifo_two_staccato_notes);
    RUN_TEST(test_held_then_queue_switch);
    RUN_TEST(test_latch_replaces_at_boundary);
    RUN_TEST(test_stop_clears_queue);
    // Queue edge cases + recursion-depth coverage
    RUN_TEST(test_steps1_many_staccato_no_stuck_note);
    RUN_TEST(test_latch_latest_wins);
    RUN_TEST(test_queue_full_no_crash);
    RUN_TEST(test_mute_suppresses_noteon_keeps_off_and_echo);
    // Timing bug fix: promotion/replacement must start at the next step boundary
    RUN_TEST(test_promotion_starts_next_boundary_not_same_tick);
    RUN_TEST(test_latch_replacement_starts_next_boundary_not_same_tick);
    // Cycle-boundary fix: late noteOn must not cut the last sounding note
    RUN_TEST(test_keyboard_note_during_last_step_not_cut);
    return UNITY_END();
}
