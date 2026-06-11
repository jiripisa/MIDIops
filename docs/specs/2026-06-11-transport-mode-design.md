# Transport mode (Off / Send / Receive) — design

Status: approved in brainstorm · Date: 2026-06-11 · Part of the v1 series.

A global **Transport** setting that makes MIDI transport (Start 0xFA /
Continue 0xFB / Stop 0xFC) handling explicit and complete. Today the device
half-sends (global latches only), half-receives (Stop silences engines when
following an external clock, Start/Continue do nothing) and blindly forwards —
none of it configurable.

## 1. The setting

```cpp
enum class TransportMode : uint8_t { Off = 0, Send, Receive, kCount };
```

- Owned by `AppShell`, exposed via `AppServices`
  (`transportMode()` / `setTransportMode()`). Default: **Send** (preserves
  today's master behaviour).
- Edited in **Settings → MIDI, Enc4** (the currently empty cell):
  name `TRNSPT`, values `Off` / `Send` / `Recv` (cycled with `cycleEnum`).
- **Independent of Clock source** — any combination is allowed
  (e.g. Receive + Internal clock = the DAW starts/stops the device while the
  device keeps its own tempo).

## 2. Send — the device is the transport master

When `transportMode == Send`:

- **Global latches** (non-capturing modes: Monitoring / BPM / Settings /
  Debug) emit Start / Continue / Stop exactly as today.
- **NEW — Berlin's local latches also emit:** Latch1 ON → **Start**
  (**Continue** when resuming from pause), Latch1 OFF → **Stop**,
  Latch2 (Stop) → **Stop**. Latch3 (Generate) emits nothing.
- Arp's latches are Hold / Mute / Reset — not transport; they never emit.

When `transportMode != Send`, **nothing emits transport** — the global
latches and Berlin's latches still perform their local function, but no
0xFA/0xFB/0xFC goes to the wire.

Mechanism: a new `AppServices::notifyLocalTransport(Transport t)` — the
shell updates `transportState_` (so the top-bar ▶/⏸ now also reflects
Berlin's local playback — a side benefit) and emits the matching MIDI
message only when the mode is Send. `AppShell::applyTransport` (global
latches) gates its sends on Send the same way.

## 3. Receive — the device follows incoming transport

When `transportMode == Receive`, incoming realtime transport drives playback
per the MIDI standard, and is **consumed** (never re-emitted):

| Incoming | Effect |
|---|---|
| **Start** | play **from the beginning**: active mode gets `onTransport(Reset)` then `onTransport(Play)` |
| **Continue** | resume **from the current position**: `onTransport(Play)` |
| **Stop** | halt, **keep the position**, silence the sounding note immediately: `onTransport(Pause)` |

Per-mode reactions:
- **Berlin** (`onTransport`): Play → `engine_.play()`; Pause →
  `engine_.pause()` + immediate `engine_.silence()` (new small engine helper:
  NoteOff for a sounding note without moving the playhead); Reset/Stop →
  `engine_.stop()` (rewind + silence).
- **Arp**: Stop and Pause → `engine_.stop()` (silence; an arp has no
  meaningful Start/Continue — they are ignored).
- Non-capturing modes: only `transportState_` (top bar) updates.

`transportState_` mirrors the received state (Start/Continue → Playing,
Stop → Paused-like Stopped; exact mapping settled in the plan).

When `transportMode != Receive`, incoming transport is **ignored** — with
one exception (§4).

## 4. Off — and the external-clock safety rule

`Off`: the device neither sends nor reacts to transport.

**Safety exception (always active, any mode):** when `clockSource ==
External` and an incoming **Stop** arrives, the active mode still gets the
silencing treatment (`onTransport(Pause)`). Rationale: the audit's N6 —
DAWs stop sending clock on Stop, so a gate-off scheduled on ticks would
never fire and the note would hang forever. The engine is halted by the
missing clock anyway; the safety only makes the silence immediate instead
of eternal.

## 5. Forwarding is removed

The current pass-through (incoming Start/Continue/Stop re-emitted
downstream when following an external clock) is **deleted**. In the new
model it is never right: Receive consumes, Send is itself the master, Off
ignores. (Same no-echo lesson as the external-clock fix in v0.5.)

## 6. Touched code

- `core/app/AppServices.h` — `TransportMode` enum, getter/setter,
  `notifyLocalTransport(Transport)`.
- `core/app/AppShell.{h,cpp}` — member + setting; `applyTransport` send
  gating; `notifyLocalTransport` impl; `onMidiIn` realtime-transport branch
  rewritten (Receive mapping, safety rule, forwarding removed).
- `core/modes/SettingsMode.{h,cpp}` — MIDI screen Enc4 cell.
- `core/modes/BerlinMode.{h,cpp}` — latch handlers call
  `notifyLocalTransport`; `onTransport` extended (Play/Pause/Reset).
- `core/BerlinEngine.{h,cpp}` — `silence()` helper.
- `core/modes/ArpMode.cpp` — `onTransport` also handles Pause.
- `MANUAL.md` + `MANUAL.cs.md` — Settings table, §4/§6, Berlin transport
  notes.

## 7. Testing

- Settings: Enc4 cycles Off/Send/Recv; default Send.
- Send gating: global latch emits Start only under Send (Off/Receive → no
  wire transport); Berlin Latch1 ON → Start under Send (Continue when
  resuming from pause), nothing under Off/Receive; Latch2 → Stop under Send.
- Receive: Start plays Berlin from step 0; Continue resumes from the held
  position; Stop pauses + silences immediately; incoming transport ignored
  under Send/Off (except the safety rule).
- Safety: Off + External clock + incoming Stop → sounding note silenced.
- Existing transport tests updated where the default-Send gating or the
  removed forwarding changes expectations.

## 8. Out of scope

- Song-position pointer (0xF2), MIDI Machine Control.
- Arp reacting to Start/Continue.
- Persisting the setting (flash persistence is its own future plan).
