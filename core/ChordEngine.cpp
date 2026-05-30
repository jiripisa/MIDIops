#include "ChordEngine.h"

#include "MidiMessage.h"
#include "MidiOutput.h"

namespace core {

namespace {

// Intervals (semitones above the root) for each chord type. Order matters
// because the arpeggiator walks them left-to-right (Up) or right-to-left
// (Down).
constexpr int8_t kMajor[]  = {0, 4, 7};
constexpr int8_t kMinor[]  = {0, 3, 7};
constexpr int8_t kDim[]    = {0, 3, 6};
constexpr int8_t kAug[]    = {0, 4, 8};
constexpr int8_t kDom7[]   = {0, 4, 7, 10};
constexpr int8_t kMin7[]   = {0, 3, 7, 10};
constexpr int8_t kMaj7[]   = {0, 4, 7, 11};

} // namespace

int ChordEngine::intervalsForType(ChordType t, const int8_t** out) const {
    switch (t) {
        case ChordType::Major: *out = kMajor; return sizeof(kMajor);
        case ChordType::Minor: *out = kMinor; return sizeof(kMinor);
        case ChordType::Dim:   *out = kDim;   return sizeof(kDim);
        case ChordType::Aug:   *out = kAug;   return sizeof(kAug);
        case ChordType::Dom7:  *out = kDom7;  return sizeof(kDom7);
        case ChordType::Min7:  *out = kMin7;  return sizeof(kMin7);
        case ChordType::Maj7:  *out = kMaj7;  return sizeof(kMaj7);
        default:               *out = kMajor; return sizeof(kMajor);
    }
}

uint32_t ChordEngine::ticksToMs(uint8_t ticks) const {
    // 24 PPQN: one tick = 60000 / (BPM * 24) ms.
    return (static_cast<uint32_t>(ticks) * 60000u) /
           (static_cast<uint32_t>(bpm_) * 24u);
}

void ChordEngine::clearMappings() {
    for (auto& m : mappings_) m.live = false;
    mappingCount_ = 0;
}

bool ChordEngine::addMapping(const Mapping& m) {
    if (mappingCount_ >= kMaxMappings) return false;
    for (int i = 0; i < kMaxMappings; ++i) {
        if (!mappings_[i].live) {
            mappings_[i]      = m;
            mappings_[i].live = true;
            ++mappingCount_;
            return true;
        }
    }
    return false;
}

void ChordEngine::updateMapping(int index, const Mapping& m) {
    if (index < 0 || index >= kMaxMappings) return;
    if (!mappings_[index].live) return;
    const bool wasLive = mappings_[index].live;
    mappings_[index]      = m;
    mappings_[index].live = wasLive;
}

int ChordEngine::findMappingByTrigger(uint8_t note,
                                      uint8_t triggerChannel) const {
    for (int i = 0; i < kMaxMappings; ++i) {
        const Mapping& m = mappings_[i];
        if (!m.live) continue;
        if (m.triggerNote != note) continue;
        if (m.triggerChannel != 0 && triggerChannel != 0 &&
            m.triggerChannel != triggerChannel) continue;
        return i;
    }
    return -1;
}

int ChordEngine::nextLiveIndex(int fromIndex) const {
    if (mappingCount_ == 0) return -1;
    for (int step = 1; step <= kMaxMappings; ++step) {
        const int idx = (fromIndex + step) % kMaxMappings;
        if (mappings_[idx].live) return idx;
    }
    return -1;
}

void ChordEngine::onMessage(const MidiMessage& msg, uint32_t nowMs) {
    if (msg.type != MidiType::NoteOn) return;
    if (msg.data2 == 0)              return;   // velocity-0 = NoteOff
    // Match against every live mapping (a single trigger could be wired
    // to multiple mappings — useful for layering).
    for (int i = 0; i < kMaxMappings; ++i) {
        const Mapping& m = mappings_[i];
        if (!m.live) continue;
        if (m.triggerNote != msg.data1) continue;
        if (m.triggerChannel != 0 && m.triggerChannel != msg.channel) continue;
        schedule(m, nowMs);
    }
}

void ChordEngine::schedule(const Mapping& m, uint32_t nowMs) {
    const int8_t* intervals = nullptr;
    const int n = intervalsForType(m.type, &intervals);
    const uint32_t gateMs = ticksToMs(m.gateTicks);
    if (gateMs == 0) return;

    auto pushEvent = [this](bool isOn, uint8_t note, uint8_t channel,
                            uint8_t velocity, uint32_t whenMs) {
        for (auto& ev : events_) {
            if (ev.alive) continue;
            ev.alive       = true;
            ev.isOn        = isOn;
            ev.note        = note;
            ev.channel     = channel;
            ev.velocity    = velocity;
            ev.scheduledMs = whenMs;
            return;
        }
        // Pool full — silently drop. With 64 slots and typical use this
        // shouldn't happen.
    };

    if (m.direction == Direction::Block) {
        for (int i = 0; i < n; ++i) {
            const int pitch = static_cast<int>(m.rootNote) + intervals[i];
            if (pitch < 0 || pitch > 127) continue;
            pushEvent(true,  static_cast<uint8_t>(pitch), m.outputChannel,
                      m.velocity, nowMs);
            pushEvent(false, static_cast<uint8_t>(pitch), m.outputChannel,
                      0,          nowMs + gateMs);
        }
        return;
    }

    // Up / Down: walk intervals[] in the chosen direction; each step gets
    // its own slot of gateMs (no overlap).
    for (int step = 0; step < n; ++step) {
        const int idx = (m.direction == Direction::Up) ? step : (n - 1 - step);
        const int pitch = static_cast<int>(m.rootNote) + intervals[idx];
        if (pitch < 0 || pitch > 127) continue;
        const uint32_t startMs = nowMs + gateMs * static_cast<uint32_t>(step);
        pushEvent(true,  static_cast<uint8_t>(pitch), m.outputChannel,
                  m.velocity, startMs);
        pushEvent(false, static_cast<uint8_t>(pitch), m.outputChannel,
                  0,          startMs + gateMs);
    }
}

void ChordEngine::tick(uint32_t nowMs) {
    if (!out_) return;
    for (auto& ev : events_) {
        if (!ev.alive) continue;
        // Use wrap-safe signed comparison so a clock that has just rolled
        // over doesn't strand events.
        if (static_cast<int32_t>(nowMs - ev.scheduledMs) < 0) continue;
        if (ev.isOn) {
            out_->sendNoteOn(ev.channel, ev.note, ev.velocity);
        } else {
            out_->sendNoteOff(ev.channel, ev.note);
        }
        ev.alive = false;
    }
}

void ChordEngine::panic() {
    if (out_) {
        for (auto& ev : events_) {
            if (ev.alive && !ev.isOn) {
                // Best-effort: fire pending NoteOffs so we don't leave
                // notes hanging on the downstream synth.
                out_->sendNoteOff(ev.channel, ev.note);
            }
        }
    }
    for (auto& ev : events_) ev.alive = false;
}

} // namespace core
