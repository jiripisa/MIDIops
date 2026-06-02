# MIDIops v1 — Plan 2: Monitoring mode (worms) + renderer extraction

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extract the worm/keyboard rendering and its note/worm state out of the legacy `MidiMonitorApp` into reusable, partly unit-testable components, and build a real `MonitoringMode` (Worms screen) on the AppShell framework, fed by live MIDI.

**Architecture:** Split into a **model** (`NoteWormModel` — held-note bitmasks + worm lifecycle + scroll, no `Display`, unit-tested) and a **renderer** (`WormsRenderer` — draws worms + keyboard from the model). Shared pixel helpers (`scaleRgb565`, `channelColor`, key-geometry, palette) move from the `MidiMonitorApp.cpp` anonymous namespace into `core/render/` headers. `MonitoringMode` owns a `NoteWormModel`, feeds it from `Mode::onMidiIn`, ticks it in `Mode::update`, and renders via the Worms screen.

**Tech Stack:** C++17, PlatformIO (envs `teensy41`, `native`, `test`), Unity, SDL2/RtMidi (sim), ILI9341_t3n (firmware).

This is **Plan 2 of the series** (`docs/specs/2026-06-02-v1-mode-architecture-design.md`). The **Notes screen / `NotationRenderer`** is intentionally deferred to **Plan 3** (notation is ~300 lines of independent pixel logic with its own name-drift animation). The legacy `MidiMonitorApp.{h,cpp}` stays in the tree until all its rendering is extracted.

Source of truth for ported pixel code: `core/MidiMonitorApp.{h,cpp}` at the line ranges named per task. When a task says "port verbatim", reproduce the existing logic exactly, applying only the member→accessor substitutions listed.

---

## File structure (Plan 2)

- Create `core/render/KeyLayout.h` — keyboard geometry constants + `KeyRect` + key helpers (`keyRectFor`, `whiteKeyIdx`, `whiteKeyAt`, `isBlackPc`, `noteVisible`), all free functions in `namespace core`.
- Create `core/render/Color.h` — `scaleRgb565`, `channelColor`, the 16-entry channel palette.
- Create `core/NoteWormModel.h` / `core/NoteWormModel.cpp` — `Worm` struct + note/worm state + lifecycle + scroll.
- Create `core/render/WormsRenderer.h` / `core/render/WormsRenderer.cpp` — `drawWorms` + `drawKeyboard` over a `const NoteWormModel&`.
- Create `core/modes/MonitoringMode.h` / `core/modes/MonitoringMode.cpp` — the mode + its Worms screen.
- Test: `test/test_monitoring/test_monitoring.cpp` — new Unity suite (model + renderer + mode).
- Modify `platform/teensy/main.cpp`, `platform/host/main.cpp` — register `MonitoringMode` as the first mode.

Conventions: 2-space indent, `core::` namespace, RGB565, English identifiers/comments, no platform headers in `core/`, no exceptions in `core/`.

Roll/keyboard geometry (from the existing app, keep identical): `kScreenW=320`, `kHeaderH=20`, `kRollTop=22`, `kRollBottom=180`, `kKeyboardTop=180`, `kKeyboardBot=240`, `kBlackKeyH=32`, `kLowestNote=36`, `kHighestNote=83`, `kWhiteKeysVisible=28`, `kWhiteKeyW=11`, `kBlackKeyW=7`, `kKeyboardX0=(320-28*11)/2=6`. Scroll: `kScrollPxPerSec=50`, `kMaxWorms=64`, `kPostRollMargin=200`.

---

### Task 1: Key geometry helpers → `core/render/KeyLayout.h`

**Files:**
- Create: `core/render/KeyLayout.h`
- Test: `test/test_monitoring/test_monitoring.cpp`
- Source: `core/MidiMonitorApp.cpp` anonymous-namespace tables (`kWhiteIdxInOctave`, `kWhitePc`) near the top, and the static helpers `isBlackPc` (`:170`), `whiteKeyIdx` (`:174`), `whiteKeyAt` (`:180`), `keyRectFor` (`:186`); `KeyRect` struct (`MidiMonitorApp.h:191`); the layout constants (`MidiMonitorApp.h:139`).

- [ ] **Step 1: Write `core/render/KeyLayout.h`** — a header-only `namespace core` containing: the geometry constants listed above as `constexpr int`; `struct KeyRect { int x; int w; bool isBlack; };`; and these `inline` free functions ported verbatim from the named sources, with the only change that `MidiMonitorApp::` static members become plain `inline` functions and the anon-namespace tables become `static constexpr` arrays inside this header:

```cpp
#pragma once

#include <cstdint>

namespace core {

// Keyboard window geometry (must start on a C, end on a B).
constexpr int kScreenW        = 320;
constexpr int kRollTop        = 22;
constexpr int kRollBottom     = 180;
constexpr int kKeyboardTop    = 180;
constexpr int kKeyboardBot    = 240;
constexpr int kBlackKeyH      = 32;
constexpr uint8_t kLowestNote  = 36;   // C2
constexpr uint8_t kHighestNote = 83;   // B5
constexpr int kWhiteKeysVisible = 28;
constexpr int kWhiteKeyW        = 11;
constexpr int kBlackKeyW        = 7;
constexpr int kKeyboardX0       = (kScreenW - kWhiteKeysVisible * kWhiteKeyW) / 2;

struct KeyRect { int x; int w; bool isBlack; };

inline bool noteVisible(uint8_t note) {
    return note >= kLowestNote && note <= kHighestNote;
}

// pitch-class -> is it a black key. Copy the exact table + body from
// MidiMonitorApp.cpp (kWhiteIdxInOctave / isBlackPc).
inline bool isBlackPc(int pc) { /* ported verbatim */ }

// note -> 0-based white-key index from kLowestNote (ported from whiteKeyIdx).
inline int whiteKeyIdx(uint8_t note) { /* ported verbatim */ }

// inverse of whiteKeyIdx (ported from whiteKeyAt).
inline uint8_t whiteKeyAt(int idx) { /* ported verbatim */ }

// note -> pixel rect (ported from keyRectFor; returns {-1,0,false} if !noteVisible).
inline KeyRect keyRectFor(uint8_t note) { /* ported verbatim */ }

} // namespace core
```

The implementer must open the source file and paste the real bodies/tables (the `/* ported verbatim */` markers are placeholders for the actual ported code — do not leave them in).

- [ ] **Step 2: Write the test suite skeleton + key-layout tests** in `test/test_monitoring/test_monitoring.cpp`:

```cpp
#include <unity.h>

#include "core/render/KeyLayout.h"

void setUp() {}
void tearDown() {}

static void test_white_key_index_roundtrip() {
    for (int i = 0; i < core::kWhiteKeysVisible; ++i) {
        const uint8_t note = core::whiteKeyAt(i);
        TEST_ASSERT_EQUAL_INT(i, core::whiteKeyIdx(note));
    }
}

static void test_c2_is_white_first_key() {
    TEST_ASSERT_FALSE(core::isBlackPc(0));            // C is white
    TEST_ASSERT_TRUE(core::isBlackPc(1));             // C# is black
    const core::KeyRect r = core::keyRectFor(core::kLowestNote);  // C2
    TEST_ASSERT_FALSE(r.isBlack);
    TEST_ASSERT_EQUAL_INT(core::kKeyboardX0, r.x);
}

static void test_out_of_range_note_has_invalid_rect() {
    const core::KeyRect r = core::keyRectFor(0);
    TEST_ASSERT_EQUAL_INT(-1, r.x);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_white_key_index_roundtrip);
    RUN_TEST(test_c2_is_white_first_key);
    RUN_TEST(test_out_of_range_note_has_invalid_rect);
    return UNITY_END();
}
```

- [ ] **Step 3: Run** `pio test -e test -f test_monitoring` → expect PASS. (If `isBlackPc(0)` etc. fail, the table was ported wrong — re-check against the source.)
- [ ] **Step 4: Commit**
```bash
git add core/render/KeyLayout.h test/test_monitoring/test_monitoring.cpp
git commit -m "feat(render): extract keyboard geometry helpers to KeyLayout.h"
```

---

### Task 2: Colour helpers → `core/render/Color.h`

**Files:**
- Create: `core/render/Color.h`
- Test: `test/test_monitoring/test_monitoring.cpp`
- Source: `scaleRgb565` (anonymous namespace, `core/MidiMonitorApp.cpp`), `channelColor` (`:199`) and its `kChannelPalette[16]` table (anonymous namespace).

- [ ] **Step 1: Write `core/render/Color.h`** — header-only `namespace core`:

```cpp
#pragma once

#include <cstdint>

#include "core/Display.h"   // for color:: constants + rgb565

namespace core {

// Scale an RGB565 colour's brightness by factor/256 (ported verbatim from
// MidiMonitorApp.cpp scaleRgb565).
inline uint16_t scaleRgb565(uint16_t c, uint16_t factor) { /* ported verbatim */ }

// 1-based MIDI channel -> display colour. Channel 0 or >16 -> white.
// Copy the exact kChannelPalette[16] table from MidiMonitorApp.cpp.
inline uint16_t channelColor(uint8_t channel) { /* ported verbatim */ }

} // namespace core
```

Paste the real `scaleRgb565` body and the real `kChannelPalette` contents from the source (make the palette a `static constexpr uint16_t kChannelPalette[16]` inside the header).

- [ ] **Step 2: Add colour tests** to `test/test_monitoring/test_monitoring.cpp` (add `#include "core/render/Color.h"` and register the tests in `main()`):

```cpp
static void test_scale_full_is_identity() {
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, core::scaleRgb565(0xFFFF, 256));
}
static void test_scale_zero_is_black() {
    TEST_ASSERT_EQUAL_HEX16(0x0000, core::scaleRgb565(0xFFFF, 0));
}
static void test_channel0_is_white() {
    TEST_ASSERT_EQUAL_HEX16(core::color::White, core::channelColor(0));
}
static void test_channels_have_distinct_colors() {
    TEST_ASSERT_NOT_EQUAL(core::channelColor(1), core::channelColor(2));
}
```

- [ ] **Step 3: Run** `pio test -e test -f test_monitoring` → expect PASS.
- [ ] **Step 4: Commit**
```bash
git add core/render/Color.h test/test_monitoring/test_monitoring.cpp
git commit -m "feat(render): extract scaleRgb565 + channelColor to Color.h"
```

---

### Task 3: `NoteWormModel` — note/worm state + lifecycle (model, no Display)

**Files:**
- Create: `core/NoteWormModel.h`, `core/NoteWormModel.cpp`
- Test: `test/test_monitoring/test_monitoring.cpp`
- Source: `Worm` struct (`MidiMonitorApp.h:170`); `notePressedBy_`/`outNotePressedBy_` (`:201`,`:207`); `onNoteOn` (`:621`), `onNoteOff` (`:646`), `onEngineEcho` (`:220`), `releaseAllNotesOnChannel` (`:548`), `advanceWorms` (`:679`), `pressedChannelFor` (`:665`), `outPressedChannelFor` (`:259`); scroll constants.

- [ ] **Step 1: Write `core/NoteWormModel.h`**

```cpp
#pragma once

#include <cstdint>

#include "core/MidiMessage.h"

namespace core {

class NoteWormModel {
public:
    static constexpr int      kMaxWorms       = 64;
    static constexpr uint32_t kScrollPxPerSec = 50;
    static constexpr int      kPostRollMargin = 200;

    struct Worm {
        bool     live     = false;
        bool     growing  = false;
        bool     isOutput = false;
        uint8_t  note     = 0;
        uint8_t  channel  = 0;
        int16_t  topY     = 0;
        int16_t  bottomY  = 0;
        uint32_t startMs  = 0;
        uint32_t endMs    = 0;
    };

    // Incoming-note events (caller applies any channel filtering upstream).
    void onNoteOn (uint8_t channel, uint8_t note);
    void onNoteOff(uint8_t channel, uint8_t note);
    // Engine-played (ghost) note events — visualised as output worms.
    void onEngineNoteOn (uint8_t channel, uint8_t note);
    void onEngineNoteOff(uint8_t channel, uint8_t note);
    // Release everything held on a channel (CC120/123 / channel change).
    void releaseAllOnChannel(uint8_t channel);
    // Drop all input state (e.g. on a monitored-channel switch).
    void clearInput();

    // Advance scroll using a monotonic millisecond clock.
    void tick(uint32_t nowMs);

    // Queries used by renderers.
    const Worm* worms() const { return worms_; }
    int   maxWorms() const { return kMaxWorms; }
    uint8_t pressedChannelFor(uint8_t note) const;     // lowest input channel, 0=none
    uint8_t outPressedChannelFor(uint8_t note) const;  // lowest engine channel, 0=none
    uint32_t lastTickMs() const { return lastTickMs_; }

private:
    void advanceWorms(int dy);
    int  spawnWorm(uint8_t channel, uint8_t note, bool isOutput, uint32_t nowMs);
    void stopWorm(uint8_t channel, uint8_t note, bool isOutput, uint32_t nowMs);

    uint16_t notePressedBy_[128]    = {};
    uint16_t outNotePressedBy_[128] = {};
    Worm     worms_[kMaxWorms]      = {};
    uint32_t lastTickMs_   = 0;
    uint32_t scrollAccumMs_= 0;
    bool     started_      = false;
};

} // namespace core
```

- [ ] **Step 2: Write the failing model tests** in `test/test_monitoring/test_monitoring.cpp` (add `#include "core/NoteWormModel.h"`, register in `main()`):

```cpp
static int countLive(const core::NoteWormModel& m) {
    int n = 0;
    for (int i = 0; i < m.maxWorms(); ++i) if (m.worms()[i].live) ++n;
    return n;
}

static void test_noteon_spawns_growing_input_worm() {
    core::NoteWormModel m;
    m.tick(0);
    m.onNoteOn(1, 60);
    TEST_ASSERT_EQUAL_INT(1, countLive(m));
    // find it
    const core::NoteWormModel::Worm* w = nullptr;
    for (int i = 0; i < m.maxWorms(); ++i) if (m.worms()[i].live) w = &m.worms()[i];
    TEST_ASSERT_TRUE(w->growing);
    TEST_ASSERT_FALSE(w->isOutput);
    TEST_ASSERT_EQUAL_INT(60, w->note);
    TEST_ASSERT_EQUAL_INT(core::kRollBottom - 1, w->bottomY);  // anchored at keyboard
}

static void test_pressedChannelFor_tracks_bitmask() {
    core::NoteWormModel m;
    m.tick(0);
    m.onNoteOn(3, 64);
    TEST_ASSERT_EQUAL_INT(3, m.pressedChannelFor(64));
    m.onNoteOff(3, 64);
    TEST_ASSERT_EQUAL_INT(0, m.pressedChannelFor(64));
}

static void test_noteoff_freezes_worm_growth() {
    core::NoteWormModel m;
    m.tick(0);
    m.onNoteOn(1, 60);
    m.onNoteOff(1, 60);
    const core::NoteWormModel::Worm* w = nullptr;
    for (int i = 0; i < m.maxWorms(); ++i) if (m.worms()[i].live) w = &m.worms()[i];
    TEST_ASSERT_TRUE(w != nullptr);
    TEST_ASSERT_FALSE(w->growing);   // released
}

static void test_tick_scrolls_growing_worm_topY_up() {
    core::NoteWormModel m;
    m.tick(0);
    m.onNoteOn(1, 60);
    const int16_t top0 = m.worms()[0].topY;
    m.tick(100);                      // 100ms @ 50px/s = 5px
    TEST_ASSERT_TRUE(m.worms()[0].topY < top0);          // moved up
    TEST_ASSERT_EQUAL_INT(core::kRollBottom - 1, m.worms()[0].bottomY); // still anchored
}

static void test_released_worm_eventually_expires() {
    core::NoteWormModel m;
    m.tick(0);
    m.onNoteOn(1, 60);
    m.onNoteOff(1, 60);
    for (uint32_t t = 100; t <= 20000; t += 100) m.tick(t);  // scroll far past the top
    TEST_ASSERT_EQUAL_INT(0, countLive(m));
}

static void test_engine_worm_is_output() {
    core::NoteWormModel m;
    m.tick(0);
    m.onEngineNoteOn(2, 67);
    TEST_ASSERT_EQUAL_INT(2, m.outPressedChannelFor(67));
    bool foundOut = false;
    for (int i = 0; i < m.maxWorms(); ++i)
        if (m.worms()[i].live && m.worms()[i].isOutput) foundOut = true;
    TEST_ASSERT_TRUE(foundOut);
}
```

- [ ] **Step 3: Run, verify they FAIL** (model not implemented): `pio test -e test -f test_monitoring` → compile/link error or failures.

- [ ] **Step 4: Write `core/NoteWormModel.cpp`** — port the logic from the named sources. Key transformations from `MidiMonitorApp`:
  - `onNoteOn(channel,note)`: set bit `(channel-1)` in `notePressedBy_[note]`; `spawnWorm(channel,note,false,lastTickMs_)` (initial `topY=bottomY=kRollBottom-1`, `growing=true`). Guard `noteVisible`/channel 1..16 — but `noteVisible` lives in KeyLayout.h; include it (`#include "core/render/KeyLayout.h"`).
  - `onNoteOff`: clear the bit; `stopWorm(...,false,...)` (set matching live+growing worm `growing=false,endMs=lastTickMs_`).
  - `onEngineNoteOn/Off`: same against `outNotePressedBy_` and `isOutput=true`.
  - `releaseAllOnChannel(ch)`: clear that channel's bit across all 128 notes (input mask) and stop its growing input worms. (Mirror `releaseAllNotesOnChannel`.)
  - `clearInput()`: zero `notePressedBy_`; freeze all growing input worms (`growing=false,endMs=lastTickMs_`). (Mirror the body of `setChannel` that resets note state.)
  - `tick(nowMs)`: first call seeds `lastTickMs_` (and `started_`) and returns; else accumulate `scrollAccumMs_ += nowMs-lastTickMs_`, drain at `1000/kScrollPxPerSec` ms/px into `dy` (cap at `kRollBottom-kRollTop`), `advanceWorms(dy)`. (Port from `tick`, MINUS the chord-engine call and splash logic — those are not the model's concern.)
  - `advanceWorms(dy)`: port verbatim (topY-=dy; growing → clamp topY to kRollTop, bottomY fixed; else bottomY-=dy; expire when `bottomY < kRollTop - kPostRollMargin`).
  - `pressedChannelFor`/`outPressedChannelFor`: port verbatim (LSB-first scan).
  - `spawnWorm`: first `!live` slot; fill fields; return index or -1 if full.

- [ ] **Step 5: Run, verify PASS** `pio test -e test -f test_monitoring`.
- [ ] **Step 6: Commit**
```bash
git add core/NoteWormModel.h core/NoteWormModel.cpp test/test_monitoring/test_monitoring.cpp
git commit -m "feat(core): NoteWormModel — held-note + worm lifecycle (unit-tested)"
```

---

### Task 4: `WormsRenderer` — draw worms + keyboard from the model

**Files:**
- Create: `core/render/WormsRenderer.h`, `core/render/WormsRenderer.cpp`
- Test: `test/test_monitoring/test_monitoring.cpp`
- Source: `drawWorms` (`MidiMonitorApp.cpp:965`), `drawKeyboard` (`:1056`).

- [ ] **Step 1: Write `core/render/WormsRenderer.h`**

```cpp
#pragma once

namespace core {

class Display;
class NoteWormModel;

// Draws the per-channel worm roll and the piano keyboard for a NoteWormModel.
// Stateless — all state comes from the model.
namespace WormsRenderer {
    void drawWorms(const NoteWormModel& model, Display& d);
    void drawKeyboard(const NoteWormModel& model, Display& d);
    // Convenience: roll + keyboard in the canonical order.
    void render(const NoteWormModel& model, Display& d);
}

} // namespace core
```

- [ ] **Step 2: Write `core/render/WormsRenderer.cpp`** — port the bodies of `MidiMonitorApp::drawWorms` and `drawKeyboard` verbatim, with these substitutions:
  - `worms_` → `model.worms()`, `kMaxWorms` → `model.maxWorms()`.
  - `pressedChannelFor(n)` → `model.pressedChannelFor(n)`; `outPressedChannelFor(n)` → `model.outPressedChannelFor(n)`.
  - `keyRectFor/whiteKeyIdx/whiteKeyAt/isBlackPc/noteVisible` → the `core::` free functions from `KeyLayout.h`.
  - `channelColor/scaleRgb565` → the `core::` functions from `Color.h`.
  - layout constants (`kRollTop`, etc.) → from `KeyLayout.h`.
  - `color::*` from `Display.h`.
  Includes: `core/render/WormsRenderer.h`, `core/render/KeyLayout.h`, `core/render/Color.h`, `core/NoteWormModel.h`, `core/Display.h`. `render()` = `drawWorms(model,d); drawKeyboard(model,d);`.

- [ ] **Step 3: Add renderer tests** to `test/test_monitoring/test_monitoring.cpp` (uses the existing `StubDisplay` from `test/support/StubDisplay.h` — add the include):

```cpp
#include "support/StubDisplay.h"
#include "core/render/WormsRenderer.h"

static void test_renderer_draws_keyboard_surface() {
    core::NoteWormModel m; m.tick(0);
    StubDisplay d;
    core::WormsRenderer::render(m, d);
    TEST_ASSERT_TRUE(d.rects > 0);   // at least the keyboard surface + separators
}

static void test_live_input_worm_adds_fills() {
    core::NoteWormModel m; m.tick(0);
    StubDisplay empty; core::WormsRenderer::drawWorms(m, empty);
    const int before = empty.rects;
    m.onNoteOn(1, 60); m.tick(200);   // grow a worm a few px
    StubDisplay withWorm; core::WormsRenderer::drawWorms(m, withWorm);
    TEST_ASSERT_TRUE(withWorm.rects > before);
}
```

- [ ] **Step 4: Run** `pio test -e test -f test_monitoring` → PASS.
- [ ] **Step 5: Commit**
```bash
git add core/render/WormsRenderer.h core/render/WormsRenderer.cpp test/test_monitoring/test_monitoring.cpp
git commit -m "feat(render): WormsRenderer — worm roll + keyboard from NoteWormModel"
```

---

### Task 5: `MonitoringMode` (Worms screen)

**Files:**
- Create: `core/modes/MonitoringMode.h`, `core/modes/MonitoringMode.cpp`
- Test: `test/test_monitoring/test_monitoring.cpp`

- [ ] **Step 1: Write `core/modes/MonitoringMode.h`**

```cpp
#pragma once

#include "core/app/Mode.h"
#include "core/NoteWormModel.h"

namespace core {

// Visualises incoming MIDI notes. Plan 2: one screen (worms + keyboard).
// The Notes (notation) screen is added in Plan 3.
class MonitoringMode : public Mode {
public:
    MonitoringMode();
    const char* name() const override { return "Monitoring"; }
    int     screenCount() const override { return 1; }
    Screen& screen(int) override { return wormsScreen_; }
    void onMidiIn(const MidiMessage& msg) override;
    void update(uint32_t nowMs) override { model_.tick(nowMs); }

private:
    NoteWormModel model_;

    class WormsScreen : public Screen {
    public:
        explicit WormsScreen(NoteWormModel& m) : model_(m) {}
        const char* name() const override { return "worms"; }
        void render(Display& d) const override;
    private:
        NoteWormModel& model_;
    };
    WormsScreen wormsScreen_{model_};
};

} // namespace core
```

- [ ] **Step 2: Write `core/modes/MonitoringMode.cpp`**

```cpp
#include "core/modes/MonitoringMode.h"

#include "core/Display.h"
#include "core/render/WormsRenderer.h"

namespace core {

MonitoringMode::MonitoringMode() = default;

void MonitoringMode::onMidiIn(const MidiMessage& msg) {
    // Channel filtering is a future Settings concern; accept all channels
    // (OMNI) for now. NoteOn with velocity 0 == NoteOff.
    if (msg.type == MidiType::NoteOn && msg.data2 > 0) {
        model_.onNoteOn(msg.channel, msg.data1);
    } else if (msg.type == MidiType::NoteOff ||
               (msg.type == MidiType::NoteOn && msg.data2 == 0)) {
        model_.onNoteOff(msg.channel, msg.data1);
    } else if (msg.type == MidiType::ControlChange &&
               (msg.data1 == 120 || msg.data1 == 123)) {
        model_.releaseAllOnChannel(msg.channel);
    }
}

void MonitoringMode::WormsScreen::render(Display& d) const {
    WormsRenderer::render(model_, d);
}

} // namespace core
```

Note: confirm the exact `MidiType` enumerator names and `MidiMessage` fields (`type`, `channel`, `data1`, `data2`) against `core/MidiMessage.h`; adjust the conditions to match the real API (e.g. if there is an `isNoteOn()` helper, prefer it).

- [ ] **Step 3: Add an integration test** to `test/test_monitoring/test_monitoring.cpp` (uses `AppShell`; add `#include "core/app/AppShell.h"` and `#include "core/modes/MonitoringMode.h"`):

```cpp
static void test_monitoring_mode_renders_injected_note() {
    core::AppShell shell;
    core::MonitoringMode mon;
    shell.addMode(&mon);
    shell.begin();
    core::MidiMessage on{};
    on.type = core::MidiType::NoteOn; on.channel = 1; on.data1 = 60; on.data2 = 100;
    shell.onMidiIn(on);
    shell.tick(200);
    StubDisplay d;
    shell.render(d);
    TEST_ASSERT_TRUE(d.rects > 0);                 // worms + keyboard drawn
    TEST_ASSERT_TRUE(d.drewText("Monitoring"));    // top bar
}
```

(Adapt the `MidiMessage` construction to the real API discovered in Step 2.)

- [ ] **Step 4: Run** `pio test -e test` (whole suite) → PASS.
- [ ] **Step 5: Commit**
```bash
git add core/modes/MonitoringMode.h core/modes/MonitoringMode.cpp test/test_monitoring/test_monitoring.cpp
git commit -m "feat(modes): MonitoringMode with worms screen"
```

---

### Task 6: Register MonitoringMode in both platform mains

**Files:**
- Modify: `platform/teensy/main.cpp`, `platform/host/main.cpp`

Mode order (per spec): Monitoring should be the **first** registered mode (index 0), before BPM and Debug.

- [ ] **Step 1: teensy main** — add `#include "core/modes/MonitoringMode.h"`, declare `static core::MonitoringMode monitoringMode;`, and register it FIRST in `setup()` (before `bpmMode`/`debugMode`):
```cpp
    app.addMode(&monitoringMode);
    app.addMode(&bpmMode);
    app.addMode(&debugMode);
```
- [ ] **Step 2: Build** `pio run -e teensy41` → SUCCESS.
- [ ] **Step 3: host main** — same: `#include "core/modes/MonitoringMode.h"`, a `core::MonitoringMode monitoringMode;` local (NOT static — match the lifetime of `app`/the other modes), and register it first. Update the startup help text to mention Monitoring is the default mode.
- [ ] **Step 4: Build** `pio run -e native` → SUCCESS. **Do NOT run the simulator** (the SDL window blocks; the controller does visual verification).
- [ ] **Step 5: Run** `pio test -e test` → all suites PASS.
- [ ] **Step 6: Commit**
```bash
git add platform/teensy/main.cpp platform/host/main.cpp
git commit -m "feat(app): register MonitoringMode as the default mode on both platforms"
```

- [ ] **Step 7 (controller / human): visual verification** in the simulator (`make sim`): boots into **Monitoring / worms**; injecting notes (`z x c v b n m`, with `Shift+1..9` choosing the inject channel) shows per-channel coloured worms scrolling up from the keyboard; held keys light up; releasing a note lets its worm scroll away and expire. Enc5 click → overlay now lists Monitoring / BPM / Debug.

---

## Self-review notes

- **Spec coverage:** §6 shared `NoteState` → `NoteWormModel` (Task 3); §7.1 Monitoring worms → Tasks 4–6; §9 "extract worms renderer / keyboard / channel colours" → Tasks 1,2,4. The **Notes screen / NotationRenderer (§7.1)** is deferred to Plan 3 — `MonitoringMode::screenCount()` returns 1 now and grows to 2 in Plan 3.
- **Reuse-ready:** `NoteWormModel` keeps `onEngineNoteOn/Off` + `outNotePressedBy_` so Plan 3 (Arp) can feed OUTGOING notes through the same model + renderer, per the spec's source-parameterised design.
- **Deferred (correct for Plan 2):** notation, held-note chord-name overlay (`detectChordOnChannel`), the chord-queue strip (engine-specific → Arp), and MIDI-in channel filtering (Settings, Plan 4 — Monitoring is OMNI for now).
- **Placeholder discipline:** the `/* ported verbatim */` markers in Tasks 1–2 are explicit instructions to paste real code from the named source lines; the implementer must replace them with the actual ported bodies (they are not allowed to remain).
- **Type consistency:** `NoteWormModel` accessor names (`worms()`, `maxWorms()`, `pressedChannelFor`, `outPressedChannelFor`, `tick`, `onNoteOn/Off`, `onEngineNoteOn/Off`) are used identically across Tasks 3–5. `WormsRenderer::{drawWorms,drawKeyboard,render}` signatures match between Task 4 and Task 5. Verify `MidiMessage`/`MidiType` field names against `core/MidiMessage.h` during Task 5.
