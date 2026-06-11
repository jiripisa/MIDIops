# Transport mode (Off / Send / Receive) — implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement the approved Transport setting (spec:
`docs/specs/2026-06-11-transport-mode-design.md`): a global
`TransportMode { Off, Send, Receive }` (default Send) edited in
Settings → MIDI Enc4; Send gates all outgoing Start/Continue/Stop and adds
emission from Berlin's local latches; Receive drives playback from incoming
transport per the MIDI standard; the old pass-through forwarding is removed;
an always-on safety silences notes on Stop under an external clock.

**Architecture:** `TransportMode` + `notifyLocalTransport(Transport)` join
`AppServices`; `AppShell::applyTransport` becomes
`notifyLocalTransport(t) + mode notify`, with MIDI emission gated on Send.
The `onMidiIn` realtime branch is restructured: Clock keeps its
external-follow path; Start/Continue/Stop map to
`onTransport(Reset+Play / Play / Pause)` under Receive, are ignored
otherwise except the external-clock Stop safety. `BerlinEngine` gains
`silence()`. All in `core/` (portable C++17, no platform headers).

**Tech Stack:** C++17, PlatformIO (`teensy41` / `native` / `test`), Unity.

Conventions: 2-space indent, `core::` namespace, English, one statement per
line (`-Wmisleading-indentation`). Tests `pio test -e test`
(`-f <suite>` for one). Builds `pio run -e native|teensy41` warning-clean.
Manuals MANUAL.md + MANUAL.cs.md must be updated in the same change
(CLAUDE.md rule).

---

### Task 1: TransportMode setting + Send gating + Berlin emission

**Files:** Modify `core/app/AppServices.h`, `core/app/AppShell.{h,cpp}`,
`core/modes/SettingsMode.{h,cpp}`, `core/modes/BerlinMode.{h,cpp}`;
Tests `test/test_settings/`, `test/test_shell/`, `test/test_berlin_mode/`.

- [ ] **Step 1: `core/app/AppServices.h`** — add next to `ClockSource`:
```cpp
enum class TransportMode : uint8_t { Off = 0, Send, Receive, kCount };
```
and to the interface:
```cpp
    virtual TransportMode transportMode() const = 0;
    virtual void          setTransportMode(TransportMode m) = 0;
    // A capturing mode reports its local transport action. The shell mirrors
    // transportState_ (top bar) and emits MIDI transport when mode == Send.
    // Does NOT call back into the mode's onTransport (the mode already acted).
    virtual void          notifyLocalTransport(Transport t) = 0;
```

- [ ] **Step 2: `core/app/AppShell.h`** — member
`TransportMode transportMode_ = TransportMode::Send;`, inline
`transportMode()/setTransportMode()` overrides, declare
`void notifyLocalTransport(Transport t) override;`.

- [ ] **Step 3: `core/app/AppShell.cpp`** — refactor:
```cpp
void AppShell::notifyLocalTransport(Transport t) {
    const bool send = out_ != nullptr && transportMode_ == TransportMode::Send;
    switch (t) {
        case Transport::Play:
            if (send) {
                if (transportState_ == TransportState::Paused) out_->sendContinue();
                else out_->sendStart();
            }
            transportState_ = TransportState::Playing;
            break;
        case Transport::Pause:
            if (send) out_->sendStop();
            transportState_ = TransportState::Paused;
            break;
        case Transport::Stop:
        case Transport::Reset:
            if (send) out_->sendStop();
            transportState_ = TransportState::Stopped;
            break;
    }
    transport_ = t;
}

void AppShell::applyTransport(Transport t) {
    notifyLocalTransport(t);
    if (modeCount_ > 0) modes_[activeMode_]->onTransport(t);
}
```
(Behaviour under the default Send is identical to today — existing
test_shell transport tests must stay green unchanged.)

- [ ] **Step 4: Settings → MIDI Enc4.** In `core/modes/SettingsMode.cpp`
MidiScreen: `case 4:` cycles `svc_.setTransportMode(cycleEnum(svc_.transportMode(), delta))`
(non-zero delta only, mirroring the clock toggle's guard style); render the
fourth cell `TRNSPT` with a `transportName()` helper returning
`"Off" / "Send" / "Recv"`. Test (test_settings, mirroring the existing
cases): default is Send; Enc4 cycles through all three and wraps.

- [ ] **Step 5: Berlin local latches emit (flip-gated).** In
`core/modes/BerlinMode.cpp` `onRawInput`: keep the engine actions exactly as
they are; ADD `svc_.notifyLocalTransport(...)` calls gated on a REAL flip
(`changed && !firstDelivery`, the existing `flip` logic — Latch1 needs the
same flip flag computed, while its engine action stays level-driven):
  - Latch1 flip to ON → `notifyLocalTransport(Transport::Play)`;
    flip to OFF → `notifyLocalTransport(Transport::Pause)`.
  - Latch2 flip → after `engine_.stop()`, `notifyLocalTransport(Transport::Stop)`.
  - Latch3 → no notify.
  - The first-frame sync adoption (entering the mode with a switch ON) must
    NOT emit — only real flips do.
- [ ] **Step 6: Tests** (test_berlin_mode; FakeMidiOutput already counts
`starts/continues/stops`; mind the latch first-frame absorb — prime with the
opposite level first, as the existing tests do):
  - default (Send): Latch1 OFF→ON → `out.starts == 1`; ON→OFF → `stops == 1`;
    OFF→ON again from paused → `continues == 1`.
  - `shell.setTransportMode(TransportMode::Off)`: the same flips emit nothing.
  - test_shell: global latch Play with mode Off → no Start on the wire
    (state still changes); with Receive → also no emission.
- [ ] **Step 7:** `pio test -e test` ALL PASS; both builds SUCCESS. Commit:
`feat(transport): TransportMode setting (Off/Send/Receive) + Send gating + Berlin latch emission`

---

### Task 2: Receive + safety + forwarding removal

**Files:** Modify `core/app/AppShell.cpp`, `core/BerlinEngine.{h,cpp}`,
`core/modes/BerlinMode.{h,cpp}`, `core/modes/ArpMode.{h,cpp}`;
Tests `test/test_clock/`, `test/test_berlin_mode/`.

- [ ] **Step 1: `BerlinEngine::silence()`** — public helper: if a note is
sounding, emit its NoteOff and clear the gate; do NOT move the playhead or
change `playing_`. (Implemented on the `GateTimer gate_`.) Unit test in
test_berlin_engine: play one step, call `silence()`, assert the NoteOff
arrived and `playhead()` unchanged.

- [ ] **Step 2: `BerlinMode::onTransport`** — extend to:
```cpp
void BerlinMode::onTransport(Transport t) {
    switch (t) {
        case Transport::Play:  engine_.play();  break;
        case Transport::Pause: engine_.silence(); engine_.pause(); break;
        case Transport::Reset:
        case Transport::Stop:  engine_.stop();  break;
    }
}
```
**`ArpMode::onTransport`** — also treat Pause as stop:
`if (t == Transport::Stop || t == Transport::Pause) engine_.stop();`

- [ ] **Step 3: rewrite the realtime branch of `AppShell::onMidiIn`** —
Clock keeps the existing external-follow block (comment included) and
returns; Start/Continue/Stop become:
```cpp
        // Transport (Start/Continue/Stop). Receive drives playback per the
        // MIDI standard and CONSUMES the message (no re-emit — same no-echo
        // rule as the clock). Otherwise transport is ignored, except the
        // external-clock Stop safety below.
        if (transportMode_ == TransportMode::Receive) {
            if (modeCount_ > 0) {
                if (msg.type == MidiType::Start) {
                    modes_[activeMode_]->onTransport(Transport::Reset);   // rewind
                    modes_[activeMode_]->onTransport(Transport::Play);    // from the top
                } else if (msg.type == MidiType::Continue) {
                    modes_[activeMode_]->onTransport(Transport::Play);    // from position
                } else {
                    modes_[activeMode_]->onTransport(Transport::Pause);   // halt, keep position
                }
            }
            transportState_ = (msg.type == MidiType::Stop)
                                  ? TransportState::Paused
                                  : TransportState::Playing;
        } else if (msg.type == MidiType::Stop && clockSource_ == ClockSource::External) {
            // Safety (audit N6): a stopping master stops its clock too, so a
            // tick-scheduled gate-off would never fire. Silence immediately;
            // playback state is otherwise untouched.
            if (modeCount_ > 0) modes_[activeMode_]->onTransport(Transport::Pause);
        }
        return;
```
The old pass-through `out_->sendStart()/sendContinue()/sendStop()` calls in
this branch are DELETED.

- [ ] **Step 4: Tests** (mind latch priming; construct like the existing
test_clock shell+Berlin tests):
  - Receive + Internal clock: incoming Start → Berlin `isPlaying()`,
    `playhead()==0`, a NoteOn emitted (drive a few internal ticks via
    `out.pendingTicks`); incoming Stop mid-sequence → immediate NoteOff,
    `!isPlaying()`, playhead retained (> 0); incoming Continue → resumes
    from that playhead (no rewind).
  - Send (default): incoming Start is ignored (Berlin stays stopped); no
    transport re-emitted (`out.starts+continues+stops` unchanged).
  - Safety: mode Off + External clock + sounding note + incoming Stop →
    NoteOff arrives.
  - UPDATE `test_external_stop_silences_engine` (test_clock): it was written
    for the old always-forward behaviour — under the new model (default
    Send + External) the SAFETY branch still silences, so keep its
    assertions about NoteOff/`!isPlaying()` but drop/adjust anything
    asserting the forwarded Stop. Read it and adjust minimally.
- [ ] **Step 5:** `pio test -e test` ALL PASS; both builds SUCCESS. Commit:
`feat(transport): Receive drives playback (Start/Continue/Stop), external-Stop safety, forwarding removed`

---

### Task 3: Manuals

**Files:** `MANUAL.md`, `MANUAL.cs.md` (keep structurally identical).

- [ ] §4 Global concepts: add a **Transport** bullet (Off/Send/Recv,
default Send, independent of Clock source).
- [ ] §5.5 Settings MIDI table: add the Enc4 row
(`Transport | Off, Send, Recv | Send | Send = emit Start/Continue/Stop;
Recv = follow incoming transport; Off = neither`).
- [ ] §5.1 Monitoring: the sentence "which sends MIDI Start/Stop/Continue
when the clock source is Internal" → "when Transport is set to Send".
- [ ] §5.3 Berlin transport table: note that under Send, Latch1/Latch2 also
emit MIDI Start/Continue/Stop so a DAW can follow.
- [ ] §6 Clock: rewrite the external-Stop sentence: under Receive the device
follows Start/Continue/Stop; in any mode an incoming Stop under an external
clock still silences the sounding note immediately (safety).
- [ ] Czech file mirrors every change. Commit:
`docs(transport): manual coverage for the Transport setting (EN+CZ)`

---

### Task 4: Verify

- [ ] `pio test -e test` ALL PASS, `pio run -e native` + `teensy41`
SUCCESS warning-clean.
- [ ] (controller/human) Sim check: Settings → MIDI → TRNSPT; Send: Berlin
Latch1/2 start/stop a DAW; Receive: DAW play/stop drives Berlin
(Start from top, Continue from position); Off: neither.

---

## Self-review notes

- Spec §1→T1 (enum/setting/UI), §2→T1 (gating + Berlin emission,
  flip-gated, no sync-frame emission), §3→T2 (Receive mapping
  Reset+Play/Play/Pause, consume), §4→T2 (safety), §5→T2 (forwarding
  deleted), §7→tests in T1/T2, manuals §6→T3.
- `notifyLocalTransport` deliberately skips `onTransport` echo-back — the
  initiating mode already acted; Berlin's local pause keeps its ring-out
  (silence is only forced by *received* Pause or the safety).
- Default Send keeps every existing transport test green except the one
  forwarding-era test updated in T2 Step 4.
- Berlin Latch1 emission is flip-gated so the hardware's every-frame level
  delivery can't spam Start; the first-frame sync adoption stays silent.
