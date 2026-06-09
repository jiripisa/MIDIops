#pragma once

#include <cstdint>

#include "core/ArpTypes.h"

namespace core {

class MidiOutput;
class Scale;

// ArpEngine — clock-driven arpeggiator with FIFO note queue.
//
// Queue model:
//   - FIFO of up to kQueueCap entries: { note, velocity }.
//   - The queue HEAD is the "active" note; the engine plays its arpeggio.
//   - One-shot (hold off, params_.latch == false): each played note plays
//     exactly ONE cycle (seqLen_ steps), then the engine advances the FIFO
//     (next queued note) or goes idle.  Physical hold state is irrelevant;
//     noteOff is a no-op.
//   - A "cycle" is seqLen_ emitted steps counted from the moment the note
//     became active (activeCycleSteps_ resets on promotion), regardless of
//     direction.  Reason: direction modes like UpDown/DownUp have irregular
//     periods; a fixed seqLen_-step cycle gives a predictable boundary.
//   - Append, never interrupt: new NoteOns append to the FIFO tail while an
//     active note is playing.
//   - Latch (params_.latch == true): loops forever; noteOff is also a no-op.
//     A new NoteOn is stored as a single pending replacement; at the next
//     cycle boundary the active note is replaced by the pending one.
//     Toggling latch ON→OFF while looping makes the engine finish the current
//     cycle then stop (falls out naturally: next boundary takes one-shot path).
//   - stop(): emit NoteOff for any sounding note, clear queue, go idle.
//   - reset(): return step index to 0; keep the active note.

class ArpEngine {
public:
    using EchoFn = void(*)(void* user, bool isOn, uint8_t channel, uint8_t note, uint8_t velocity);

    void setOutput(MidiOutput* o) { out_ = o; }
    void setEcho(EchoFn fn, void* user) { echo_ = fn; echoUser_ = user; }
    void setBpm(uint16_t bpm) { bpm_ = bpm ? bpm : 120; }
    void setScale(const Scale* s) { scale_ = s; }
    void setParams(const ArpParams& p) { params_ = p; }
    void setOutChannel(uint8_t ch) { outChannel_ = ch; }
    void setMuted(bool m) { muted_ = m; }
    bool muted() const { return muted_; }

    void noteOn(uint8_t note, uint8_t velocity, uint32_t nowMs);
    void noteOff(uint8_t note, uint32_t nowMs);
    void tick(uint32_t nowMs);
    void stop();
    void reset();

    bool isPlaying() const { return active_; }

private:
    // ---------------------------------------------------------------------------
    // Queue
    // ---------------------------------------------------------------------------
    static constexpr int kQueueCap = 16;
    struct QueueEntry { uint8_t note; uint8_t velocity; };
    QueueEntry queue_[kQueueCap] = {};
    int        qHead_  = 0;   // index of head element
    int        qCount_ = 0;   // number of valid elements

    void     qPush(uint8_t note, uint8_t velocity);
    void     qPop();          // remove head

    // Latch: pending replacement note (latest noteOn under latch while active)
    bool     latchHasPending_ = false;
    uint8_t  latchPendingNote_ = 0;
    uint8_t  latchPendingVel_  = 0;

    // ---------------------------------------------------------------------------
    // Helpers
    // ---------------------------------------------------------------------------
    void     emit(bool isOn, uint8_t note, uint8_t velocity);
    uint32_t msPerStep() const;

    // Set up seq_/seqPos_/udDir_/stepCount_/activeCycleSteps_ from the queue
    // head WITHOUT emitting any notes.  Call beginStep() afterwards.
    void     initSeqFromHead();

    // Core step-emission:
    //   1. Kill any prev sounding note (NoteOff).
    //   2. Emit the current step NoteOn, schedule gate NoteOff + next step time.
    //   3. Advance seqPos_, increment activeCycleSteps_.
    //   4. If cycle complete: immediately dequeue / replace / loop (calls itself
    //      recursively for the new head note — tail-recursive same-tick promotion;
    //      depth ≤ 1 for steps>1, ≤ kQueueCap (16) when steps==1 — bounded and
    //      safe on the target's stack).
    void     beginStep(uint32_t nowMs);

    int      nextSeqIndex();              // advance seqPos_ per direction
    uint8_t  velocityForStep(int seqPos) const;

    // ---------------------------------------------------------------------------
    // State
    // ---------------------------------------------------------------------------
    MidiOutput*  out_ = nullptr;
    EchoFn       echo_ = nullptr; void* echoUser_ = nullptr;
    const Scale* scale_ = nullptr;
    ArpParams    params_{};
    uint16_t     bpm_ = 120;
    uint8_t      outChannel_ = 1;

    bool     muted_  = false;
    bool     active_ = false;
    uint8_t  rootVel_ = 100;
    uint8_t  seq_[16] = {};
    int      seqLen_ = 0;
    int      seqPos_ = 0;          // index into seq_ for the NEXT step to emit
    int      stepCount_ = 0;       // emitted-step counter, for swing odd-step detection
    int8_t   udDir_ = +1;          // for UpDown/DownUp traversal
    uint32_t nextStepMs_ = 0;
    bool     noteSounding_ = false;
    uint8_t  soundingNote_ = 0;
    uint32_t noteOffMs_ = 0;
    uint32_t randState_ = 0x12345;

    // Per-active-note cycle tracking.
    // activeCycleSteps_: steps emitted since the current queue-head note
    //   became active.  Resets to 0 at each cycle boundary (and on promotion)
    //   to prevent integer overflow over long sessions.
    int  activeCycleSteps_ = 0;

    // Deferred cycle-boundary flag: set when a cycle completes inside beginStep
    // and cleared at the START of the next beginStep call.  Keeps active_==true
    // through the last note's gate so a late noteOn simply appends to the queue
    // instead of incorrectly starting a fresh sequence and cutting the last note.
    bool cyclePending_ = false;
};

} // namespace core
