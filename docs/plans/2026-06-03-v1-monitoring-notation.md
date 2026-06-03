# MIDIops v1 — Plan 3: Monitoring Notes screen (notation) extraction

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extract the grand-staff notation rendering out of the legacy `MidiMonitorApp` into a reusable `NotationRenderer` (fed by the existing `NoteWormModel`), and add it as the **Notes** screen of `MonitoringMode`, completing Monitoring's two screens (worms + notes).

**Architecture:** A `NotationRenderer` reads the shared `NoteWormModel` (note-heads scroll from each worm's `startMs`; held-note names come from `pressedChannelFor`). The held-note name-drift animation state (`NameDisplay[]`) moves into the renderer and the model-mutation that the legacy code did inside a `const` draw call is split into an explicit `update(model, nowMs)` (mutates) + `render(model, d) const` (draws) — fixing the original `mutable`-in-`const` hazard. The glyph blitter is extracted to a shared header. `MonitoringMode` grows a second screen.

**Tech Stack:** C++17, PlatformIO (envs `teensy41`, `native`, `test`), Unity, SDL2/RtMidi (sim), ILI9341_t3n (firmware).

This is **Plan 3 of the series** (`docs/specs/2026-06-02-v1-mode-architecture-design.md`), completing the Monitoring mode (§7.1). It builds directly on Plan 2's `NoteWormModel`, `KeyLayout.h`, `Color.h`, `WormsRenderer`, and `MonitoringMode`.

Source of truth for ported pixel code: `core/MidiMonitorApp.cpp` `drawNotation` (lines **1190–1500**) and `drawGlyph` (lines **107–131**), the `NameDisplay` struct (`MidiMonitorApp.h:225–233`), and `core/notation_glyphs.h`. "Port verbatim" = reproduce the exact drawing math, applying only the listed substitutions.

---

## File structure (Plan 3)

- Create `core/render/Glyph.h` — `inline void drawGlyph(...)` (the 1-bit-per-pixel row blitter), extracted from the `MidiMonitorApp.cpp` anonymous namespace.
- Create `core/render/NotationRenderer.h` / `core/render/NotationRenderer.cpp` — the grand-staff renderer with `update()`/`render()` and the `NameDisplay[]` animation state.
- Modify `core/modes/MonitoringMode.h` / `.cpp` — add a second `Screen` ("notes") wired to a `NotationRenderer`; `screenCount()` → 2.
- Test: `test/test_monitoring/test_monitoring.cpp` — extend the existing suite.
- No platform-main changes (MonitoringMode is already registered; the new screen is reachable via Enc5 screen-switch).

Conventions: 2-space indent, `core::` namespace, RGB565, English identifiers/comments, no platform headers in `core/`, no exceptions in `core/`.

---

### Task 1: Extract the glyph blitter → `core/render/Glyph.h`

**Files:**
- Create: `core/render/Glyph.h`
- Test: `test/test_monitoring/test_monitoring.cpp`
- Source: `core/MidiMonitorApp.cpp:107–131` (`drawGlyph`).

The source signature is:
```cpp
void drawGlyph(Display& d, int x, int y,
               const uint16_t* rows, int width, int height, uint16_t color);
```
It blits a bitmap where each `rows[r]` holds `width` bits (MSB = leftmost pixel), drawing horizontal runs of set bits via `fillRect`.

- [ ] **Step 1: Write `core/render/Glyph.h`** — header-only `namespace core`, `#include "core/Display.h"` and `<cstdint>`, with `inline void drawGlyph(Display& d, int x, int y, const uint16_t* rows, int width, int height, uint16_t color)` ported **verbatim** from the source body (lines 110–130).

- [ ] **Step 2: Add a glyph test** to `test/test_monitoring/test_monitoring.cpp` (`#include "core/render/Glyph.h"`; `StubDisplay` is already included; register in `main()`):

```cpp
static void test_glyph_draws_runs_of_set_bits() {
    // 3x2 glyph: row0 = 0b101 (two single pixels), row1 = 0b111 (one run of 3).
    const uint16_t rows[2] = {0b101, 0b111};
    StubDisplay d;
    core::drawGlyph(d, 0, 0, rows, 3, 2, core::color::White);
    // row0 -> 2 fillRects (two isolated bits), row1 -> 1 fillRect (one run).
    TEST_ASSERT_EQUAL_INT(3, d.rects);
}
```

- [ ] **Step 3: Run** `pio test -e test -f test_monitoring` → PASS.
- [ ] **Step 4: Commit**
```bash
git add core/render/Glyph.h test/test_monitoring/test_monitoring.cpp
git commit -m "feat(render): extract drawGlyph blitter to Glyph.h"
```

---

### Task 2: `NotationRenderer` — grand staff + note-heads + name drift

**Files:**
- Create: `core/render/NotationRenderer.h`, `core/render/NotationRenderer.cpp`
- Test: `test/test_monitoring/test_monitoring.cpp`
- Source: `core/MidiMonitorApp.cpp:1190–1500` (`drawNotation`), `NameDisplay` (`MidiMonitorApp.h:225–233`), `core/notation_glyphs.h`.

**The key transformation:** `drawNotation` currently does three things inside one `const` method: (a) collects the set of held notes and reconciles them against the `mutable nameDisplays_[]` slots (allocating new slots, stamping `releasedMs` on release, expiring old slots) — this is STATE MUTATION; (b) draws the staff + clefs + scrolling note-heads from `worms_`; (c) draws the held/drifting note names from `nameDisplays_`. Split (a) into `update()`, keep (b)+(c) in a `const` `render()`.

- [ ] **Step 1: Write `core/render/NotationRenderer.h`**

```cpp
#pragma once

#include <cstdint>

namespace core {

class Display;
class NoteWormModel;

// Grand-staff notation view for a NoteWormModel: note-heads scroll left from
// each worm's startMs; held-note names appear below the staff and drift
// down + fade after release. Owns the name-drift animation state; call
// update() once per frame (mutates) before render() (const, draws).
class NotationRenderer {
public:
    void update(const NoteWormModel& model, uint32_t nowMs);
    void render(const NoteWormModel& model, Display& d) const;

private:
    struct NameDisplay {
        bool     live       = false;
        uint8_t  note       = 0;
        uint8_t  channel    = 0;
        int16_t  x          = 0;
        uint32_t releasedMs = 0;   // 0 = still held
    };
    static constexpr int kMaxNameDisplays = 32;
    NameDisplay nameDisplays_[kMaxNameDisplays] = {};
    uint32_t    nowMs_ = 0;
};

} // namespace core
```

- [ ] **Step 2: Write `core/render/NotationRenderer.cpp`** — port `drawNotation` (1190–1500), split as follows. Includes: `core/render/NotationRenderer.h`, `core/render/Glyph.h`, `core/render/Color.h`, `core/render/KeyLayout.h`, `core/NoteWormModel.h`, `core/Display.h`, `core/MidiMessage.h` (for `MidiMessage::noteName()`), `core/notation_glyphs.h`.

  **`update(model, nowMs)`** — port the name-slot management (the `Held held[]` collection loop ~1409–1431 and the `nameDisplays_` reconciliation ~1433–1470). Substitutions:
  - the held-note source: instead of reading `notePressedBy_` directly, enumerate notes `0..127` and treat note `n` as held iff `model.pressedChannelFor(n) != 0`, with channel = `model.pressedChannelFor(n)`.
  - store `nowMs` into `nowMs_`.
  - the per-slot release/expire/allocate logic, the `x` anchoring at release, the `kFallPxPerS`/`kFadeMs` constants — port verbatim (they govern the drift, computed in `render` from `releasedMs` + `nowMs_`). Releases are detected here (a slot whose note is no longer held and `releasedMs==0` gets `releasedMs = nowMs_`).

  **`render(model, d) const`** — port the drawing (staff lines, clef glyphs, note-heads, names). Substitutions:
  - `worms_` / `kMaxWorms` → `model.worms()` / `model.maxWorms()`; note-head X uses `now = model.lastTickMs()` and `w.startMs` exactly as the source used `lastTickMs_`.
  - `drawGlyph(...)` → `core::drawGlyph(...)` (from `Glyph.h`); glyph data + dims from `core::notation::kTrebleClef` etc. (unchanged).
  - `channelColor` / `scaleRgb565` → `core::channelColor` / `core::scaleRgb565`.
  - `MidiMessage::noteName(note)` for the name strings (unchanged API — confirm the real signature in `core/MidiMessage.h`).
  - the CH label that the source drew from `channel_`: **drop it** (Monitoring has no channel filter and the AppShell top bar already shows the mode/screen). Remove only that label; keep everything else.
  - all staff layout constants (`kStepH`, `kStaffX`, `kF5Y`, … `kNamesY`, `kRightX`, the local `kScrollPxPerSec=40`, `kPcToNatural[]`, `kPcIsSharp[]`, `stepY`, etc.) → keep as local `constexpr`/statics inside the `.cpp`, identical values.
  - names are drawn from `nameDisplays_` (now a renderer member), read-only in `render`.

  Keep the note-head scroll math, stem direction, ledger lines, sharps, and the name drift/fade identical to the source.

- [ ] **Step 3: Add renderer tests** to `test/test_monitoring/test_monitoring.cpp` (`#include "core/render/NotationRenderer.h"`; register in `main()`):

```cpp
static void test_notation_draws_staff() {
    core::NoteWormModel m; m.tick(0);
    core::NotationRenderer n;
    n.update(m, 0);
    StubDisplay d;
    n.render(m, d);
    TEST_ASSERT_TRUE(d.rects > 0);   // staff lines + clefs
}

static void test_notation_shows_held_note_name() {
    core::NoteWormModel m; m.tick(0);
    m.onNoteOn(1, 60);               // C4
    core::NotationRenderer n;
    n.update(m, 0);
    StubDisplay d;
    n.render(m, d);
    TEST_ASSERT_TRUE(d.drewText("C"));   // a C-something name below the staff
}

static void test_notation_update_is_safe_without_render() {
    core::NoteWormModel m; m.tick(0);
    core::NotationRenderer n;
    m.onNoteOn(1, 64);
    n.update(m, 0);
    m.onNoteOff(1, 64);
    for (uint32_t t = 100; t <= 3000; t += 100) { m.tick(t); n.update(m, t); }
    StubDisplay d; n.render(m, d);      // must not crash; faded name may be gone
    TEST_ASSERT_TRUE(true);
}
```

(If `MidiMessage::noteName` returns a `std::string` whose sharp/format differs, adjust the `"C"` assertion to a substring that the real `noteName(60)` produces.)

- [ ] **Step 4: Run** `pio test -e test -f test_monitoring` → PASS.
- [ ] **Step 5: Commit**
```bash
git add core/render/NotationRenderer.h core/render/NotationRenderer.cpp test/test_monitoring/test_monitoring.cpp
git commit -m "feat(render): NotationRenderer — grand staff + note-heads + name drift"
```

---

### Task 3: Add the Notes screen to `MonitoringMode`

**Files:**
- Modify: `core/modes/MonitoringMode.h`, `core/modes/MonitoringMode.cpp`
- Test: `test/test_monitoring/test_monitoring.cpp`

- [ ] **Step 1: Update `core/modes/MonitoringMode.h`** — add a second screen and a `NotationRenderer`. `screenCount()` returns 2; `screen(i)` returns `wormsScreen_` for 0, `notesScreen_` for 1 (or non-0). Add `#include "core/render/NotationRenderer.h"`. The NotesScreen holds a reference to the shared `model_` and owns a `NotationRenderer`; its `update(nowMs)` advances the notation animation, its `render(d)` draws it:

```cpp
    class NotesScreen : public Screen {
    public:
        explicit NotesScreen(NoteWormModel& m) : model_(m) {}
        const char* name() const override { return "notes"; }
        void update(uint32_t nowMs) override { notation_.update(model_, nowMs); }
        void render(Display& d) const override { notation_.render(model_, d); }
    private:
        NoteWormModel&   model_;
        NotationRenderer notation_;
    };
    NotesScreen notesScreen_{model_};
```
and:
```cpp
    int     screenCount() const override { return 2; }
    Screen& screen(int i) override { return i == 0 ? wormsScreen_ : notesScreen_; }
```
Note: `MonitoringMode::update(nowMs)` already ticks `model_`. The shell calls the ACTIVE screen's `update()` too, so `NotesScreen::update` advances notation only while the notes screen is shown (matches the legacy behaviour where notation state advanced only in the notation view).

- [ ] **Step 2: `core/modes/MonitoringMode.cpp`** — no functional change needed beyond what the header inlines; if `screen(int)` was previously defined inline returning only `wormsScreen_`, update it. Ensure the `#include` for `NotationRenderer.h` is present (header) and the file still compiles.

- [ ] **Step 3: Add an integration test** to `test/test_monitoring/test_monitoring.cpp`:

```cpp
static void test_monitoring_notes_screen_renders() {
    core::AppShell shell;
    core::MonitoringMode mon;
    shell.addMode(&mon);
    shell.begin();
    TEST_ASSERT_EQUAL_INT(2, mon.screenCount());
    shell.onEncoderKnob(5, +1);          // switch to the notes screen
    TEST_ASSERT_EQUAL_INT(1, shell.activeScreenIndex());
    core::MidiMessage on{};
    on.type = core::MidiType::NoteOn; on.channel = 1; on.data1 = 60; on.data2 = 100;
    shell.onMidiIn(on);
    shell.tick(50);
    StubDisplay d;
    shell.render(d);
    TEST_ASSERT_TRUE(d.rects > 0);                 // staff drawn
    TEST_ASSERT_TRUE(d.drewText("notes"));         // top bar screen name
}
```

- [ ] **Step 4: Run the full suite** `pio test -e test` → PASS.
- [ ] **Step 5: Build both targets** `pio run -e native` and `pio run -e teensy41` → SUCCESS. (Do NOT run the simulator — the controller does the visual check.)
- [ ] **Step 6: Commit**
```bash
git add core/modes/MonitoringMode.h core/modes/MonitoringMode.cpp test/test_monitoring/test_monitoring.cpp
git commit -m "feat(modes): add Notes (notation) screen to MonitoringMode"
```

- [ ] **Step 7 (controller / human): visual verification** (`make sim`): in Monitoring, rotate Enc5 (`q`/`e`) to the **notes** screen; injected notes (`z x c v b n m`) appear as note-heads on the grand staff scrolling left, with held-note names below that drift down + fade after release. Top bar reads `Monitoring - notes`. Compare against the legacy notation view for fidelity.

---

## Self-review notes

- **Spec coverage:** completes §7.1 Monitoring (worms + notes). NotationRenderer is fed by the shared `NoteWormModel` per §6 (source-parameterised), so a future Arp notes screen can reuse it for outgoing notes.
- **Hazard fixed:** the legacy `mutable nameDisplays_` mutation inside a `const` draw is replaced by an explicit `update()`/`render()` split. `render()` is genuinely const.
- **Deferred (correct):** the legacy notation CH label (no channel filter in Monitoring yet — Settings, Plan 4). The chord-name header overlay and chord-queue strip remain out of scope (engine-specific → Arp).
- **Type consistency:** `NotationRenderer::{update,render}` signatures are used identically in Tasks 2–3; `core::drawGlyph` signature matches the Task-1 header and the source. `NoteWormModel` accessors used: `worms()`, `maxWorms()`, `pressedChannelFor()`, `lastTickMs()` — all already public from Plan 2. Confirm `MidiMessage::noteName` signature in Task 2.
- **No main changes:** MonitoringMode is already registered; the second screen is reachable via the existing Enc5 screen-switch, so no platform-main edits are needed.
