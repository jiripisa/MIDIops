#pragma once

#include <cstddef>
#include <cstdint>

namespace core {

class MidiOutput;
struct MidiMessage;

// Trigger-driven chord player. Accepts incoming NoteOn messages; when one
// matches a stored mapping, schedules NoteOn / NoteOff events on the
// configured output channel — either as a block (all notes at once) or
// as an arpeggio (notes played sequentially, ascending or descending).
//
// Timing is millisecond-based, derived from the engine's current BPM and
// the 24-PPQN tick base. Gate length is per-note in ticks (1..96).
//
// Pure C++17 — talks to hardware only via the MidiOutput abstract
// interface, so the same engine drives both the Teensy build and the
// SDL/RtMidi simulator.
class ChordEngine {
public:
    enum class ChordType : uint8_t {
        Major     = 0,
        Minor     = 1,
        Dim       = 2,
        Aug       = 3,
        Dom7      = 4,
        Min7      = 5,
        Maj7      = 6,
        kCount    = 7,
    };

    enum class Direction : uint8_t {
        Block = 0,
        Up    = 1,
        Down  = 2,
    };

    struct Mapping {
        bool      live          = false;
        uint8_t   triggerNote   = 0;        // 0..127
        uint8_t   triggerChannel = 0;       // 0 = any, 1..16 = specific
        uint8_t   rootNote      = 60;       // 0..127 (chord root)
        ChordType type          = ChordType::Major;
        uint8_t   gateTicks     = 24;       // 1..96 (24 PPQN base)
        uint8_t   outputChannel = 1;        // 1..16
        Direction direction     = Direction::Block;
        uint8_t   velocity      = 100;
    };

    static constexpr int kMaxMappings = 16;
    static constexpr int kMaxEvents   = 64;

    // The engine doesn't own the MidiOutput — caller wires it.
    void setOutput(MidiOutput* out) { out_ = out; }
    void setBpm(uint16_t bpm)       { bpm_ = (bpm > 0) ? bpm : 120; }

    // Mapping management.
    void clearMappings();
    bool addMapping(const Mapping& m);    // returns false if pool is full
    void updateMapping(int index, const Mapping& m);
    int  mappingCount() const             { return mappingCount_; }
    int  mappingCapacity() const          { return kMaxMappings; }
    const Mapping& mappingAt(int i) const { return mappings_[i]; }

    // Returns the index of the first live mapping whose trigger note
    // matches `note` (and whose trigger channel matches or is 0/any).
    // Returns -1 if no match.
    int findMappingByTrigger(uint8_t note, uint8_t triggerChannel) const;

    // Returns the next live mapping index AFTER `fromIndex` (wraps to
    // the start), or -1 if there are no live mappings at all.
    int nextLiveIndex(int fromIndex) const;

    // Called by the host on every incoming MIDI message. Non-NoteOn
    // messages are ignored. NoteOff for a trigger doesn't currently
    // affect anything — each trigger schedules its full chord and
    // runs to completion.
    void onMessage(const MidiMessage& msg, uint32_t nowMs);

    // Advance scheduled events; fires any whose time has come.
    void tick(uint32_t nowMs);

    // Sends NoteOff for every event still in flight and clears the
    // queue. Useful when the host panics or switches mode.
    void panic();

private:
    struct Event {
        bool     alive       = false;
        bool     isOn        = false;     // true = NoteOn, false = NoteOff
        uint8_t  note        = 0;
        uint8_t  channel     = 1;
        uint8_t  velocity    = 0;         // only meaningful for isOn
        uint32_t scheduledMs = 0;
    };

    void schedule(const Mapping& m, uint32_t nowMs);
    int  intervalsForType(ChordType t, const int8_t** out) const;
    uint32_t ticksToMs(uint8_t ticks) const;

    Mapping     mappings_[kMaxMappings]{};
    int         mappingCount_ = 0;
    Event       events_[kMaxEvents]{};
    MidiOutput* out_ = nullptr;
    uint16_t    bpm_ = 120;
};

} // namespace core
