# Berlin MIDI Transposition Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Berlin transposes its whole three-voice stack diatonically (in scale degrees, quantized to the global scale) in response to notes arriving on the global MIDI-in channel — a latched "change the key centre" performance control.

**Architecture:** `AppShell` already filters MIDI-in by channel and routes notes to the active mode; Berlin gains an `onMidiIn` override. A new `Scale::degreeIndex` turns an incoming note into a signed scale-degree distance from a reference tonic (root in the octave at MIDI 60). The amount is pushed to all three `BerlinEngine`s as a `transposeDegrees_` offset applied non-destructively at note emit (the stored sequence stays "home"); the piano-roll renders transposed copies so the visualization moves with the audio.

**Tech Stack:** portable C++17 `core/` (no platform headers, no exceptions), PlatformIO + Unity (`pio test -e test`), builds `pio run -e native` / `pio run -e teensy41`.

**Spec:** `docs/specs/2026-06-13-berlin-midi-transpose-design.md`.

**File structure:**
- `core/Scale.{h,cpp}` — add `degreeIndex(note)` (Task 1).
- `core/BerlinEngine.{h,cpp}` — `transposeDegrees_` + setter/getter + transpose at emit (Task 2).
- `core/modes/BerlinMode.{h,cpp}` — `onMidiIn` sets the offset on all voices; `onEnter` resets it; `renderRoll` shows it (Tasks 3, 4).
- `MANUAL.md` / `MANUAL.cs.md` / `CLAUDE.md` — docs (Task 5).

---

### Task 1: `Scale::degreeIndex` — signed scale-degree index of a note

**Files:**
- Modify: `core/Scale.h` (declaration, after `degreeNote`)
- Modify: `core/Scale.cpp` (implementation, after `degreeNote`)
- Test: `test/test_berlin_generator/test_berlin_generator.cpp` (Scale tests live here)

- [ ] **Step 1: Write the failing test** (append + register `RUN_TEST(test_scale_degree_index);` in `main()`)

```cpp
// degreeIndex: +1 per scale step, +degreeCount() per octave, signed across
// the root; consistent with degreeNote (its inverse over a degree delta).
static void test_scale_degree_index() {
    core::Scale cmaj(core::Scale::Type::Major, 0);   // C major, 7 notes
    const int n = cmaj.degreeCount();                // 7
    // One scale step up (C->D = 60->62) is +1.
    TEST_ASSERT_EQUAL_INT(1, cmaj.degreeIndex(62) - cmaj.degreeIndex(60));
    // One octave up (60->72) is +degreeCount.
    TEST_ASSERT_EQUAL_INT(n, cmaj.degreeIndex(72) - cmaj.degreeIndex(60));
    // One octave down is -degreeCount.
    TEST_ASSERT_EQUAL_INT(-n, cmaj.degreeIndex(48) - cmaj.degreeIndex(60));
    // Round-trip: stepping degreeNote by the index delta lands on the note.
    const int d = cmaj.degreeIndex(67) - cmaj.degreeIndex(60);   // C->G = +4
    TEST_ASSERT_EQUAL_INT(67, cmaj.degreeNote(60, d));
    // Non-C root stays consistent (A minor: A->B is +1).
    core::Scale amin(core::Scale::Type::Minor, 9);   // root A (pc 9)
    TEST_ASSERT_EQUAL_INT(1, amin.degreeIndex(71) - amin.degreeIndex(69));
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `pio test -e test -f test_berlin_generator 2>&1 | tail -5`
Expected: compile error — `no member named 'degreeIndex' in 'core::Scale'`.

- [ ] **Step 3: Declare in `core/Scale.h`** (immediately after the `degreeNote` declaration):

```cpp
    // Absolute scale-degree index of `note` (signed), including octaves:
    // octave * degreeCount() + position-in-octave, after quantizing to scale.
    // Differences between two indices give the signed scale-degree distance.
    int degreeIndex(uint8_t note) const;
```

- [ ] **Step 4: Implement in `core/Scale.cpp`** (after `degreeNote`, before `degreeCount`):

```cpp
int Scale::degreeIndex(uint8_t note) const {
    const uint8_t* ivs = nullptr;
    int len = intervals(&ivs);
    uint8_t qnote = quantize(note);
    uint8_t pc = static_cast<uint8_t>((static_cast<int>(qnote) - root_ + 120) % 12);
    int idx = 0;
    for (int i = 0; i < len; ++i) {
        if (ivs[i] == pc) { idx = i; break; }
    }
    // base_note = root of qnote's octave; (base_note - root_) is a multiple of 12.
    int base_note = static_cast<int>(qnote) - static_cast<int>(ivs[idx]);
    int oct = (base_note - static_cast<int>(root_)) / 12;
    return oct * len + idx;
}
```

- [ ] **Step 5: Run to verify it passes**

Run: `pio test -e test -f test_berlin_generator 2>&1 | tail -3`
Expected: PASSED. Then the full suite + Teensy build (the header compiles into firmware):
`pio test -e test 2>&1 | tail -3 && pio run -e teensy41 2>&1 | tail -1` → all PASSED, SUCCESS.

- [ ] **Step 6: Commit**

```bash
git add core/Scale.h core/Scale.cpp test/test_berlin_generator/test_berlin_generator.cpp
git commit -m "feat(scale): degreeIndex - signed scale-degree index of a note"
```

---

### Task 2: `BerlinEngine` transpose offset applied at emit

**Files:**
- Modify: `core/BerlinEngine.h` (setter/getter + member)
- Modify: `core/BerlinEngine.cpp` (`emitStep` transposes the emitted note + arms the gate with it)
- Test: `test/test_berlin_engine/test_berlin_engine.cpp`

- [ ] **Step 1: Write the failing test** (append + register `RUN_TEST(test_engine_transpose_shifts_emitted_note);`)

```cpp
// setTransposeDegrees(n) shifts every emitted note by n scale degrees; the
// matching NoteOff targets the SAME transposed note; offset 0 is home.
static void test_engine_transpose_shifts_emitted_note() {
    core::Scale scale(core::Scale::Type::Major, 0);   // C major
    core::DrunkardWalkGenerator gen;
    FakeMidiOutput out;
    core::BerlinEngine e;
    core::BerlinParams p; p.density = 100;             // every step active
    e.setOutput(&out); e.setScale(&scale); e.setGenerator(&gen);
    e.setParams(p); e.seed(1); e.generate();
    TEST_ASSERT_EQUAL_INT(0, e.transposeDegrees());

    // Home: capture step 0's emitted note.
    e.play();
    const uint8_t homeNote = out.events.front().note;
    e.stop();

    // +1 scale degree.
    out.events.clear();
    e.setTransposeDegrees(1);
    TEST_ASSERT_EQUAL_INT(1, e.transposeDegrees());
    e.play();
    const uint8_t shifted = out.events.front().note;
    TEST_ASSERT_EQUAL_INT(scale.degreeNote(homeNote, 1), shifted);

    // The matching NoteOff (on stop) targets the same transposed note.
    out.events.clear();
    e.stop();
    TEST_ASSERT_FALSE(out.events.empty());
    TEST_ASSERT_FALSE(out.events.back().isOn);
    TEST_ASSERT_EQUAL_INT(shifted, out.events.back().note);

    // Back to home.
    out.events.clear();
    e.setTransposeDegrees(0);
    e.play();
    TEST_ASSERT_EQUAL_INT(homeNote, out.events.front().note);
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `pio test -e test -f test_berlin_engine 2>&1 | tail -5`
Expected: compile error — `no member named 'setTransposeDegrees'`.

- [ ] **Step 3: Add the API to `core/BerlinEngine.h`.** After the `setMuted`/`muted` block:

```cpp
    // Diatonic transpose applied at emit (BerlinMode drives this from MIDI-in).
    // The stored sequence stays untransposed ("home"); the offset shifts the
    // emitted pitch by N scale degrees via the scale. 0 = home.
    void setTransposeDegrees(int d) { transposeDegrees_ = d; }
    int  transposeDegrees() const { return transposeDegrees_; }
```

and add the member next to `muted_`:

```cpp
    int   transposeDegrees_ = 0;
```

- [ ] **Step 4: Transpose at emit in `core/BerlinEngine.cpp`.** Replace the body of `emitStep` after the `if (muted_) return;` line:

```cpp
    if (muted_) return;   // suppressed: nothing sounds, so no gate to arm
    // Diatonic transpose (live, non-destructive): the stored note is "home",
    // the emitted pitch is shifted by transposeDegrees_ scale steps. The gate
    // is armed with the SAME transposed note so its NoteOff matches even if the
    // offset changes mid-gate.
    const uint8_t outNote = (scale_ && transposeDegrees_ != 0)
                                ? scale_->degreeNote(s.note, transposeDegrees_)
                                : s.note;
    emit(true, outNote, s.velocity);
    const int gateTicks = stepLenTicks() * params_.gatePercent / 100;
    gate_.arm(outNote, gateTicks);   // arm() clamps to >= 1
```

(Leave the existing comment block about live gate derivation directly above the `gateTicks` line if present; the key change is using `outNote` for both `emit` and `gate_.arm`.)

- [ ] **Step 5: Run to verify it passes**

Run: `pio test -e test -f test_berlin_engine 2>&1 | tail -3`
Expected: PASSED. Then full suite + Teensy:
`pio test -e test 2>&1 | tail -3 && pio run -e teensy41 2>&1 | tail -1` → all PASSED, SUCCESS.

- [ ] **Step 6: Commit**

```bash
git add core/BerlinEngine.h core/BerlinEngine.cpp test/test_berlin_engine/test_berlin_engine.cpp
git commit -m "feat(berlin): engine transpose offset applied at emit (non-destructive)"
```

---

### Task 3: `BerlinMode::onMidiIn` drives the transpose on all voices

**Files:**
- Modify: `core/modes/BerlinMode.h` (`onMidiIn` override + `transposeDegrees_` member)
- Modify: `core/modes/BerlinMode.cpp` (`onMidiIn` impl; reset in `onEnter`)
- Test: `test/test_berlin_mode/test_berlin_mode.cpp`

- [ ] **Step 1: Write the failing test** (append + register `RUN_TEST(test_midi_in_transposes_all_voices);`)

```cpp
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

    // A NoteOn with velocity 0 is also a note-off → ignored.
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

    // Transpose, then re-enter the mode → reset to home.
    berlin.onMidiIn(on);
    TEST_ASSERT_EQUAL_INT(1, berlin.engine(core::BerlinMode::kHigh).transposeDegrees());
    berlin.onEnter();
    TEST_ASSERT_EQUAL_INT(0, berlin.engine(core::BerlinMode::kHigh).transposeDegrees());
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `pio test -e test -f test_berlin_mode 2>&1 | tail -5`
Expected: FAIL — `onMidiIn` is the base no-op, so `transposeDegrees()` stays 0.

- [ ] **Step 3: Declare in `core/modes/BerlinMode.h`.** Add the override next to the other `Mode` overrides (e.g. after `onClockTick`):

```cpp
    // Incoming notes on the global MIDI-in channel transpose the whole stack
    // diatonically (latched). See docs/specs/2026-06-13-berlin-midi-transpose-design.md.
    void onMidiIn(const MidiMessage& msg) override;
```

and add the authoritative offset to the private members (next to `editVoice_`):

```cpp
    int                   transposeDegrees_ = 0;
```

- [ ] **Step 4: Implement in `core/modes/BerlinMode.cpp`.** Add `onMidiIn` (e.g. just after `onTransport`):

```cpp
void BerlinMode::onMidiIn(const MidiMessage& msg) {
    // Diatonic transposition control (latched): a NoteOn sets the new key
    // centre; a NoteOff — or a NoteOn with velocity 0 — is ignored, so the
    // last note's transposition persists. The note is a silent control input
    // (never echoed out). "Home" is the scale root in the octave at MIDI 60.
    if (msg.type != MidiType::NoteOn || msg.data2 == 0) return;
    const Scale& sc = svc_.scale();
    const int r0 = 60 + static_cast<int>(sc.root());
    transposeDegrees_ = sc.degreeIndex(msg.data1) -
                        sc.degreeIndex(static_cast<uint8_t>(r0));
    for (int v = 0; v < kVoices; ++v)
        voices_[v].engine.setTransposeDegrees(transposeDegrees_);
}
```

In `onEnter`, reset the offset. Inside the existing per-voice loop add a `setTransposeDegrees(0)`, and clear the mode's copy. The loop currently reads:

```cpp
    for (int v = 0; v < kVoices; ++v) {
        Voice& vc = voices_[v];
        vc.engine.setScale(&scale_);
        vc.engine.setParams(vc.params);
        vc.engine.setOutChannel(vc.channel);
        applyGenerator(v);
        if (!vc.engine.sequence().step(0).active) vc.engine.generate();
    }
```

Add `vc.engine.setTransposeDegrees(0);` inside the loop (e.g. right after `setOutChannel`), and add `transposeDegrees_ = 0;` just before the loop.

- [ ] **Step 5: Run to verify it passes**

Run: `pio test -e test -f test_berlin_mode 2>&1 | tail -3`
Expected: PASSED. Then full suite + both builds:
`pio test -e test 2>&1 | tail -3 && pio run -e native 2>&1 | tail -1 && pio run -e teensy41 2>&1 | tail -1` → all PASSED, both SUCCESS.

- [ ] **Step 6: Commit**

```bash
git add core/modes/BerlinMode.h core/modes/BerlinMode.cpp test/test_berlin_mode/test_berlin_mode.cpp
git commit -m "feat(berlin): MIDI-in note transposes the whole stack diatonically (latched)"
```

---

### Task 4: piano-roll reflects the transpose

**Files:**
- Modify: `core/modes/BerlinMode.cpp` (`renderRoll` builds transposed display copies)
- Test: `test/test_berlin_mode/test_berlin_mode.cpp`

- [ ] **Step 1: Write the failing test** (append + register `RUN_TEST(test_roll_reflects_transpose);`)

```cpp
// The roll moves with the transpose: rendering the same screen before and
// after a one-octave transpose produces different note rectangles.
static void test_roll_reflects_transpose() {
    core::AppShell shell;
    core::BerlinMode berlin(shell);
    berlin.onEnter();
    StubDisplay before;
    berlin.screen(0).render(before);

    core::MidiMessage up{}; up.type = core::MidiType::NoteOn;
    up.channel = 1; up.data1 = 72; up.data2 = 100;   // C5 = +1 octave above tonic 60
    berlin.onMidiIn(up);
    StubDisplay after;
    berlin.screen(0).render(after);

    bool differ = before.rects.size() != after.rects.size();
    for (size_t i = 0; !differ && i < before.rects.size(); ++i)
        if (before.rects[i].y != after.rects[i].y) differ = true;
    TEST_ASSERT_TRUE(differ);
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `pio test -e test -f test_berlin_mode 2>&1 | tail -5`
Expected: FAIL — `renderRoll` reads the untransposed sequence, so the two renders are identical (`differ` stays false).

- [ ] **Step 3: Implement.** Replace `BerlinMode::renderRoll` in `core/modes/BerlinMode.cpp`:

```cpp
void BerlinMode::renderRoll(Display& d) const {
    BerlinRollVoice rv[kVoices];
    BerlinSequence  disp[kVoices];   // transposed display copies (home stays put)
    for (int v = 0; v < kVoices; ++v) {
        const BerlinEngine& e = voices_[v].engine;
        const BerlinSequence& src = e.sequence();
        disp[v].setLength(src.length());
        for (int i = 0; i < BerlinSequence::kMaxSteps; ++i) {
            BerlinStep s = src.step(i);
            if (s.active && transposeDegrees_ != 0)
                s.note = scale_.degreeNote(s.note, transposeDegrees_);
            disp[v].step(i) = s;
        }
        rv[v].seq          = &disp[v];
        rv[v].playhead     = e.playhead();
        rv[v].soundingNote = e.soundingNote();   // already transposed (gate armed transposed)
        rv[v].color        = kBerlinVoiceColors[v];
        rv[v].muted        = e.muted();
        rv[v].edited       = (v == editVoice_);
        rv[v].name         = kBerlinVoiceNames[v];
    }
    drawBerlinMultiRoll(d, rv, kVoices);
}
```

(`disp` lives for the whole function, so the `rv[v].seq` pointers stay valid through `drawBerlinMultiRoll`. `scale_` is the mode's scale copy, refreshed each `update()`/`onEnter`.)

- [ ] **Step 4: Run to verify it passes**

Run: `pio test -e test -f test_berlin_mode 2>&1 | tail -3`
Expected: PASSED. Then full suite + both builds:
`pio test -e test 2>&1 | tail -3 && pio run -e native 2>&1 | tail -1 && pio run -e teensy41 2>&1 | tail -1` → all PASSED, both SUCCESS.

- [ ] **Step 5: Sim smoke check**

Build and run the simulator (`pio run -e native`, then `.pio/build/native/program`). In Berlin, with notes injected on the in-channel (sim keys `z x c v b n m` on channel 1), the piano-roll should shift up/down and the audio key centre should move; playing the tonic returns home.

- [ ] **Step 6: Commit**

```bash
git add core/modes/BerlinMode.cpp test/test_berlin_mode/test_berlin_mode.cpp
git commit -m "feat(berlin): piano-roll reflects the live MIDI transpose"
```

---

### Task 5: documentation (manuals EN+CZ, CLAUDE.md)

**Files:**
- Modify: `MANUAL.md` (§5.3 Berlin)
- Modify: `MANUAL.cs.md` (same section, structurally identical)
- Modify: `CLAUDE.md` (Berlin bullet)

- [ ] **Step 1: Add to `MANUAL.md` §5.3.** After the transport/latch description of Berlin, add a paragraph:

```markdown
**MIDI transposition.** While in Berlin, notes arriving on the global **MIDI In
channel** (Settings → MIDI) transpose the whole three-voice stack **diatonically**
— everything stays in the current scale. The transposition is **latched**: the
last note sets the new key centre and it holds until the next note. Playing the
**scale root around middle C** returns home; playing higher/lower shifts the
melody up/down by scale degrees (whole octaves included). The incoming note is a
silent control — it is not sounded. The piano-roll moves with the transposition.
```

- [ ] **Step 2: Add the matching Czech paragraph to `MANUAL.cs.md` §5.3** (same position, faithful translation):

```markdown
**MIDI transpozice.** Když jsi v Berlinu, noty přicházející na globálním **MIDI In
kanálu** (Settings → MIDI) transponují celý tříhlasý stack **diatonicky** — vše
zůstane v aktuální stupnici. Transpozice je **zamčená**: poslední nota nastaví
nové tonální centrum a to drží až do další noty. Zahráním **rootu stupnice kolem
středního C** se vrátíš domů; vyšší/nižší nota posune melodii nahoru/dolů po
stupních stupnice (včetně celých oktáv). Příchozí nota je tichý ovládací vstup —
nezní. Piano-roll se posune spolu s transpozicí.
```

- [ ] **Step 3: Update the Berlin bullet in `CLAUDE.md`** (the "Berlin —" item in the mode list). Append to that bullet:

```markdown
  Incoming notes on the global MIDI-in channel transpose the whole stack
  diatonically (latched; silent control input), via `BerlinMode::onMidiIn` →
  per-engine `setTransposeDegrees` + `Scale::degreeIndex`.
```

- [ ] **Step 4: Verify docs build nothing but stay consistent**

Run: `pio test -e test 2>&1 | tail -3` (unchanged green) and confirm the two manuals stay structurally identical:
`grep -c "^###\|^##\|^\*\*Screen\|^\*\*Obrazovka" MANUAL.md MANUAL.cs.md` (counts should match).

- [ ] **Step 5: Commit**

```bash
git add MANUAL.md MANUAL.cs.md CLAUDE.md
git commit -m "docs: Berlin MIDI transposition (manuals EN+CZ, project notes)"
```
