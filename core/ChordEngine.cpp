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
    // to multiple mappings — useful for layering). Each match either
    // plays immediately (engine idle and queue empty) or appends to
    // the FIFO so chords are heard in trigger order.
    for (int i = 0; i < kMaxMappings; ++i) {
        const Mapping& m = mappings_[i];
        if (!m.live) continue;
        if (m.triggerNote != msg.data1) continue;
        if (m.triggerChannel != 0 && m.triggerChannel != msg.channel) continue;
        if (isBusy() || queueSize() > 0) {
            enqueue(i, nowMs);
        } else {
            scheduleFromQueue(i, nowMs);
        }
    }
}

bool ChordEngine::isBusy() const {
    for (const auto& ev : events_) {
        if (ev.alive) return true;
    }
    return false;
}

bool ChordEngine::enqueue(int mappingIndex, uint32_t nowMs) {
    for (auto& q : queue_) {
        if (!q.alive) {
            q.alive        = true;
            q.mappingIndex = mappingIndex;
            q.enqueuedMs   = nowMs;
            return true;
        }
    }
    return false;  // queue full — drop the trigger silently
}

int ChordEngine::oldestQueueIndex() const {
    int      idx     = -1;
    uint32_t oldest  = 0;
    bool     hasOne  = false;
    for (int i = 0; i < kMaxQueue; ++i) {
        const auto& q = queue_[i];
        if (!q.alive) continue;
        if (!hasOne || static_cast<int32_t>(q.enqueuedMs - oldest) < 0) {
            oldest = q.enqueuedMs;
            idx    = i;
            hasOne = true;
        }
    }
    return idx;
}

int ChordEngine::queueSize() const {
    int n = 0;
    for (const auto& q : queue_) if (q.alive) ++n;
    return n;
}

int ChordEngine::queueAt(int pos) const {
    // Walk queue entries in FIFO order (by enqueuedMs ascending).
    // Stable across calls because each entry has its own timestamp.
    int      sentinel = -1;
    uint32_t prevTs   = 0;
    bool     prevSet  = false;
    for (int rank = 0; ; ++rank) {
        int      best     = -1;
        uint32_t bestTs   = 0;
        bool     bestSet  = false;
        for (int i = 0; i < kMaxQueue; ++i) {
            const auto& q = queue_[i];
            if (!q.alive) continue;
            // Must come after the previously-picked one (strict).
            if (prevSet &&
                static_cast<int32_t>(q.enqueuedMs - prevTs) <= 0) continue;
            if (!bestSet ||
                static_cast<int32_t>(q.enqueuedMs - bestTs) < 0) {
                best    = i;
                bestTs  = q.enqueuedMs;
                bestSet = true;
            }
        }
        if (!bestSet) return sentinel;
        if (rank == pos) return queue_[best].mappingIndex;
        prevTs  = bestTs;
        prevSet = true;
    }
}

void ChordEngine::scheduleFromQueue(int mappingIndex, uint32_t nowMs) {
    if (mappingIndex < 0 || mappingIndex >= kMaxMappings) return;
    const Mapping& m = mappings_[mappingIndex];
    if (!m.live) return;
    currentMappingIndex_ = mappingIndex;
    schedule(m, nowMs);
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
    bool stillBusy = false;
    for (auto& ev : events_) {
        if (!ev.alive) continue;
        // Use wrap-safe signed comparison so a clock that has just rolled
        // over doesn't strand events.
        if (static_cast<int32_t>(nowMs - ev.scheduledMs) < 0) {
            stillBusy = true;
            continue;
        }
        if (ev.isOn) {
            out_->sendNoteOn(ev.channel, ev.note, ev.velocity);
        } else {
            out_->sendNoteOff(ev.channel, ev.note);
        }
        if (echoFn_) {
            MidiMessage m{};
            m.type    = ev.isOn ? MidiType::NoteOn : MidiType::NoteOff;
            m.channel = ev.channel;
            m.data1   = ev.note;
            m.data2   = ev.isOn ? ev.velocity : static_cast<uint8_t>(0);
            echoFn_(echoUser_, m);
        }
        ev.alive = false;
    }
    if (stillBusy) return;
    // Engine just went idle. Mark so, then pop the next queued chord
    // (if any) and schedule it from `nowMs`. We only pop one per tick:
    // the freshly-scheduled events become alive, so the next tick will
    // see the engine busy again and wait for them to finish.
    currentMappingIndex_ = -1;
    const int next = oldestQueueIndex();
    if (next < 0) return;
    const int mappingIndex = queue_[next].mappingIndex;
    queue_[next].alive = false;
    scheduleFromQueue(mappingIndex, nowMs);
}

void ChordEngine::panic() {
    if (out_) {
        for (auto& ev : events_) {
            if (ev.alive && !ev.isOn) {
                // Best-effort: fire pending NoteOffs so we don't leave
                // notes hanging on the downstream synth.
                out_->sendNoteOff(ev.channel, ev.note);
                if (echoFn_) {
                    MidiMessage m{};
                    m.type    = MidiType::NoteOff;
                    m.channel = ev.channel;
                    m.data1   = ev.note;
                    m.data2   = 0;
                    echoFn_(echoUser_, m);
                }
            }
        }
    }
    for (auto& ev : events_) ev.alive = false;
    for (auto& q  : queue_)  q.alive  = false;
    currentMappingIndex_ = -1;
}

} // namespace core
