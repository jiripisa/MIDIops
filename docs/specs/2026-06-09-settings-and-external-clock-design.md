# Settings mode + external clock — design

Status: for review · Date: 2026-06-09 · Part of the v1 series
(`2026-06-02-v1-mode-architecture-design.md`, §7.5 + §8).

Implemented as **two plans**:
- **Plan 5a — Settings mode + simple settings** (scale type, root, MIDI-out
  channel, MIDI-in channel filter). Uses existing infrastructure; no timing
  changes. The clock-source toggle exists here but only stores a flag.
- **Plan 5b — External clock + tick-driven engines** (realtime MIDI-in,
  a unified ClockSource, ArpEngine reworked to advance on 24-PPQN ticks,
  external-clock follow + forwarding). The substantial cross-layer subsystem.

Persistence to flash is **deferred** (a later plan) — settings live in RAM
and reset to defaults on power cycle.

---

## 1. Settings model (Plan 5a)

Five global settings, owned by `AppShell` and exposed via `AppServices`:

| Setting | Type / range | Default |
|---|---|---|
| Scale type | `Scale::Type` (maj/min/aug/dim/penta-maj/penta-min) | Major |
| Root key | pitch class 0..11 (C..B) | C (0) |
| MIDI-out channel | 1..16 | 1 |
| MIDI-in channel | OMNI (0) / 1..16 | OMNI |
| Clock source | Internal / External | Internal |

Scale type + root already exist in `AppServices` (Plan 4). Add:
```cpp
    virtual uint8_t  midiOutChannel() const = 0;
    virtual void     setMidiOutChannel(uint8_t ch) = 0;     // 1..16
    virtual uint8_t  midiInChannel() const = 0;             // 0 = OMNI
    virtual void     setMidiInChannel(uint8_t ch) = 0;      // 0..16
    enum class ClockSource { Internal, External };
    virtual ClockSource clockSource() const = 0;
    virtual void     setClockSource(ClockSource s) = 0;
```
`AppShell` holds the backing members + defaults.

## 2. MIDI-in channel filter (Plan 5a)

Currently `AppShell::onMidiIn` forwards every message to the active mode.
Add a global channel filter there:
- If `midiInChannel_ != 0` (not OMNI) and the message is channel-voice with a
  channel != `midiInChannel_`, drop it before forwarding.
- OMNI (0) passes everything (current behaviour).
- The **Debug mode** must still see raw hardware; it observes encoders/latches
  via `onRawInput`, not MIDI, so it is unaffected. Realtime/clock messages
  (Plan 5b) are not channel-voice and bypass the filter.

This makes the filter consistent for all modes (Monitoring, Arp, Berlin…)
without each re-implementing it.

## 3. MIDI-out channel (Plan 5a)

There is no shared output channel today — `ArpMode` hardcodes `outChannel_ = 1`.
Make it global: `ArpMode` (and future generators) read `svc_.midiOutChannel()`
each `update()` and call `engine_.setOutChannel(...)`. Default 1 → unchanged
behaviour until the user edits it.

## 4. Settings mode + UI (Plan 5a)

`SettingsMode` (in `core/modes/`), a `Mode` taking `AppServices&`. It edits the
settings live via the same param-screen pattern as `ArpMode` (2×2 grid, small
name + large value, `cycleEnum`/clamp on `onEncoder`).

Five settings → **two screens** (Enc5 cycles them):
- **Screen "midi"**: Enc1 = MIDI-out channel (1..16), Enc2 = MIDI-in channel
  (OMNI,1..16), Enc3 = Clock source (Internal/External), Enc4 = (empty / spare).
- **Screen "scale"**: Enc1 = Scale type (cycle), Enc2 = Root key (C..B),
  Enc3/Enc4 spare.

(Exact grouping settled in the plan; the point is each setting maps to one
encoder and renders name+value.) Editing calls the `AppServices` setters
immediately. `SettingsMode` does NOT capture transport (the latches keep their
normal/global meaning here).

Registration: add `SettingsMode` to both mains, in spec order
(Monitoring, Arp, Berlin, BPM, **Settings**, Debug).

## 5. Realtime MIDI input (Plan 5b)

Extend the MIDI plumbing to carry realtime messages:
- `core::MidiType` gains `Clock = 0xF8`, `Start = 0xFA`, `Continue = 0xFB`,
  `Stop = 0xFC`. (`channel`/`data1`/`data2` are unused for these.)
- **Teensy** `TeensyMidiInput`: map `usbMIDI.Clock/Start/Continue/Stop` →
  the new `MidiType`s.
- **Host** `RtMidiInput`: stop discarding realtime (`status >= 0xF0`); deliver
  the four clock/transport bytes as `MidiMessage`s. (Other System messages
  stay ignored.)

These reach `AppShell::onMidiIn`; they are NOT channel-voice, so they bypass
the channel filter and are routed to the clock subsystem (below), not the
active mode's note handling.

## 6. Unified clock + tick-driven engines (Plan 5b)

### The tick stream
Generators (Arp, future Berlin) advance on a **24-PPQN tick** stream instead
of millisecond polling. A single `ClockSource` (core) produces ticks from one
of two origins, selected by `clockSource()`:

- **Internal:** the autonomous clock master (the existing
  `TeensyMidiOutput` IntervalTimer / `RtMidiOutput` thread) already pulses at
  24 PPQN. Expose `uint32_t consumeClockTicks()` on `MidiOutput` — an
  atomically-incremented counter of pulses generated since the last call. The
  platform main loop drains it each iteration and feeds the shell that many
  ticks. (The ISR/thread stays the precise timing source; the main loop only
  drains accumulated ticks — sub-loop latency, no per-note ms jitter.)
- **External:** each incoming MIDI `Clock` (0xF8) message is one tick.

`AppShell` owns the routing: on each `tick(nowMs)` (internal) or on each
incoming Clock message (external), it calls `activeMode->onClockTick()` (a new
`Mode` hook) the appropriate number of times. Only the active mode is ticked
(consistent with the current `update()` rule).

### BPM derivation (external)
From the incoming Clock pulses, derive BPM with a moving average over the last
**24 pulses** (one beat): `bpm = 60000 / (ms for last 24 ticks)`, clamped to
[30..300]. The derived BPM updates `AppServices::bpm()` so the BPM view and any
display reflect the followed tempo. (Internal BPM is set by the user as today.)

### ArpEngine rework (tick-driven)
Replace the ms scheduler with a tick scheduler:
- `onClockTick()` advances an internal tick counter. A step fires every
  `arpRateTicks(rate)` ticks (the table already exists: 24/12/8/6/4/3). The
  step/queue/boundary/hold/one-shot logic (decide-at-start, FIFO, latch) is
  **unchanged** — only the "when to advance a step" trigger changes from an ms
  threshold to a tick count.
- **Gate** becomes ticks: `gateTicks = arpRateTicks(rate) * gatePercent / 100`
  (min 1). The note-off fires after that many ticks. **Swing** delays odd
  steps by `(swing-50)% of arpRateTicks` ticks.
- This supersedes the simulator's ms-tick-frequency fix (the engine is now
  phase-locked to the clock on both platforms).
- Tests convert from `tick(ms)` to a sequence of `onClockTick()` calls; the
  behavioural assertions (sequence order, hold, FIFO, boundary) carry over.

### Clock output when External
When `clockSource == External`:
- The internal clock master stops generating its own pulses
  (`setClockBpm(0)`), so `consumeClockTicks()` returns 0.
- Each incoming Clock (0xF8) is **forwarded** to the MIDI output so downstream
  gear stays synced to the external master. Add a `forwardClock()` (send a raw
  0xF8) to `MidiOutput`, or reuse a realtime-send path.
- Incoming `Start`/`Stop`/`Continue` are forwarded too (pass-through), and may
  later drive a global play state (out of scope here — the device keeps
  running; transport→hold/mute in Arp is separate).
When switching back to Internal: resume `setClockBpm(bpm_)`.

## 7. Plan decomposition

- **Plan 5a:** AppServices/AppShell settings (out/in channel, clock-source
  flag) + onMidiIn channel filter + SettingsMode (2 screens) + Arp reads
  out-channel from settings + register in mains. Fully testable on host, no
  timing changes. The clock-source toggle stores the flag (no effect yet).
- **Plan 5b:** MidiType realtime + both platform MIDI-in deliver realtime +
  `MidiOutput::consumeClockTicks()`/`forwardClock()` + `ClockSource` routing in
  AppShell + `Mode::onClockTick()` + ArpEngine tick-driven rework + external
  BPM derivation + clock forwarding/switching. Engine logic unit-tested with
  `onClockTick()`; platform realtime I/O verified in the sim/hardware.

## 8. Testing

- **5a:** AppShell settings getters/setters + the onMidiIn channel filter
  (OMNI passes; specific channel drops others; realtime bypasses). SettingsMode
  edit/clamp/cycle via StubDisplay. Arp out-channel follows the setting.
- **5b:** MidiType realtime values; a `ClockFollower` BPM-derivation unit test
  (feed timed pulses → expected BPM, clamped, averaged). ArpEngine tick-driven
  step timing (N onClockTick → step boundaries), gate-in-ticks, swing; all the
  existing hold/FIFO/boundary tests converted to ticks. Clock forwarding +
  source-switch behaviour with a fake MidiOutput recording forwarded pulses.

## 9. Deferred / out of scope

- Flash persistence of settings (separate plan; also covers mappings).
- External transport (Start/Stop) controlling a global play/run state beyond
  forwarding — for now incoming transport is passed through only.
- Tempo display polish for followed external tempo.

## 10. Resolved questions (from brainstorm)

1. Clock source = **build full external now**, as Plan 5b.
2. Engine timebase = **driven by 24-PPQN clock ticks** (internal + external),
   not ms — ArpEngine reworked.
3. External clock output = **forward incoming clock** downstream (device stops
   generating its own).
4. MIDI-in filter = **global in AppShell** (OMNI default; Debug unaffected).
5. Persistence = **deferred** (RAM only for now).
6. Scope = **two plans** (5a Settings UI, 5b external clock).
