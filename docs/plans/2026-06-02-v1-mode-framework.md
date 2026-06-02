# MIDIops v1 — Plan 1: mode framework foundation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stand up the `AppShell` + `Mode` + `Screen` framework (with the Enc5 screen-switch / mode-change overlay and Latch1–3 transport), a native unit-test harness, and the first two ported modes (Debug, BPM), with both builds green.

**Architecture:** Portable `core/app/` holds the shell + abstractions; modes live in `core/modes/`. The shell owns global services (BPM/clock, transport) behind an `AppServices` interface and routes hardware events to the active mode/screen. The legacy `MidiMonitorApp` stays in the tree (unused) as a source for later renderer extraction. Logic is unit-tested on the `native` platform via PlatformIO + Unity with stub `Display`/`MidiOutput`; rendering is verified visually in the simulator.

**Tech Stack:** C++17, PlatformIO (envs: `teensy41`, `native`, new `test`), Unity test framework, SDL2/RtMidi (sim), ILI9341_t3n + usbMIDI (firmware).

This is **Plan 1 of a series** following the spec's strangler migration
(`docs/specs/2026-06-02-v1-mode-architecture-design.md`). Later plans:
Plan 2 Monitoring (extract WormsRenderer + NotationRenderer), Plan 3 Arp
(+ Scale + 8 params), Plan 4 Settings, Plan 5 Berlin.

---

## File structure (Plan 1)

- Create `core/app/Mode.h` — `Transport` enum, `RawInput` struct, `Screen` + `Mode` abstract interfaces.
- Create `core/app/AppServices.h` — services interface the shell exposes to modes (BPM get/set, transport state).
- Create `core/app/AppShell.h` / `core/app/AppShell.cpp` — the runtime: mode registry, active mode/screen, Enc routing, mode-change overlay, transport, top bar + render.
- Create `core/modes/DebugMode.h` / `.cpp` — hardware telemetry mode (ported from `MidiMonitorApp::drawDebug`), one screen.
- Create `core/modes/BpmMode.h` / `.cpp` — big-BPM readout (ported from `MidiMonitorApp::drawBigBpm`), one screen.
- Create `test/support/StubDisplay.h` — recording `core::Display` stub.
- Create `test/support/FakeMidiOutput.h` — counting `core::MidiOutput` stub.
- Create `test/support/Fakes.h` — `FakeMode`/`FakeScreen`/`FakeServices` for shell tests.
- Create `test/test_shell/test_shell.cpp` — Unity tests for routing, overlay, transport.
- Create `test/test_smoke/test_smoke.cpp` — harness smoke test.
- Modify `platformio.ini` — add `[env:test]`.
- Modify `platform/teensy/main.cpp` — drive `AppShell` instead of `MidiMonitorApp`.
- Modify `platform/host/main.cpp` — drive `AppShell` instead of `MidiMonitorApp`.

Conventions: 2-space indent, `core::` namespace, RGB565 colors, identifiers/comments in English.

---

### Task 0: Native unit-test harness

**Files:**
- Modify: `platformio.ini`
- Create: `test/test_smoke/test_smoke.cpp`

- [ ] **Step 1: Add a `test` environment to `platformio.ini`**

Append after the `[env:native]` block:

```ini
; ============================================================
;  Native unit tests (core/ only, Unity)
; ============================================================
[env:test]
platform        = native
build_src_filter = -<*> +<core/>
build_flags     =
    -std=c++17
    -Wall
    -Wextra
    -I${PROJECT_DIR}
test_framework  = unity
```

- [ ] **Step 2: Write a smoke test**

Create `test/test_smoke/test_smoke.cpp`:

```cpp
#include <unity.h>

void setUp() {}
void tearDown() {}

static void test_harness_runs() {
    TEST_ASSERT_EQUAL_INT(4, 2 + 2);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_harness_runs);
    return UNITY_END();
}
```

- [ ] **Step 3: Run the test, verify it passes**

Run: `pio test -e test`
Expected: `test_harness_runs` PASS, `1 Tests 0 Failures 0 Ignored`.

- [ ] **Step 4: Commit**

```bash
git add platformio.ini test/test_smoke/test_smoke.cpp
git commit -m "test: add native Unity test environment + smoke test"
```

---

### Task 1: Mode / Screen abstractions

**Files:**
- Create: `core/app/Mode.h`
- Create: `test/support/Fakes.h`
- Test: `test/test_shell/test_shell.cpp`

- [ ] **Step 1: Write `core/app/Mode.h`**

```cpp
#pragma once

#include <cstdint>

#include "core/MidiMessage.h"

namespace core {

class Display;

// Transport actions produced by the Latch1-3 panel switches.
enum class Transport { Play, Pause, Stop, Reset };

// Raw hardware event, used only by observability modes (Debug). Normal
// modes use the semantic Screen/Mode callbacks instead. Indices are
// 1-based: encoders 1..5, latches 1..3.
struct RawInput {
    enum class Kind { EncoderKnob, EncoderSw, Latch };
    Kind kind;
    int  index   = 0;
    int  delta   = 0;      // EncoderKnob only
    bool on      = false;  // Latch only
};

// One interactive page inside a Mode. Receives encoders 1..4 only;
// encoder 5 is reserved by the shell (screen switch / mode overlay).
class Screen {
public:
    virtual ~Screen() = default;
    virtual const char* name() const = 0;
    virtual void onEnter() {}
    virtual void onExit() {}
    virtual void onEncoder(int index, int delta) { (void)index; (void)delta; }
    virtual void onEncoderSw(int index) { (void)index; }
    virtual void update(uint32_t nowMs) { (void)nowMs; }
    virtual void render(Display& d) const = 0;
};

// A top-level unit of behaviour. Owns its screens and mode-local state.
class Mode {
public:
    virtual ~Mode() = default;
    virtual const char* name() const = 0;
    virtual int     screenCount() const = 0;
    virtual Screen& screen(int i) = 0;
    virtual void onEnter() {}
    virtual void onExit() {}
    virtual void onMidiIn(const MidiMessage& msg) { (void)msg; }
    virtual void onTransport(Transport t) { (void)t; }
    virtual void onRawInput(const RawInput& in) { (void)in; }
    virtual void update(uint32_t nowMs) { (void)nowMs; }
};

} // namespace core
```

- [ ] **Step 2: Write fakes used across shell tests**

Create `test/support/Fakes.h`:

```cpp
#pragma once

#include <string>
#include <vector>

#include "core/app/Mode.h"
#include "core/Display.h"

// A screen that records the callbacks it receives.
struct FakeScreen : core::Screen {
    std::string label;
    int enters = 0, exits = 0, renders = 0;
    std::vector<std::pair<int,int>> encoders;  // (index, delta)
    std::vector<int> sws;

    explicit FakeScreen(std::string l) : label(std::move(l)) {}
    const char* name() const override { return label.c_str(); }
    void onEnter() override { ++enters; }
    void onExit() override { ++exits; }
    void onEncoder(int i, int d) override { encoders.push_back({i, d}); }
    void onEncoderSw(int i) override { sws.push_back(i); }
    void render(core::Display&) const override {}
};

// A mode holding N fake screens; records transport + raw input.
struct FakeMode : core::Mode {
    std::string label;
    std::vector<FakeScreen*> screens;
    std::vector<core::Transport> transports;
    int rawCount = 0, midiCount = 0, enters = 0, exits = 0;

    FakeMode(std::string l, int screenN) : label(std::move(l)) {
        for (int i = 0; i < screenN; ++i)
            screens.push_back(new FakeScreen(label + ":s" + std::to_string(i)));
    }
    ~FakeMode() override { for (auto* s : screens) delete s; }

    const char* name() const override { return label.c_str(); }
    int screenCount() const override { return static_cast<int>(screens.size()); }
    core::Screen& screen(int i) override { return *screens[i]; }
    void onEnter() override { ++enters; }
    void onExit() override { ++exits; }
    void onMidiIn(const core::MidiMessage&) override { ++midiCount; }
    void onTransport(core::Transport t) override { transports.push_back(t); }
    void onRawInput(const core::RawInput&) override { ++rawCount; }
};
```

- [ ] **Step 3: Write a compile/dispatch test**

Create `test/test_shell/test_shell.cpp`:

```cpp
#include <unity.h>

#include "support/Fakes.h"

void setUp() {}
void tearDown() {}

static void test_fake_mode_screen_dispatch() {
    FakeMode m("arp", 2);
    TEST_ASSERT_EQUAL_INT(2, m.screenCount());
    m.screen(0).onEncoder(1, +1);
    auto& fs = static_cast<FakeScreen&>(m.screen(0));
    TEST_ASSERT_EQUAL_INT(1, static_cast<int>(fs.encoders.size()));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_fake_mode_screen_dispatch);
    return UNITY_END();
}
```

- [ ] **Step 4: Run, verify pass**

Run: `pio test -e test -f test_shell`
Expected: `test_fake_mode_screen_dispatch` PASS.

- [ ] **Step 5: Commit**

```bash
git add core/app/Mode.h test/support/Fakes.h test/test_shell/test_shell.cpp
git commit -m "feat(app): add Mode/Screen abstractions + test fakes"
```

---

### Task 2: AppServices interface + StubDisplay/FakeMidiOutput

**Files:**
- Create: `core/app/AppServices.h`
- Create: `test/support/StubDisplay.h`
- Create: `test/support/FakeMidiOutput.h`

- [ ] **Step 1: Write `core/app/AppServices.h`**

```cpp
#pragma once

#include <cstdint>

#include "core/app/Mode.h"

namespace core {

// Global services the shell exposes to modes. Grows over time (settings,
// scale, note state). Plan 1: tempo + transport read/control.
class AppServices {
public:
    virtual ~AppServices() = default;
    virtual uint16_t bpm() const = 0;
    virtual void     setBpm(uint16_t bpm) = 0;
    virtual Transport transport() const = 0;
};

} // namespace core
```

- [ ] **Step 2: Write `test/support/StubDisplay.h`**

```cpp
#pragma once

#include <string>
#include <vector>

#include "core/Display.h"

// Records drawing calls so tests can assert on rendered output.
struct StubDisplay : core::Display {
    int clears = 0, presents = 0, rects = 0;
    std::vector<std::string> texts;

    int  width()  const override { return 320; }
    int  height() const override { return 240; }
    void clear(uint16_t) override { ++clears; }
    void fillRect(int, int, int, int, uint16_t) override { ++rects; }
    void drawText(int, int, const char* t, uint16_t, uint16_t, int) override {
        texts.emplace_back(t);
    }
    void present() override { ++presents; }

    bool drewText(const std::string& needle) const {
        for (const auto& t : texts)
            if (t.find(needle) != std::string::npos) return true;
        return false;
    }
};
```

- [ ] **Step 3: Write `test/support/FakeMidiOutput.h`**

```cpp
#pragma once

#include "core/MidiOutput.h"

// Counts transport + clock calls for assertions.
struct FakeMidiOutput : core::MidiOutput {
    uint16_t lastBpm = 0;
    int starts = 0, continues = 0, stops = 0;

    void setClockBpm(uint16_t bpm) override { lastBpm = bpm; }
    void sendStart()    override { ++starts; }
    void sendContinue() override { ++continues; }
    void sendStop()     override { ++stops; }
    void sendNoteOn (uint8_t, uint8_t, uint8_t) override {}
    void sendNoteOff(uint8_t, uint8_t) override {}
};
```

- [ ] **Step 4: Verify the test env still compiles these headers**

Add a temporary include check to `test/test_shell/test_shell.cpp` top:
`#include "support/StubDisplay.h"` and `#include "support/FakeMidiOutput.h"`, then run `pio test -e test -f test_shell`.
Expected: still PASS (headers compile).

- [ ] **Step 5: Commit**

```bash
git add core/app/AppServices.h test/support/StubDisplay.h test/support/FakeMidiOutput.h test/test_shell/test_shell.cpp
git commit -m "feat(app): add AppServices interface + display/midi test stubs"
```

---

### Task 3: AppShell — registry, active mode/screen, Enc1–5 routing

**Files:**
- Create: `core/app/AppShell.h`, `core/app/AppShell.cpp`
- Test: `test/test_shell/test_shell.cpp`

- [ ] **Step 1: Write `core/app/AppShell.h`**

```cpp
#pragma once

#include <cstdint>

#include "core/app/AppServices.h"
#include "core/app/Mode.h"

namespace core {

class Display;
class MidiOutput;
struct MidiMessage;

// The runtime. Hosts a fixed array of modes, routes hardware input to the
// active mode/screen, runs the mode-change overlay, owns tempo + transport.
class AppShell : public AppServices {
public:
    static constexpr int      kMaxModes        = 12;
    static constexpr uint32_t kOverlayTimeoutMs = 3000;

    void addMode(Mode* mode);          // call once per mode before begin()
    void setMidiOutput(MidiOutput* o); // for transport realtime messages
    void begin();                      // enters mode 0

    // Hardware input. Encoder index 1..5, latch index 1..3.
    void onEncoderKnob(int index, int delta);
    void onEncoderSw(int index);
    void onLatch(int index, bool on);
    void onMidiIn(const MidiMessage& msg);

    void tick(uint32_t nowMs);
    void render(Display& d);

    // AppServices
    uint16_t bpm() const override { return bpm_; }
    void     setBpm(uint16_t bpm) override;
    Transport transport() const override { return transport_; }

    // Inspectors for tests.
    int activeModeIndex() const { return activeMode_; }
    int activeScreenIndex() const { return screenIndex_; }
    bool overlayOpen() const { return overlayOpen_; }
    int overlayChoice() const { return overlayChoice_; }

private:
    enum class TransportState { Stopped, Playing, Paused };

    Mode*  modes_[kMaxModes] = {};
    int    modeCount_ = 0;
    int    activeMode_ = 0;
    int    screenIndex_ = 0;

    bool     overlayOpen_ = false;
    int      overlayChoice_ = 0;
    uint32_t overlayLastInputMs_ = 0;

    MidiOutput*    out_ = nullptr;
    uint16_t       bpm_ = 120;
    TransportState transportState_ = TransportState::Stopped;
    Transport      transport_ = Transport::Stop;
    bool           lastLatchOn_[4] = {};   // 1-based; [0] unused

    uint32_t nowMs_ = 0;

    Screen& activeScreen();
    void switchScreen(int delta);
    void enterMode(int index);
    void fireRaw(const RawInput& in);
    void applyTransport(Transport t);
    void drawTopBar(Display& d) const;
    void drawOverlay(Display& d) const;
};

} // namespace core
```

- [ ] **Step 2: Write `core/app/AppShell.cpp` (registry + routing only; overlay/transport/render are stubbed for now)**

```cpp
#include "core/app/AppShell.h"

#include "core/Display.h"
#include "core/MidiOutput.h"

namespace core {

void AppShell::addMode(Mode* mode) {
    if (modeCount_ < kMaxModes) modes_[modeCount_++] = mode;
}

void AppShell::setMidiOutput(MidiOutput* o) { out_ = o; }

void AppShell::begin() {
    activeMode_ = 0;
    screenIndex_ = 0;
    if (modeCount_ > 0) {
        modes_[activeMode_]->onEnter();
        activeScreen().onEnter();
    }
}

Screen& AppShell::activeScreen() {
    return modes_[activeMode_]->screen(screenIndex_);
}

void AppShell::fireRaw(const RawInput& in) {
    if (modeCount_ > 0) modes_[activeMode_]->onRawInput(in);
}

void AppShell::switchScreen(int delta) {
    const int n = modes_[activeMode_]->screenCount();
    if (n <= 1) return;
    activeScreen().onExit();
    screenIndex_ = ((screenIndex_ + delta) % n + n) % n;
    activeScreen().onEnter();
}

void AppShell::enterMode(int index) {
    if (index == activeMode_ || index < 0 || index >= modeCount_) return;
    activeScreen().onExit();
    modes_[activeMode_]->onExit();
    activeMode_ = index;
    screenIndex_ = 0;
    modes_[activeMode_]->onEnter();
    activeScreen().onEnter();
}

void AppShell::onEncoderKnob(int index, int delta) {
    fireRaw({RawInput::Kind::EncoderKnob, index, delta, false});
    if (overlayOpen_) return;           // overlay handling added in Task 4
    if (index == 5) { switchScreen(delta); return; }
    if (index >= 1 && index <= 4) activeScreen().onEncoder(index, delta);
}

void AppShell::onEncoderSw(int index) {
    fireRaw({RawInput::Kind::EncoderSw, index, 0, false});
    if (overlayOpen_) return;           // overlay handling added in Task 4
    if (index == 5) return;             // opens overlay (added in Task 4)
    if (index >= 1 && index <= 4) activeScreen().onEncoderSw(index);
}

void AppShell::onLatch(int index, bool on) {
    fireRaw({RawInput::Kind::Latch, index, 0, on});
    // transport handling added in Task 5
}

void AppShell::onMidiIn(const MidiMessage& msg) {
    if (modeCount_ > 0) modes_[activeMode_]->onMidiIn(msg);
}

void AppShell::setBpm(uint16_t bpm) {
    bpm_ = bpm;
    if (out_) out_->setClockBpm(bpm_);
}

void AppShell::tick(uint32_t nowMs) {
    nowMs_ = nowMs;
    if (modeCount_ > 0) {
        modes_[activeMode_]->update(nowMs);
        activeScreen().update(nowMs);
    }
}

// render / drawTopBar / drawOverlay / applyTransport implemented in later tasks.

} // namespace core
```

- [ ] **Step 3: Add routing tests to `test/test_shell/test_shell.cpp`**

Add these tests and register them in `main()`:

```cpp
#include "core/app/AppShell.h"

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
```

- [ ] **Step 4: Run, verify pass**

Run: `pio test -e test -f test_shell`
Expected: all routing tests PASS.

- [ ] **Step 5: Commit**

```bash
git add core/app/AppShell.h core/app/AppShell.cpp test/test_shell/test_shell.cpp
git commit -m "feat(app): AppShell mode registry + Enc1-5 routing"
```

---

### Task 4: Mode-change overlay

**Files:**
- Modify: `core/app/AppShell.cpp`
- Test: `test/test_shell/test_shell.cpp`

- [ ] **Step 1: Implement overlay open/select/confirm/timeout**

Replace the two `// overlay handling added in Task 4` blocks and add timeout logic to `tick`. In `onEncoderSw`:

```cpp
void AppShell::onEncoderSw(int index) {
    fireRaw({RawInput::Kind::EncoderSw, index, 0, false});
    if (overlayOpen_) {
        if (index == 5) {                 // confirm
            const int chosen = overlayChoice_;
            overlayOpen_ = false;
            enterMode(chosen);
        }
        return;                            // other SW ignored while overlay open
    }
    if (index == 5) {                      // open overlay
        overlayOpen_ = true;
        overlayChoice_ = activeMode_;
        overlayLastInputMs_ = nowMs_;
        return;
    }
    if (index >= 1 && index <= 4) activeScreen().onEncoderSw(index);
}
```

In `onEncoderKnob`, replace the `if (overlayOpen_) return;` line:

```cpp
    if (overlayOpen_) {
        if (index == 5 && modeCount_ > 0) {        // Enc5 moves selection
            const int n = modeCount_;
            overlayChoice_ = ((overlayChoice_ + delta) % n + n) % n;
            overlayLastInputMs_ = nowMs_;
        }
        return;
    }
```

In `tick`, before updating the active mode, add the timeout:

```cpp
    if (overlayOpen_ && (nowMs_ - overlayLastInputMs_) >= kOverlayTimeoutMs) {
        overlayOpen_ = false;              // revert: active mode never changed
    }
```

- [ ] **Step 2: Add overlay tests**

```cpp
static void test_overlay_open_select_confirm() {
    core::AppShell shell;
    FakeMode a("a", 1), b("b", 1), c("c", 1);
    shell.addMode(&a); shell.addMode(&b); shell.addMode(&c);
    shell.begin();
    shell.onEncoderSw(5);                       // open overlay
    TEST_ASSERT_TRUE(shell.overlayOpen());
    shell.onEncoderKnob(2, +2);                 // select index 2 (c)
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
    shell.onEncoderKnob(2, +1);                  // select b at t=1000
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
    shell.tick(3500);                             // 2.5s elapsed, no input since 1000... 
    shell.onEncoderKnob(2, +1);                   // rotate at t=3500 resets timer
    shell.tick(3500 + 2999);                       // <3s since last rotate
    TEST_ASSERT_TRUE(shell.overlayOpen());
}
```

Note: `test_overlay_rotation_resets_timeout` calls `tick(3500)` while the
overlay is open but before the 3s window from open (t=1000) expires
(1000+3000=4000 > 3500), so the overlay is still open when the rotate
arrives — correct.

- [ ] **Step 3: Run, verify pass**

Run: `pio test -e test -f test_shell`
Expected: all overlay tests PASS.

- [ ] **Step 4: Commit**

```bash
git add core/app/AppShell.cpp test/test_shell/test_shell.cpp
git commit -m "feat(app): mode-change overlay (Enc5 open, Enc2 select, 3s timeout revert)"
```

---

### Task 5: Transport on Latch1–3

**Files:**
- Modify: `core/app/AppShell.cpp`
- Test: `test/test_shell/test_shell.cpp`

- [ ] **Step 1: Implement `applyTransport` + latch edge detection**

Replace `onLatch` and add `applyTransport`:

```cpp
void AppShell::onLatch(int index, bool on) {
    fireRaw({RawInput::Kind::Latch, index, 0, on});
    if (overlayOpen_) return;                 // transport suppressed in overlay
    if (index < 1 || index > 3) return;
    if (on == lastLatchOn_[index]) return;    // act on any state change
    lastLatchOn_[index] = on;
    switch (index) {
        case 1:  // Play / Pause toggles
            applyTransport(transportState_ == TransportState::Playing
                               ? Transport::Pause : Transport::Play);
            break;
        case 2: applyTransport(Transport::Stop);  break;
        case 3: applyTransport(Transport::Reset); break;
    }
}

void AppShell::applyTransport(Transport t) {
    switch (t) {
        case Transport::Play:
            if (out_) {
                if (transportState_ == TransportState::Paused) out_->sendContinue();
                else out_->sendStart();
            }
            transportState_ = TransportState::Playing;
            break;
        case Transport::Pause:
            if (out_) out_->sendStop();
            transportState_ = TransportState::Paused;
            break;
        case Transport::Stop:
        case Transport::Reset:
            if (out_) out_->sendStop();
            transportState_ = TransportState::Stopped;
            break;
    }
    transport_ = t;
    if (modeCount_ > 0) modes_[activeMode_]->onTransport(t);
}
```

- [ ] **Step 2: Add transport tests**

```cpp
static void test_latch1_toggles_play_pause_and_sends_realtime() {
    core::AppShell shell;
    FakeMidiOutput out;
    FakeMode a("a", 1);
    shell.addMode(&a);
    shell.setMidiOutput(&out);
    shell.begin();
    shell.onLatch(1, true);                  // Play (rising)
    TEST_ASSERT_EQUAL_INT(1, out.starts);
    TEST_ASSERT_EQUAL_INT(core::Transport::Play, a.transports.back());
    shell.onLatch(1, false);                 // change -> Pause
    TEST_ASSERT_EQUAL_INT(1, out.stops);
    TEST_ASSERT_EQUAL_INT(core::Transport::Pause, a.transports.back());
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
    shell.onLatch(2, true);
    TEST_ASSERT_EQUAL_INT(core::Transport::Stop, a.transports.back());
    shell.onLatch(3, true);
    TEST_ASSERT_EQUAL_INT(core::Transport::Reset, a.transports.back());
    TEST_ASSERT_EQUAL_INT(2, out.stops);     // stop + reset both send 0xFC
}
```

- [ ] **Step 3: Run, verify pass**

Run: `pio test -e test -f test_shell`
Expected: all transport tests PASS.

- [ ] **Step 4: Commit**

```bash
git add core/app/AppShell.cpp test/test_shell/test_shell.cpp
git commit -m "feat(app): Latch1-3 transport (play/pause/stop/reset) + MIDI realtime"
```

---

### Task 6: Render — top bar + active screen + overlay panel

**Files:**
- Modify: `core/app/AppShell.cpp`
- Test: `test/test_shell/test_shell.cpp`

- [ ] **Step 1: Implement `render`, `drawTopBar`, `drawOverlay`**

Append to `AppShell.cpp`:

```cpp
void AppShell::drawTopBar(Display& d) const {
    d.fillRect(0, 0, d.width(), 10, color::DarkGray);
    if (modeCount_ == 0) return;
    char line[48];
    const char* mode   = modes_[activeMode_]->name();
    const char* screen = modes_[activeMode_]->screen(screenIndex_).name();
    std::snprintf(line, sizeof(line), "%s  -  %s", mode, screen);
    d.drawText(2, 2, line, color::White, color::DarkGray, 1);
}

void AppShell::drawOverlay(Display& d) const {
    const int w = 200, h = 16 * modeCount_ + 8;
    const int x = (d.width() - w) / 2;
    const int y = (d.height() - h) / 2;
    d.fillRect(x, y, w, h, color::Black);
    d.fillRect(x, y, w, 12, color::Blue);
    d.drawText(x + 4, y + 2, "SELECT MODE", color::White, color::Blue, 1);
    for (int i = 0; i < modeCount_; ++i) {
        const bool sel = (i == overlayChoice_);
        const uint16_t bg = sel ? color::Yellow : color::Black;
        const uint16_t fg = sel ? color::Black : color::White;
        d.drawText(x + 8, y + 14 + i * 16, modes_[i]->name(), fg, bg, 1);
    }
}

void AppShell::render(Display& d) {
    d.clear(color::Black);
    if (overlayOpen_) {
        drawOverlay(d);
    } else if (modeCount_ > 0) {
        activeScreen().render(d);
    }
    drawTopBar(d);
    d.present();
}
```

Add `#include <cstdio>` at the top of `AppShell.cpp` for `snprintf`.

- [ ] **Step 2: Add render tests**

```cpp
#include "support/StubDisplay.h"

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
```

- [ ] **Step 3: Run, verify pass**

Run: `pio test -e test -f test_shell`
Expected: render tests PASS.

- [ ] **Step 4: Commit**

```bash
git add core/app/AppShell.cpp test/test_shell/test_shell.cpp
git commit -m "feat(app): render top bar + active screen + mode overlay"
```

---

### Task 7: Debug mode (ported telemetry)

**Files:**
- Create: `core/modes/DebugMode.h`, `core/modes/DebugMode.cpp`
- Test: `test/test_shell/test_shell.cpp`
- Reference: `core/MidiMonitorApp.cpp:879` (`drawDebug`) for layout.

- [ ] **Step 1: Write `core/modes/DebugMode.h`**

```cpp
#pragma once

#include <cstdint>

#include "core/app/Mode.h"

namespace core {

// Hardware bring-up telemetry: per-control activity for all 5 encoders
// and 3 latches, fed via the shell's raw-input tap. One screen.
class DebugMode : public Mode {
public:
    DebugMode();
    const char* name() const override { return "Debug"; }
    int     screenCount() const override { return 1; }
    Screen& screen(int) override { return screen_; }
    void onRawInput(const RawInput& in) override;
    void update(uint32_t nowMs) override { nowMs_ = nowMs; }

private:
    struct Knob   { long total = 0; int lastDelta = 0; uint32_t lastMs = 0; };
    struct Button { unsigned count = 0; uint32_t lastMs = 0; };

    Knob     encKnob_[6];   // 1..5
    Button   encSw_[6];     // 1..5
    Button   latch_[4];     // 1..3
    uint32_t nowMs_ = 0;

    class DebugScreen : public Screen {
    public:
        explicit DebugScreen(DebugMode& m) : m_(m) {}
        const char* name() const override { return "io"; }
        void render(Display& d) const override;
    private:
        DebugMode& m_;
    };
    DebugScreen screen_{*this};
    friend class DebugScreen;
};

} // namespace core
```

- [ ] **Step 2: Write `core/modes/DebugMode.cpp`**

```cpp
#include "core/modes/DebugMode.h"

#include <cstdio>

#include "core/Display.h"

namespace core {

DebugMode::DebugMode() = default;

void DebugMode::onRawInput(const RawInput& in) {
    switch (in.kind) {
        case RawInput::Kind::EncoderKnob:
            if (in.index >= 1 && in.index <= 5 && in.delta != 0) {
                encKnob_[in.index].total += in.delta;
                encKnob_[in.index].lastDelta = in.delta > 0 ? 1 : -1;
                encKnob_[in.index].lastMs = nowMs_;
            }
            break;
        case RawInput::Kind::EncoderSw:
            if (in.index >= 1 && in.index <= 5) {
                ++encSw_[in.index].count;
                encSw_[in.index].lastMs = nowMs_;
            }
            break;
        case RawInput::Kind::Latch:
            if (in.index >= 1 && in.index <= 3) {
                ++latch_[in.index].count;
                latch_[in.index].lastMs = nowMs_;
            }
            break;
    }
}

void DebugMode::DebugScreen::render(Display& d) const {
    constexpr uint32_t kRecentMs = 500;
    int y = 14;
    char buf[40];
    for (int i = 1; i <= 5; ++i) {
        const auto& k = m_.encKnob_[i];
        const bool recent = k.lastMs != 0 && (m_.nowMs_ - k.lastMs) < kRecentMs;
        const uint16_t col = recent ? color::Yellow : color::White;
        std::snprintf(buf, sizeof(buf), "ENC%d  tot %ld  d %+d", i, k.total, k.lastDelta);
        d.drawText(4, y, buf, col, color::Black, 1);
        y += 12;
    }
    for (int i = 1; i <= 5; ++i) {
        std::snprintf(buf, sizeof(buf), "ENC%dsw  #%u", i, m_.encSw_[i].count);
        d.drawText(4, y, buf, color::White, color::Black, 1);
        y += 10;
    }
    for (int i = 1; i <= 3; ++i) {
        std::snprintf(buf, sizeof(buf), "LATCH%d  #%u", i, m_.latch_[i].count);
        d.drawText(170, 14 + (i - 1) * 12, buf, color::White, color::Black, 1);
    }
}

} // namespace core
```

- [ ] **Step 3: Add a DebugMode test**

```cpp
#include "core/modes/DebugMode.h"

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
```

- [ ] **Step 4: Run, verify pass**

Run: `pio test -e test -f test_shell`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add core/modes/DebugMode.h core/modes/DebugMode.cpp test/test_shell/test_shell.cpp
git commit -m "feat(modes): port Debug telemetry to a Mode"
```

---

### Task 8: BPM mode

**Files:**
- Create: `core/modes/BpmMode.h`, `core/modes/BpmMode.cpp`
- Test: `test/test_shell/test_shell.cpp`
- Reference: `core/MidiMonitorApp.cpp:1164` (`drawBigBpm`) for the big-number style.

- [ ] **Step 1: Write `core/modes/BpmMode.h`**

```cpp
#pragma once

#include "core/app/AppServices.h"
#include "core/app/Mode.h"

namespace core {

// Big current-tempo readout. Enc1 adjusts BPM via AppServices. One screen.
class BpmMode : public Mode {
public:
    explicit BpmMode(AppServices& svc);
    const char* name() const override { return "BPM"; }
    int     screenCount() const override { return 1; }
    Screen& screen(int) override { return screen_; }

private:
    AppServices& svc_;

    class BpmScreen : public Screen {
    public:
        explicit BpmScreen(AppServices& s) : svc_(s) {}
        const char* name() const override { return "tempo"; }
        void onEncoder(int index, int delta) override {
            if (index == 1) {
                int v = static_cast<int>(svc_.bpm()) + delta;
                if (v < 30) v = 30; if (v > 300) v = 300;
                svc_.setBpm(static_cast<uint16_t>(v));
            }
        }
        void render(Display& d) const override;
    private:
        AppServices& svc_;
    };
    BpmScreen screen_{svc_};
};

} // namespace core
```

- [ ] **Step 2: Write `core/modes/BpmMode.cpp`**

```cpp
#include "core/modes/BpmMode.h"

#include <cstdio>

#include "core/Display.h"

namespace core {

BpmMode::BpmMode(AppServices& svc) : svc_(svc) {}

void BpmMode::BpmScreen::render(Display& d) const {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%u", svc_.bpm());
    // Big number centred-ish; size 6 ~ 30x42 px per glyph.
    d.drawText(90, 90, buf, color::Cyan, color::Black, 6);
    d.drawText(140, 150, "BPM", color::White, color::Black, 2);
}

} // namespace core
```

- [ ] **Step 3: Add BPM tests**

```cpp
#include "core/modes/BpmMode.h"

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
```

- [ ] **Step 4: Run, verify pass**

Run: `pio test -e test -f test_shell`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add core/modes/BpmMode.h core/modes/BpmMode.cpp test/test_shell/test_shell.cpp
git commit -m "feat(modes): BPM readout mode (Enc1 adjusts tempo)"
```

---

### Task 9: Wire both platform mains to AppShell

**Files:**
- Modify: `platform/teensy/main.cpp`
- Modify: `platform/host/main.cpp`

The shell input API is `onEncoderKnob(idx,delta)`, `onEncoderSw(idx)`,
`onLatch(idx,on)`. Map the existing hardware/sim plumbing to it. Keep
`MidiMonitorApp` out of the mains (the file stays in the tree, unused,
for later renderer extraction).

- [ ] **Step 1: Rewire `platform/teensy/main.cpp`**

Replace the `core::MidiMonitorApp app;` line and the body of `setup()` /
`loop()` that talked to `app.on*` with the shell. Keep all `kPin*`,
encoder/button instances, and `midiOut` as-is. New shell wiring:

```cpp
#include "core/app/AppShell.h"
#include "core/modes/DebugMode.h"
#include "core/modes/BpmMode.h"

static core::AppShell    app;
static core::DebugMode   debugMode;
static core::BpmMode     bpmMode(app);
```

In `setup()`, after `app.setMidiOutput(&midiOut);` register modes and begin:

```cpp
    app.addMode(&bpmMode);
    app.addMode(&debugMode);
    app.setBpm(120);
    app.begin();
```

In `loop()`, replace the per-encoder dispatch with index-based calls.
Encoders (rotation):

```cpp
    const int d1 = enc1Knob.poll(); if (d1) app.onEncoderKnob(1, d1);
    const int d2 = enc2Knob.poll(); if (d2) app.onEncoderKnob(2, d2);
    const int d3 = enc3Knob.poll(); if (d3) app.onEncoderKnob(3, d3);
    const int d4 = enc4Knob.poll(); if (d4) app.onEncoderKnob(4, d4);
    const int d5 = enc5Knob.poll(); if (d5) app.onEncoderKnob(5, d5);
```

Encoder SW (edge-detected, one helper pattern per encoder; shown for 1,
repeat for 2–5 with their `enc*Switch` and a distinct `last` static):

```cpp
    static bool sw1Last = false; const bool sw1 = enc1Switch.pollOn();
    if (sw1 && !sw1Last) app.onEncoderSw(1); sw1Last = sw1;
    static bool sw2Last = false; const bool sw2 = enc2Switch.pollOn();
    if (sw2 && !sw2Last) app.onEncoderSw(2); sw2Last = sw2;
    static bool sw3Last = false; const bool sw3 = enc3Switch.pollOn();
    if (sw3 && !sw3Last) app.onEncoderSw(3); sw3Last = sw3;
    static bool sw4Last = false; const bool sw4 = enc4Switch.pollOn();
    if (sw4 && !sw4Last) app.onEncoderSw(4); sw4Last = sw4;
    static bool sw5Last = false; const bool sw5 = enc5Switch.pollOn();
    if (sw5 && !sw5Last) app.onEncoderSw(5); sw5Last = sw5;
```

Latches (pass current state every loop; the shell edge-detects):

```cpp
    app.onLatch(1, latch1Button.pollOn());
    app.onLatch(2, latch2Button.pollOn());
    app.onLatch(3, latch3Button.pollOn());
```

MIDI in + tick + render stay structurally the same:

```cpp
    core::MidiMessage msg;
    while (midiIn.poll(msg)) app.onMidiIn(msg);
    const uint32_t now = millis();
    app.tick(now);
    static uint32_t lastRender = 0;
    if (now - lastRender >= 33) { app.render(display); lastRender = now; }
```

- [ ] **Step 2: Build the firmware**

Run: `pio run -e teensy41`
Expected: `SUCCESS`.

- [ ] **Step 3: Rewire `platform/host/main.cpp`**

Keep `keyToNote`, `kEncoderTrios`, the SDL loop, note injection, and
Shift+number channel select. Replace `core::MidiMonitorApp app;` with the
shell + modes (same three lines as Task 9 Step 1). In `handleEncoderKey`,
replace the per-encoder `app.on*` switch with a single mapping (EncoderId
0..4 → shell index 1..5):

```cpp
bool handleEncoderKey(core::AppShell& app, SDL_Scancode sc) {
    for (const EncoderTrio& t : kEncoderTrios) {
        int delta = 0; bool click = false;
        if      (sc == t.left)  delta = -1;
        else if (sc == t.right) delta = +1;
        else if (sc == t.click) click = true;
        else continue;
        const int idx = static_cast<int>(t.id) + 1;   // Enc1->1 .. Enc5->5
        if (click) app.onEncoderSw(idx); else app.onEncoderKnob(idx, delta);
        return true;
    }
    return false;
}
```

Replace the SPACE handler (was `app.onLatch1(...)`) — SPACE now toggles
Latch1 (transport Play/Pause) for testing; keep BACKSPACE → there is no
`panic()` on the shell yet, so map BACKSPACE to Latch2 (Stop) for now:

```cpp
    if (ev.key.keysym.sym == SDLK_SPACE)     { static bool s=false; s=!s; app.onLatch(1, s); break; }
    if (ev.key.keysym.sym == SDLK_BACKSPACE) { static bool s=false; s=!s; app.onLatch(2, s); break; }
```

Register modes + begin before the loop (after `app.setMidiOutput(&midiOut);`):

```cpp
    static core::DebugMode debugMode;
    static core::BpmMode   bpmMode(app);
    app.addMode(&bpmMode);
    app.addMode(&debugMode);
    app.setBpm(120);
    app.begin();
```

Update the help text: replace the mapping/panic lines with
`SPACE = Play/Pause (Latch1), BACKSPACE = Stop (Latch2), Enc5 rotate = screen, Enc5 click = mode overlay`.

- [ ] **Step 4: Build the simulator**

Run: `pio run -e native`
Expected: `SUCCESS`.

- [ ] **Step 5: Visual verification in the simulator**

Run: `pio run -e native -t exec` (or `./scripts/run-sim.sh`).
Confirm:
- Boots into the **BPM** mode showing `120`.
- `q` then `w` (Enc5 rotate / click): rotate is a no-op (BPM has 1 screen); **click opens the mode overlay** listing `BPM` and `Debug`.
- In the overlay, rotating Enc5 (`q`/`e` keys in the sim) moves the highlight; Enc5 click confirms → switches mode. Waiting 3 s reverts.
- In **BPM** mode, `+`/`š` (Enc1 rotate) changes the number.
- In **Debug** mode, every encoder/latch updates its row.
- Top bar shows `BPM - tempo` / `Debug - io`.

- [ ] **Step 6: Run the full test suite once more**

Run: `pio test -e test`
Expected: all suites PASS.

- [ ] **Step 7: Commit**

```bash
git add platform/teensy/main.cpp platform/host/main.cpp
git commit -m "feat(app): drive both platforms via AppShell (Debug + BPM modes)"
```

---

## Self-review notes

- **Spec coverage:** framework (§5), overlay (§4), transport (§3) — Tasks 3–6; hardware map (§3) — Task 9; Debug/BPM modes (§7.4, §7.6) — Tasks 7–8. Monitoring/Arp/Berlin/Settings are explicitly later plans (per §10 migration). `MidiMonitorApp` retained for later extraction per §9.
- **Deferred (correct for Plan 1):** `NoteState`, worms/notation renderers, `Settings`, `Scale`, `panic()`, mapping editor — all later plans.
- **Type consistency:** input API (`onEncoderKnob`/`onEncoderSw`/`onLatch`), `Transport`, `RawInput`, `AppServices::bpm/setBpm/transport`, `Mode`/`Screen` signatures are used identically across tasks and mains.
- **Known follow-ups for Plan 2:** restore `panic()` (likely as an AppServices method or a Monitoring concern), and decide whether background (non-active) modes need `update()`.
