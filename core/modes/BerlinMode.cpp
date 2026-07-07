#include "core/modes/BerlinMode.h"

#include <cstdio>

#include "core/BerlinGen.h"           // berlinEnforceConsonance (consonance pass)
#include "core/Display.h"
#include "core/MidiOutput.h"
#include "core/Presets.h"
#include "core/render/BerlinLayout.h" // drawBerlinMultiRoll, kBerlinVoiceColors, kBerlinVoiceNames

namespace core {

BerlinMode::BerlinMode(AppServices& svc) : svc_(svc) {
    // Role defaults per spec §2.3 (see the table in the plan header).
    voices_[kBass].params.octaveBase  = 24;   // C1
    voices_[kBass].params.octaveRange = 1;
    voices_[kBass].params.density     = 30;
    voices_[kBass].params.gatePercent = 50;
    voices_[kBass].params.length      = 16;
    voices_[kBass].channel            = 1;

    voices_[kMid].params.octaveBase   = 48;   // C3
    voices_[kMid].params.octaveRange  = 1;
    voices_[kMid].params.length       = 15;   // phases 15x16 against High/Bass
    voices_[kMid].channel             = 2;

    voices_[kHigh].params.octaveBase  = 60;   // C4
    voices_[kHigh].params.octaveRange = 1;
    voices_[kHigh].params.length      = 16;
    voices_[kHigh].channel            = 3;

    voices_[kLead].params.octaveBase  = 60;   // C4 (spans C4–C6 with range 2)
    voices_[kLead].params.octaveRange = 2;
    voices_[kLead].params.density     = 30;   // sparse, lots of rests
    voices_[kLead].params.gatePercent = 85;   // legato
    voices_[kLead].params.length      = 16;
    voices_[kLead].channel            = 4;
    // algorithm stays the default (DrunkardWalk), like Mid/High.

    for (int v = 0; v < kVoices; ++v) {
        applyGenerator(v);
        voices_[v].engine.setParams(voices_[v].params);
        voices_[v].engine.setOutChannel(voices_[v].channel);
    }
}

void BerlinMode::applyGenerator(int v) {
    if (v == kBass) { voices_[v].engine.setGenerator(&bassGen_); return; }
    switch (voices_[v].params.algorithm) {
        case BerlinAlgorithm::DegreeWeighted:   voices_[v].engine.setGenerator(&degreeGen_);  break;
        case BerlinAlgorithm::GatePitchPhasing: voices_[v].engine.setGenerator(&phasingGen_); break;
        case BerlinAlgorithm::DrunkardWalk:
        default:                                voices_[v].engine.setGenerator(&walkGen_);    break;
    }
}

Screen& BerlinMode::screen(int i) {
    if (i == 1) return characterScreen_;
    if (i == 2) return voicesScreen_;
    if (i == 3) return dynamicsScreen_;
    if (i == 4) return behaviorScreen_;
    if (i == 5) return presetScreen_;
    return structureScreen_;
}

// ---------------------------------------------------------------------------
// PresetOps (keys "berlin.s01".."berlin.s20")
// ---------------------------------------------------------------------------

bool BerlinMode::presetUsed(int slot) {
    Storage* st = svc_.storage();
    return st && berlinPreset2Usable(*st, slot, kVoices);
}

bool BerlinMode::savePreset(int slot) {
    Storage* st = svc_.storage();
    if (!st) return false;
    BerlinVoicePreset v[kVoices];
    for (int i = 0; i < kVoices; ++i) {
        v[i].params  = voices_[i].params;
        v[i].seq     = voices_[i].engine.sequence();
        v[i].channel = voices_[i].channel;
        v[i].muted   = voices_[i].engine.muted();
    }
    return saveBerlinPreset2(*st, slot, v, kVoices);
}

bool BerlinMode::loadPreset(int slot) {
    Storage* st = svc_.storage();
    if (!st) return false;
    BerlinVoicePreset v[kVoices];
    if (!loadBerlinPreset2(*st, slot, v, kVoices)) return false;
    for (int i = 0; i < kVoices; ++i) {
        voices_[i].params  = v[i].params;
        voices_[i].channel = v[i].channel;
        applyGenerator(i);
        voices_[i].engine.setParams(v[i].params);
        voices_[i].engine.setOutChannel(v[i].channel);
        voices_[i].engine.setMuted(v[i].muted);
        voices_[i].engine.setSequence(v[i].seq);   // mid-play: playhead wraps
    }
    syncGlobals();   // the loaded voice-0 globals become canonical everywhere
    return true;
}

bool BerlinMode::deletePresetSlot(int slot) {
    Storage* st = svc_.storage();
    return st && deletePreset(*st, "berlin", slot);
}

void BerlinMode::onEnter() {
    scale_ = svc_.scale();
    transposeDegrees_ = 0;
    bool generated = false;
    for (int v = 0; v < kVoices; ++v) {
        Voice& vc = voices_[v];
        vc.engine.setScale(&scale_);
        vc.engine.setParams(vc.params);
        vc.engine.setOutChannel(vc.channel);
        vc.engine.setTransposeDegrees(0);
        applyGenerator(v);
        if (!vc.engine.sequence().step(0).active) {       // ensure something to show/play
            vc.engine.generate();
            generated = true;
        }
    }
    // The boot/first-entry stack gets the same consonance pass as Generate.
    // Only when something was actually generated — re-entering the mode must
    // never mutate sequences the user already has (Locked stays locked).
    if (generated) { maskLeadAgainstHigh(); enforceConsonance(); }
    for (int i = 0; i < 4; ++i) latchSynced_[i] = false;  // re-absorb first delivery per index
}

// Vertical consonance across the stack (spec §2.4 step 3); the highest
// per-voice tension decides the skip. Runs after Generate and after a
// first-entry generation.
void BerlinMode::enforceConsonance() {
    int tension = 0;
    for (int v = 0; v < kVoices; ++v)
        if (voices_[v].params.tension > tension)
            tension = voices_[v].params.tension;
    BerlinSequence* seqs[kVoices];
    for (int v = 0; v < kVoices; ++v)
        seqs[v] = &voices_[v].engine.sequenceMut();
    berlinEnforceConsonance(seqs, kVoices, scale_, tension);
}

// Call-and-response (spec §8): deactivate each Lead step whose aligned index
// collides with an active High step, so Lead tends to play in High's gaps.
// Approximate under phasing (alignment drifts over loops), applied at Generate.
void BerlinMode::maskLeadAgainstHigh() {
    BerlinSequence& lead = voices_[kLead].engine.sequenceMut();
    const BerlinSequence& high = voices_[kHigh].engine.sequence();
    const int hlen = high.length() < 1 ? 1 : high.length();
    for (int i = 0; i < lead.length(); ++i)
        if (lead.step(i).active && high.step(i % hlen).active)
            lead.step(i).active = false;
}

void BerlinMode::update(uint32_t /*nowMs*/) {
    scale_ = svc_.scale();
    for (int v = 0; v < kVoices; ++v) {
        Voice& vc = voices_[v];
        vc.engine.setScale(&scale_);
        vc.engine.setParams(vc.params);
        vc.engine.setOutChannel(vc.channel);   // per-voice channels (not the global out channel)
        applyGenerator(v);
    }
}

void BerlinMode::onTransport(Transport t) {
    for (int v = 0; v < kVoices; ++v) {
        BerlinEngine& e = voices_[v].engine;
        switch (t) {
            case Transport::Play:  e.play();  break;
            case Transport::Pause: e.silence(); e.pause(); break;
            case Transport::Reset:
            case Transport::Stop:  e.stop();  break;
        }
    }
}

void BerlinMode::onMidiIn(const MidiMessage& msg) {
    // Diatonic transposition control (latched): a NoteOn sets the new key
    // centre; a NoteOff -- or a NoteOn with velocity 0 -- is ignored, so the
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

void BerlinMode::onRawInput(const RawInput& in) {
    if (in.kind != RawInput::Kind::Latch) return;     // encoders go via Screen
    if (in.index < 1 || in.index > 3) return;
    // The three panel switches mechanically latch, but the SOFTWARE treats them
    // as stateless CLICK buttons: every level change (flip) = one click; the
    // switch POSITION carries no meaning. All run-state truth lives in the
    // engine and the top bar, never in the switch position. So every latch acts
    // on a flip, in either direction — one toggle = one click. The first
    // delivery per index after onEnter() is absorbed: record the level (no
    // action) so a stale shadow across boot/mode re-entry cannot fire a phantom
    // click that (for Latch3) would regenerate and destroy a Locked sequence.
    const bool firstDelivery = !latchSynced_[in.index];
    const bool changed = in.on != lastLatch_[in.index];
    lastLatch_[in.index] = in.on;
    latchSynced_[in.index] = true;
    const bool flip = changed && !firstDelivery;
    switch (in.index) {
        case 1:  // Play/Pause — a CLICK toggles by the engine state. The switch
                 // position carries no meaning (it is a mechanical latch, but the
                 // software treats every flip as a stateless button press); all
                 // run-state truth lives in the engine and the top bar. Every
                 // voice runs in lockstep, so voice 0's state is authoritative.
            if (flip) {
                const bool playing = voices_[0].engine.isPlaying();  // all in sync
                for (int v = 0; v < kVoices; ++v) {
                    if (playing) voices_[v].engine.pause();
                    else         voices_[v].engine.play();
                }
                svc_.notifyLocalTransport(playing ? Transport::Pause : Transport::Play);
            }
            break;
        case 2:  // Stop (any flip)
            if (flip) {
                for (int v = 0; v < kVoices; ++v) voices_[v].engine.stop();
                svc_.notifyLocalTransport(Transport::Stop);
            }
            break;
        case 3:  // Reset/Generate (any flip) — regenerate every voice, no transport emission
            if (flip) {
                for (int v = 0; v < kVoices; ++v) {
                    voices_[v].engine.setParams(voices_[v].params);
                    applyGenerator(v);
                    voices_[v].engine.generate();
                }
                maskLeadAgainstHigh();
                enforceConsonance();
            }
            break;
    }
}

// ---- Combined multi-voice piano-roll --------------------------------------

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

} // namespace core
