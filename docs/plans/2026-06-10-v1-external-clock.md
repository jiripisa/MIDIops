# MIDIops v1 — Plan 5b: External clock + tick-driven engines

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the Arp (and future generators) advance on a unified 24-PPQN clock-tick stream — sourced from the internal clock master OR from incoming MIDI clock — and follow/forward an external clock when `clockSource == External`.

**Architecture:** A single tick stream drives the engines via a new `Mode::onClockTick()` hook. `AppShell` routes ticks: **Internal** — drain the clock master's pulse counter (`MidiOutput::consumeClockTicks()`) each `tick()`; **External** — each incoming MIDI `Clock` (0xF8) is a tick, BPM is derived from pulse timing, and the pulse is forwarded downstream (internal generation off). `ArpEngine` is reworked from millisecond scheduling to tick counting (rate/gate/swing in ticks); its step/queue/hold logic is unchanged.

**Tech Stack:** C++17, PlatformIO (envs `teensy41`, `native`, `test`), Unity, SDL2/RtMidi (sim), ILI9341_t3n + usbMIDI (firmware).

Spec: `docs/specs/2026-06-09-settings-and-external-clock-design.md` (§5–6, Plan 5b portion). Builds on Plan 5a (`ClockSource` setting, channel filter). `ChordEngine` (legacy) is not touched.

---

## File structure (Plan 5b)

- Modify `core/MidiMessage.h` — add realtime `MidiType`s (Clock/Start/Continue/Stop).
- Modify `platform/teensy/TeensyMidiInput.cpp`, `platform/host/RtMidiInput.cpp` — deliver realtime messages.
- Modify `core/MidiOutput.h` — add `consumeClockTicks()` + `forwardClock()`.
- Modify `platform/teensy/TeensyMidiOutput.{h,cpp}`, `platform/host/RtMidiOutput.{h,cpp}` — implement the two.
- Modify `test/support/FakeMidiOutput.h` — implement the two (counter + forwarded-pulse count).
- Modify `core/ArpEngine.{h,cpp}` — rework from ms to tick counting; add `onClockTick()`.
- Modify `core/app/Mode.h` — add `virtual void onClockTick() {}`.
- Modify `core/modes/ArpMode.{h,cpp}` — `onClockTick()` forwards to the engine; stop ms-ticking the engine.
- Create `core/ClockFollower.h` / `.cpp` — derive BPM from incoming clock pulses.
- Modify `core/app/AppShell.{h,cpp}` — tick routing (internal drain / external follow + forward), clock-source switching.
- Test: `test/test_arp_engine/test_arp_engine.cpp` (convert to ticks), `test/test_clock/test_clock.cpp` (new: ClockFollower + shell routing).

Conventions: 2-space indent, `core::` namespace, no platform headers in `core/`, no exceptions in `core/`, English identifiers/comments.

---

### Task 1: Realtime MIDI types

**Files:** Modify `core/MidiMessage.h`; Test `test/test_clock/test_clock.cpp` (new).

- [ ] **Step 1: Add realtime values to `MidiType`** in `core/MidiMessage.h` (keep existing channel-voice values):
```cpp
    Clock              = 0xF8,
    Start              = 0xFA,
    Continue           = 0xFB,
    Stop               = 0xFC,
```
(`isChannelVoice()` already returns false for these — they are > 0xE0 — so the Plan 5a channel filter ignores them. Confirm.)

- [ ] **Step 2: Write `test/test_clock/test_clock.cpp`** (own `setUp/tearDown/main`):
```cpp
#include <unity.h>
#include "core/MidiMessage.h"

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

int main() { UNITY_BEGIN(); RUN_TEST(test_realtime_types_are_not_channel_voice); return UNITY_END(); }
```

- [ ] **Step 3: Run** `pio test -e test -f test_clock` → PASS.
- [ ] **Step 4: Commit** `git add core/MidiMessage.h test/test_clock/test_clock.cpp && git commit -m "feat(midi): add realtime MidiType values (Clock/Start/Continue/Stop)"`

---

### Task 2: MIDI input delivers realtime

**Files:** Modify `platform/teensy/TeensyMidiInput.cpp`, `platform/host/RtMidiInput.cpp`. (Platform I/O — verified by build; the host path is exercised live in the sim.)

- [ ] **Step 1: Teensy** — read `TeensyMidiInput.cpp`. In the `switch (rawType)` that maps `usbMIDI.*` to `core::MidiType`, add cases:
```cpp
        case usbMIDI.Clock:    type = core::MidiType::Clock;    break;
        case usbMIDI.Start:    type = core::MidiType::Start;    break;
        case usbMIDI.Continue: type = core::MidiType::Continue; break;
        case usbMIDI.Stop:     type = core::MidiType::Stop;     break;
```
Realtime messages have no channel/data — leave `channel/data1/data2` as-is (0). Make sure the function still returns `true` with the populated `type` (don't fall into a "default → ignore" that returns false for these).

- [ ] **Step 2: Host** — read `RtMidiInput.cpp`. The current guard `if (status < 0x80 || status >= 0xF0) return;` drops all realtime. Replace it so the four clock/transport bytes are delivered:
```cpp
    if (status == 0xF8) { msg.type = core::MidiType::Clock;    push(msg); return; }
    if (status == 0xFA) { msg.type = core::MidiType::Start;    push(msg); return; }
    if (status == 0xFB) { msg.type = core::MidiType::Continue; push(msg); return; }
    if (status == 0xFC) { msg.type = core::MidiType::Stop;     push(msg); return; }
    if (status < 0x80 || status >= 0xF0) return;   // ignore other system/sysex
```
(Adapt `push(msg)`/the enqueue call to however this file delivers a parsed `MidiMessage` — match the existing channel-voice path's enqueue mechanism. Realtime have no channel/data.)

- [ ] **Step 2b: Build both** — `pio run -e teensy41` SUCCESS; `pio run -e native` SUCCESS.
- [ ] **Step 3: Commit** `git add platform/teensy/TeensyMidiInput.cpp platform/host/RtMidiInput.cpp && git commit -m "feat(midi): deliver realtime clock/transport from both MIDI inputs"`

---

### Task 3: MidiOutput tick counter + clock forwarding

**Files:** Modify `core/MidiOutput.h`, `platform/teensy/TeensyMidiOutput.{h,cpp}`, `platform/host/RtMidiOutput.{h,cpp}`, `test/support/FakeMidiOutput.h`.

- [ ] **Step 1: Extend `core/MidiOutput.h`** — add two pure virtuals:
```cpp
    // Number of internal clock pulses generated since the last call, then
    // reset to 0. Lets the main loop drive tick-based engines from the
    // autonomous clock master without losing pulses. Returns 0 when the
    // internal clock is stopped (e.g. following an external clock).
    virtual uint32_t consumeClockTicks() = 0;

    // Send a single MIDI Clock (0xF8) downstream — used to forward an
    // external clock pulse when following an external source.
    virtual void forwardClock() = 0;
```

- [ ] **Step 2: TeensyMidiOutput** — in `TeensyMidiOutput.cpp`:
  - Add a file-scope `volatile uint32_t g_clockTicks = 0;`. In `clockIsr()`, after sending, `++g_clockTicks;`.
  - Implement:
```cpp
uint32_t TeensyMidiOutput::consumeClockTicks() {
    noInterrupts();
    uint32_t n = g_clockTicks;
    g_clockTicks = 0;
    interrupts();
    return n;
}
void TeensyMidiOutput::forwardClock() {
    usbMIDI.sendRealTime(usbMIDI.Clock);
    usbMIDI.send_now();
}
```
  - Declare both in `TeensyMidiOutput.h` (override).

- [ ] **Step 3: RtMidiOutput** — in `RtMidiOutput.{h,cpp}`:
  - Add `std::atomic<uint32_t> clockTicks_{0};` member. In `clockThreadFunc`, after `sendByte(0xF8);`, `clockTicks_.fetch_add(1, std::memory_order_relaxed);`.
  - Implement:
```cpp
uint32_t RtMidiOutput::consumeClockTicks() { return clockTicks_.exchange(0, std::memory_order_relaxed); }
void RtMidiOutput::forwardClock() { sendByte(0xF8); }
```
  - Declare both in the header (override).

- [ ] **Step 4: FakeMidiOutput** (`test/support/FakeMidiOutput.h`) — add for tests:
```cpp
    uint32_t pendingTicks = 0;   // tests set this; consumeClockTicks() drains it
    int      forwarded = 0;      // count of forwardClock() calls
    uint32_t consumeClockTicks() override { uint32_t n = pendingTicks; pendingTicks = 0; return n; }
    void     forwardClock() override { ++forwarded; }
```

- [ ] **Step 5: Build + test** — `pio run -e teensy41` SUCCESS; `pio run -e native` SUCCESS; `pio test -e test` PASS (FakeMidiOutput now implements the full interface).
- [ ] **Step 6: Commit** `git add core/MidiOutput.h platform/teensy/TeensyMidiOutput.h platform/teensy/TeensyMidiOutput.cpp platform/host/RtMidiOutput.h platform/host/RtMidiOutput.cpp test/support/FakeMidiOutput.h && git commit -m "feat(midi): MidiOutput consumeClockTicks() + forwardClock()"`

---

### Task 4: ArpEngine — tick-driven rework

**Files:** Modify `core/ArpEngine.{h,cpp}`, `core/app/Mode.h`; Test `test/test_arp_engine/test_arp_engine.cpp`.

This converts the engine from millisecond scheduling to **clock-tick counting**. The step/queue/boundary/hold/one-shot logic in `beginStep` (decide-at-start, FIFO, latch) is UNCHANGED — only the trigger that calls `beginStep` and the gate/swing timing change from ms to ticks.

- [ ] **Step 1: Add the hook to `core/app/Mode.h`** — `virtual void onClockTick() {}` (default no-op), near the other Mode virtuals.

- [ ] **Step 2: Rework `core/ArpEngine.h`** — replace the ms timing API/fields with tick-based ones:
  - Public: remove `void tick(uint32_t nowMs)`; add `void onClockTick();`. Keep `noteOn(note,vel)`, `noteOff(note)`, `stop()`, `reset()`, `setOutput/setEcho/setBpm/setScale/setParams/setOutChannel`, `isPlaying()`. (`noteOn`/`noteOff` no longer need a `nowMs` arg — drop it; they act immediately / append to the queue as today.)
  - Remove ms fields `nextStepMs_`, `noteOffMs_`, and `msPerStep()`. Add tick counters:
```cpp
    int  stepTicks_ = 0;     // ticks elapsed in the current step
    int  gateTicks_ = 0;     // ticks the current note stays on (computed at step start)
    int  noteAge_   = 0;     // ticks since the current note's NoteOn
```
  (Keep `bpm_`/`setBpm` — used only for display/derived elsewhere; the engine no longer needs ms.)

- [ ] **Step 3: Convert the failing tests** in `test/test_arp_engine/test_arp_engine.cpp` from `tick(ms)` to `onClockTick()`. Mapping: with `Rate=Quarter` a step is `arpRateTicks(Quarter)=24` ticks; `Sixteenth`=6 ticks. So "advance one step" = call `onClockTick()` that many times. A helper:
```cpp
static void clocks(core::ArpEngine& e, int n) { for (int i = 0; i < n; ++i) e.onClockTick(); }
```
Rewrite each test to drive `clocks(eng, arpRateTicks(rate))` per step instead of `tick(ms)`, and drop the `nowMs` args from `noteOn`/`noteOff`. The behavioural assertions (NoteOn order, hold one-shot, FIFO, decide-at-start boundary, latch, stop/reset, the keyboard-note-during-last-step case) all carry over unchanged — only the driving changes. For the gate test: `Gate=50%` at Quarter (24 ticks) → note off after 12 ticks (`24*50/100`); assert the NoteOff fires after `clocks(eng, 12)`.

- [ ] **Step 4: Implement `core/ArpEngine.cpp`** `onClockTick()` and the tick scheduler:
  - `onClockTick()`:
```cpp
void ArpEngine::onClockTick() {
    if (noteSounding_) {
        ++noteAge_;
        if (noteAge_ >= gateTicks_) { emit(false, soundingNote_, 0); noteSounding_ = false; }
    }
    if (!active_) return;
    ++stepTicks_;
    if (stepTicks_ >= stepLenTicks()) beginStep();   // boundary
}
```
  - `stepLenTicks()` = `arpRateTicks(params_.rate)`, plus swing on odd `stepCount_`: `+ (swingPercent-50) * arpRateTicks / 100` (integer, ≥0). (Mirror the old swing intent in ticks.)
  - `beginStep()` (no `nowMs` arg now): same structure as today (kill prev sounding, resolve `cyclePending_`, guard, emit step, advance `seqPos_`, set `cyclePending_` on cycle complete), but timing becomes:
```cpp
    // on emit:
    int stepLen = arpRateTicks(params_.rate);
    gateTicks_  = stepLen * params_.gatePercent / 100; if (gateTicks_ < 1) gateTicks_ = 1;
    noteAge_    = 0;
    stepTicks_  = 0;          // reset for the new step
```
  (The note-off is handled by `onClockTick` via `noteAge_ >= gateTicks_`, not a separate scheduled time.)
  - `noteOn` fresh-start path calls `beginStep()` directly (immediate first step) and resets `stepTicks_=0`. `stop()`/`reset()` reset `stepTicks_`/`noteAge_`/`cyclePending_` as before.
  - The `emit`, `nextSeqIndex`, `velocityForStep`, queue helpers, `initSeqFromHead` are unchanged except `initSeqFromHead`/`noteOn` drop any ms references.

- [ ] **Step 5: Run** `pio test -e test -f test_arp_engine` → all PASS (converted suite). Then `pio test -e test` (whole suite — `ArpMode`/others may reference the old `tick`; fix in Task 5).
- [ ] **Step 6: Commit** `git add core/ArpEngine.h core/ArpEngine.cpp core/app/Mode.h test/test_arp_engine/test_arp_engine.cpp && git commit -m "refactor(arp): ArpEngine is tick-driven (onClockTick); rate/gate/swing in ticks"`

---

### Task 5: ArpMode + AppShell internal tick routing

**Files:** Modify `core/modes/ArpMode.{h,cpp}`, `core/app/AppShell.{h,cpp}`; Test `test/test_arp_mode/test_arp_mode.cpp`, `test/test_clock/test_clock.cpp`.

- [ ] **Step 1: `ArpMode`** — add `void onClockTick() override { engine_.onClockTick(); }`. In `ArpMode::update(nowMs)`, REMOVE the `engine_.tick(nowMs)` call (the engine is now ticked via `onClockTick`); KEEP `engine_.setBpm/setScale/setParams/setOutChannel` and `model_.tick(nowMs)` (the worm model stays ms-based for visual scrolling). `onMidiIn` calls `engine_.noteOn(note,vel)` / `engine_.noteOff(note)` (no nowMs arg now).

- [ ] **Step 2: `AppShell` internal tick drain** — in `AppShell::tick(uint32_t nowMs)`, after updating the active mode/screen, when `clockSource_ == ClockSource::Internal` and `out_`, drain and route ticks:
```cpp
    if (clockSource_ == ClockSource::Internal && out_ && modeCount_ > 0) {
        uint32_t n = out_->consumeClockTicks();
        for (uint32_t i = 0; i < n; ++i) modes_[activeMode_]->onClockTick();
    }
```

- [ ] **Step 3: tests**
  - `test/test_arp_mode/test_arp_mode.cpp`: `test_arp_ticks_from_internal_clock` — `AppShell` + `ArpMode` + a `FakeMidiOutput out`; `arp.setMidiOutput(&out); shell.setMidiOutput(&out); shell.addMode(&arp); shell.begin();` set a note via `onMidiIn`; set `out.pendingTicks = arpRateTicks(default rate);` then `shell.tick(0)` → the engine advanced one step (assert a second NoteOn appeared in `out.events`). Adjust to the default Rate.
  - Update any existing `test_arp_mode` test that drove `shell.tick(ms)` expecting step advances — those now need `out.pendingTicks` set to advance the engine (the worm-visual tests that only check `rects>0` still work since a note still spawns a worm via the echo on `noteOn`).

- [ ] **Step 4: Run** `pio test -e test` → all PASS.
- [ ] **Step 5: Commit** `git add core/modes/ArpMode.h core/modes/ArpMode.cpp core/app/AppShell.h core/app/AppShell.cpp test/test_arp_mode/test_arp_mode.cpp && git commit -m "feat(app): route internal clock ticks to the active mode (Arp tick-driven)"`

---

### Task 6: External clock follow + forward + switching

**Files:** Create `core/ClockFollower.{h,cpp}`; Modify `core/app/AppShell.{h,cpp}`; Test `test/test_clock/test_clock.cpp`.

- [ ] **Step 1: `core/ClockFollower.h`** — derives BPM from clock-pulse timestamps:
```cpp
#pragma once
#include <cstdint>
namespace core {
// Averages the interval over a one-beat (24-pulse) window to derive BPM
// from an incoming 24-PPQN clock. Clamped to [30..300]. Returns 0 until
// enough pulses have been seen.
class ClockFollower {
public:
    void reset() { count_ = 0; haveFirst_ = false; bpm_ = 0; }
    // Call on each incoming Clock pulse with a monotonic ms timestamp.
    void onPulse(uint32_t nowMs);
    uint16_t bpm() const { return bpm_; }   // 0 until known
private:
    static constexpr int kWindow = 24;      // one beat
    bool     haveFirst_ = false;
    uint32_t firstMs_ = 0;
    int      count_ = 0;
    uint16_t bpm_ = 0;
};
}
```
`ClockFollower.cpp`: on each pulse, if `!haveFirst_` set `firstMs_=nowMs, haveFirst_=true, count_=0`. `++count_`. When `count_ == kWindow`: `uint32_t dt = nowMs - firstMs_;` if `dt>0` `uint32_t b = 60000u * 1 / ... `→ compute `bpm = 60000 / dt` (24 pulses = 1 beat → beat duration = dt ms → bpm = 60000/dt); clamp 30..300; store; then restart the window (`firstMs_=nowMs, count_=0`). TDD with the test below.

- [ ] **Step 2: failing test** in `test/test_clock/test_clock.cpp`:
```cpp
#include "core/ClockFollower.h"
static void test_follower_derives_120bpm() {
    core::ClockFollower f;
    // 120 BPM → beat = 500 ms → 24 pulses over 500 ms ≈ 20.83 ms apart.
    for (int i = 0; i <= 24; ++i) f.onPulse(static_cast<uint32_t>(i * 500 / 24));
    TEST_ASSERT_INT_WITHIN(2, 120, f.bpm());
}
static void test_follower_clamps() {
    core::ClockFollower f;
    for (int i = 0; i <= 24; ++i) f.onPulse(i * 5);   // very fast → clamp 300
    TEST_ASSERT_EQUAL_INT(300, f.bpm());
}
```
Register them; implement `ClockFollower.cpp` to pass.

- [ ] **Step 3: AppShell external path** — add a `ClockFollower clockFollower_;` member. In `onMidiIn`, BEFORE the channel filter, handle realtime when external:
```cpp
    if (!msg.isChannelVoice()) {
        if (clockSource_ == ClockSource::External) {
            if (msg.type == MidiType::Clock) {
                clockFollower_.onPulse(nowMs_);
                uint16_t b = clockFollower_.bpm();
                if (b) { bpm_ = b; }
                if (out_) out_->forwardClock();
                if (modeCount_ > 0) modes_[activeMode_]->onClockTick();
            } else if (out_ && (msg.type == MidiType::Start || msg.type == MidiType::Continue || msg.type == MidiType::Stop)) {
                // pass transport through
                if (msg.type == MidiType::Start)    out_->sendStart();
                if (msg.type == MidiType::Continue) out_->sendContinue();
                if (msg.type == MidiType::Stop)     out_->sendStop();
            }
        }
        return;   // realtime never goes to a mode's note handler
    }
    // ... existing channel filter + forward to mode ...
```
- [ ] **Step 4: AppShell clock-source switching** — make `setClockSource` stop/restart the internal generator:
```cpp
    void setClockSource(ClockSource s) override {
        clockSource_ = s;
        if (out_) {
            if (s == ClockSource::External) { out_->setClockBpm(0); clockFollower_.reset(); }
            else                            { out_->setClockBpm(bpm_); }
        }
    }
```
(This is no longer a one-line inline — move it to `AppShell.cpp` if cleaner. Keep `setBpm` updating the internal clock only when Internal: in `setBpm`, `if (out_ && clockSource_ == ClockSource::Internal) out_->setClockBpm(bpm_);`.)

- [ ] **Step 5: shell routing test** in `test/test_clock/test_clock.cpp`:
```cpp
static void test_external_clock_routes_and_forwards() {
    core::AppShell shell; FakeMode a("a", 1); FakeMidiOutput out;
    shell.setMidiOutput(&out); shell.addMode(&a); shell.begin();
    shell.setClockSource(core::ClockSource::External);
    TEST_ASSERT_EQUAL_INT(0, out.lastBpm);            // internal stopped (setClockBpm(0))
    core::MidiMessage clk{}; clk.type = core::MidiType::Clock;
    shell.onMidiIn(clk);
    TEST_ASSERT_EQUAL_INT(1, out.forwarded);          // pulse forwarded
    // (a.onClockTick fired — FakeMode could count it; add a counter if FakeMode lacks one)
}
```
(If `FakeMode` has no clock-tick counter, add `int clockTicks_ = 0; void onClockTick() override { ++clockTicks_; }` to `test/support/Fakes.h` and assert it.)

- [ ] **Step 6: Run** `pio test -e test` → all PASS. `pio run -e native`/`-e teensy41` SUCCESS.
- [ ] **Step 7: Commit** `git add core/ClockFollower.h core/ClockFollower.cpp core/app/AppShell.h core/app/AppShell.cpp test/test_clock/test_clock.cpp test/support/Fakes.h && git commit -m "feat(app): external clock follow (derive BPM) + forward + source switching"`

---

### Task 7: Verify on both platforms

**Files:** none expected (the main loops already call `app.tick`; `onMidiIn` already routes via the shell). Confirm no main change is needed; if the host loop needs the realtime input drained the same as note input, it already is (single `midiIn.poll` loop).

- [ ] **Step 1: Build** `pio run -e teensy41` SUCCESS; `pio run -e native` SUCCESS; `pio test -e test` PASS.
- [ ] **Step 2 (controller/human): verify in the sim/hardware.**
  - Internal (default): Arp timing is now phase-locked to the clock master (tight on both platforms).
  - External: in Settings set Clock = Ext. Feed MIDI clock from a DAW (Ableton → IAC → MIDIops) — the Arp follows the external tempo, its steps lock to the incoming pulses, and the incoming clock is forwarded out (downstream gear stays synced). Switch back to Int → the internal master resumes.

---

## Self-review notes

- **Spec coverage (5b):** realtime MIDI types §5 → Task 1; both inputs deliver §5 → Task 2; `consumeClockTicks`/`forwardClock` §6 → Task 3; tick-driven engines §6 → Tasks 4–5; external follow + BPM derivation + forwarding + switching §6 → Task 6.
- **Supersedes:** the simulator's ms-tick fix from the Arp plan (the engine is now phase-locked on both platforms via the clock-tick stream).
- **Unchanged engine logic:** the `beginStep` decide-at-start / FIFO / hold / one-shot state machine is preserved; only the timing trigger (ms→ticks) changes. The full ArpEngine test set is converted, not rewritten.
- **Deferred:** external transport driving a global play/run state (Start/Stop only forwarded, not interpreted); flash persistence.
- **Type consistency:** `MidiType::Clock/Start/Continue/Stop` (Task 1) used in Tasks 2/6. `consumeClockTicks()`/`forwardClock()` (Task 3) used by AppShell (Tasks 5/6) + FakeMidiOutput. `Mode::onClockTick()` (Task 4) implemented by ArpMode (Task 5) + FakeMode (Task 6) and called by AppShell (Tasks 5/6). `ArpEngine::onClockTick()`/dropped-`tick` (Task 4) used by ArpMode (Task 5). `ClockFollower` (Task 6) used by AppShell.
- **Risk:** Task 4 (engine rework) is the largest; its tests are a mechanical ms→tick conversion of an already-passing, well-reviewed suite, preserving every behavioural assertion.
