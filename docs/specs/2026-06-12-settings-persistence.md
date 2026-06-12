# Settings persistence + factory reset — design

Status: approved in brainstorm · Date: 2026-06-12.

Global settings survive a power cycle: everything in the Settings section
plus the tempo is saved automatically (debounced) and restored at boot. A
new third Settings screen offers a two-step **factory reset**.

## 1. What is persisted

| Field | Default | Valid range |
|---|---|---|
| Scale type | Major | enum < kCount |
| Scale root | C (0) | 0..11 |
| MIDI out channel | 1 | 1..16 |
| MIDI in channel | OMNI (0) | 0..16 |
| Clock source | Internal | Internal/External |
| Transport mode | Send | enum < kCount |
| BPM | 120 | 30..300 |

Mode parameters (Arp/Berlin) are out of scope for now; the storage API is
key-based so they can be added later under their own keys.

## 2. Storage abstraction (the architectural rule applies)

New `core/Storage.h` interface: `load(key, buf, len)` (exactly `len` bytes
or fail), `save(key, buf, len)`, `remove(key)`. Implementations:

* `platform/teensy/TeensyStorage` — LittleFS_Program on a 64 KB region of
  the unused program flash (LittleFS wear-levels internally).
* `platform/host/FileStorage` — files under `$HOME/.midiops/`.

## 3. Wire format (key `"settings"`)

13 explicit bytes (no struct memcpy, no padding concerns): magic `MOPS`,
version `1`, then scaleType, scaleRoot, outCh, inCh, clockSource,
transportMode, bpm lo, bpm hi. Any failure — missing key, short read, bad
magic/version, any field out of range — leaves the compile-time defaults
untouched (never a partial apply).

## 4. AppShell behaviour

* `setStorage(Storage*)` before `begin()`; nullptr keeps today's behaviour.
* `begin()` loads + validates + applies (via the setters, so the clock
  master is reprogrammed; clock source applied last), then clears dirty.
* Every settings setter (`setBpm`, `setScaleType/Root`, `setMidiOut/InChannel`,
  `setClockSource`, `setTransportMode`) marks dirty with a timestamp.
* `tick()` saves once when dirty and ≥ 2 s (`kSettingsSaveDebounceMs`)
  passed since the **last** change — a knob twist is a single flash write.
  BPM derived from a followed external clock writes `bpm_` directly and
  never marks dirty.
* New `AppServices::factoryReset()` (default no-op; AppShell overrides):
  restores all defaults via the setters, `remove("settings")`, clears dirty.

## 5. Settings UI

Third screen **system**: a FACTORY RESET cell driven by Enc1 **press**
(two-step): idle shows `PRESS`, first press arms (`SURE?`) for 3 s, second
press within the window resets and shows `DONE` briefly; the window expiring
returns to idle. Rotation does nothing on this screen.

## 6. Testing

Shell-level with a FakeStorage: stored blob applied at boot; corrupt
magic/version/range → defaults; an edit saves exactly once, 2 s after the
last change (not immediately, not repeatedly); factory reset restores
defaults and removes the key. Settings-mode level: third screen exists;
arm→confirm calls factoryReset; the arm window expires. Manuals (EN+CZ),
CLAUDE.md scope notes updated.
