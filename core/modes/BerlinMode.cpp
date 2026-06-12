#include "core/modes/BerlinMode.h"

#include <cstdio>

#include "core/BerlinGen.h"           // berlinGateTicks (live gate re-stamp)
#include "core/Display.h"
#include "core/MidiOutput.h"
#include "core/Presets.h"
#include "core/render/BerlinLayout.h" // drawBerlinParamCell, drawBerlinParamDividers, drawBerlinPianoRoll
#include "core/render/ParamGrid.h"   // cycleEnum

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
    return st && presetExists(*st, "berlin", slot);
}

bool BerlinMode::savePreset(int slot) {
    Storage* st = svc_.storage();
    return st && saveBerlinPreset(*st, slot, editParams(), editEngine().sequence());
}

bool BerlinMode::loadPreset(int slot) {
    Storage* st = svc_.storage();
    if (!st) return false;
    BerlinParams   p;
    BerlinSequence seq;
    if (!loadBerlinPreset(*st, slot, p, seq)) return false;
    editParams() = p;
    applyGenerator(editVoice_);
    editEngine().setParams(p);
    editEngine().setSequence(seq);    // seamless mid-play: playhead wraps, keeps running
    return true;
}

bool BerlinMode::deletePresetSlot(int slot) {
    Storage* st = svc_.storage();
    return st && deletePreset(*st, "berlin", slot);
}

void BerlinMode::onEnter() {
    scale_ = svc_.scale();
    for (int v = 0; v < kVoices; ++v) {
        Voice& vc = voices_[v];
        vc.engine.setScale(&scale_);
        vc.engine.setParams(vc.params);
        vc.engine.setOutChannel(vc.channel);
        applyGenerator(v);
        if (!vc.engine.sequence().step(0).active) vc.engine.generate();  // ensure something to show/play
    }
    for (int i = 0; i < 4; ++i) latchSynced_[i] = false;  // re-absorb first delivery per index
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
            }
            break;
    }
}

// ---- Voices screen (mixer) ------------------------------------------------

static const char* kVoiceNames[3] = {"BASS", "MID", "HIGH"};

void BerlinMode::VoicesScreen::onEncoder(int index, int delta) {
    if (index < 1 || index > kVoices || delta == 0) return;
    Voice& v = mode_.voices_[index - 1];
    int ch = v.channel + delta;
    if (ch < 1)  ch = 1;
    if (ch > 16) ch = 16;
    v.channel = static_cast<uint8_t>(ch);
    v.engine.setOutChannel(v.channel);
}

void BerlinMode::VoicesScreen::onEncoderSw(int index) {
    if (index < 1 || index > kVoices) return;
    BerlinEngine& e = mode_.voices_[index - 1].engine;
    e.setMuted(!e.muted());
}

void BerlinMode::VoicesScreen::render(Display& d) const {
    char buf[12];
    for (int v = 0; v < kVoices; ++v) {
        const bool muted = mode_.voices_[v].engine.muted();
        snprintf(buf, sizeof buf, "CH %d", mode_.voices_[v].channel);
        drawBerlinParamCell(d, v, kVoiceNames[v], buf, muted);
        if (muted)
            d.drawText(v * kBerlinCellW + 4, kBerlinParamTop + 52, "MUTED",
                       color::Gray, color::Black, 1);
    }
    drawBerlinParamDividers(d);
    drawBerlinPianoRoll(d, mode_.engine().sequence(), mode_.engine().playhead(),
                        mode_.engine().soundingNote(), color::Green);
}

// ---- Structure screen -----------------------------------------------------

static const char* algoName(BerlinAlgorithm a) {
    switch (a) {
        case BerlinAlgorithm::DrunkardWalk:     return "Walk";
        case BerlinAlgorithm::GatePitchPhasing: return "Phase";
        case BerlinAlgorithm::DegreeWeighted:   return "Degree";
        default:                                return "?";
    }
}

// Re-stamp the baked per-step gateTicks of the active steps from the current
// params, so the piano-roll widths track a live Gate/Resolution change.
static void restampGateTicks(BerlinEngine& engine, const BerlinParams& p) {
    BerlinSequence& seq = engine.sequenceMut();
    const int g = berlinGateTicks(p);
    for (int i = 0; i < seq.length(); ++i) {
        if (seq.step(i).active) {
            seq.step(i).gateTicks = static_cast<uint16_t>(g);
        }
    }
}

void BerlinMode::StructureScreen::onEncoder(int index, int delta) {
    BerlinParams& p = mode_.editParams();
    switch (index) {
        case 1:  // Algorithm — no live action (applies at the next Generate).
            p.algorithm = cycleEnum(p.algorithm, delta);
            break;
        case 2: { int v = p.length + delta; if (v < 3) v = 3; if (v > 32) v = 32;
                  p.length = static_cast<uint8_t>(v);
                  if (mode_.live()) { mode_.editEngine().setParams(p); mode_.editEngine().applyLiveLength(); }
                } break;
        case 3:  // Resolution is inherently live in every behavior (the engine
                 // reads the grid per tick); just re-stamp the baked gateTicks
                 // so the piano-roll widths match. No regeneration.
            p.resolution = cycleEnum(p.resolution, delta);
            mode_.editEngine().setParams(p);
            restampGateTicks(mode_.editEngine(), p);
            break;
        case 4: { int v = p.density + delta * 5; if (v < 0) v = 0; if (v > 100) v = 100;
                  p.density = static_cast<uint8_t>(v);
                  if (mode_.live()) { mode_.editEngine().setParams(p); mode_.editEngine().applyLiveDensity(); }
                } break;
    }
}

void BerlinMode::StructureScreen::render(Display& d) const {
    const BerlinParams& p = mode_.editParams();
    char buf[12];
    drawBerlinParamCell(d, 0, "ALGO",    algoName(p.algorithm));
    snprintf(buf, sizeof buf, "%d", p.length);
    drawBerlinParamCell(d, 1, "LENGTH",  buf);
    drawBerlinParamCell(d, 2, "RESOL",   p.resolution == BerlinResolution::Sixteenth ? "16th" : "8th");
    snprintf(buf, sizeof buf, "%d%%", p.density);
    drawBerlinParamCell(d, 3, "DENSITY", buf);
    drawBerlinParamDividers(d);
    drawBerlinPianoRoll(d, mode_.engine().sequence(), mode_.engine().playhead(),
                        mode_.engine().soundingNote(), color::Green);
}

// ---- Character screen -------------------------------------------------------

// MIDI note → octave label using C-1 = MIDI 0 convention, e.g. 48 → "C3".
static void octaveLabel(uint8_t note, char* buf, int n) {
    snprintf(buf, n, "C%d", note / 12 - 1);
}

void BerlinMode::CharacterScreen::onEncoder(int index, int delta) {
    BerlinParams& p = mode_.editParams();
    switch (index) {
        case 1: { int v = p.gatePercent + delta;     if (v < 40) v = 40; if (v > 99) v = 99;
                  p.gatePercent = static_cast<uint8_t>(v);
                  // Gate applies LIVE in every behavior: the engine derives the
                  // gate from the current params at each step; re-stamping the
                  // baked per-step gateTicks keeps the piano-roll widths in sync.
                  mode_.editEngine().setParams(p);
                  restampGateTicks(mode_.editEngine(), p);
                } break;
        case 2: { int v = p.tension + delta * 5;     if (v < 0) v = 0;  if (v > 100) v = 100;
                  p.tension = static_cast<uint8_t>(v);
                  if (mode_.live()) { mode_.editEngine().setParams(p); mode_.editEngine().applyLiveTension(); }
                } break;
        case 3: { const int oldBase = p.octaveBase;
                  int v = p.octaveBase + delta * 12; if (v < 24) v = 24; if (v > 72) v = 72;
                  p.octaveBase = static_cast<uint8_t>(v);
                  // Pass the ACTUAL applied delta (clamping at the edges can make
                  // it less than delta*12), so the transpose matches the new base.
                  if (mode_.live()) {
                      mode_.editEngine().setParams(p);
                      mode_.editEngine().applyLiveOctaveBase(static_cast<int>(p.octaveBase) - oldBase);
                  }
                } break;
        case 4: { const int oldRange = p.octaveRange;
                  int v = p.octaveRange + delta;     if (v < 1) v = 1;  if (v > 3) v = 3;
                  p.octaveRange = static_cast<uint8_t>(v);
                  if (mode_.live() && v != oldRange) {
                      mode_.editEngine().setParams(p);
                      mode_.editEngine().applyLiveOctaveRange(oldRange);
                  }
                } break;
    }
}

void BerlinMode::CharacterScreen::render(Display& d) const {
    const BerlinParams& p = mode_.editParams();
    char buf[12];
    snprintf(buf, sizeof buf, "%d%%", p.gatePercent);
    drawBerlinParamCell(d, 0, "GATE",    buf);
    snprintf(buf, sizeof buf, "%d%%", p.tension);
    drawBerlinParamCell(d, 1, "TENSION", buf);
    octaveLabel(p.octaveBase, buf, sizeof buf);
    drawBerlinParamCell(d, 2, "OCT",     buf);
    snprintf(buf, sizeof buf, "%d", p.octaveRange);
    drawBerlinParamCell(d, 3, "RANGE",   buf);
    drawBerlinParamDividers(d);
    drawBerlinPianoRoll(d, mode_.engine().sequence(), mode_.engine().playhead(),
                        mode_.engine().soundingNote(), color::Green);
}

// ---- Dynamics screen --------------------------------------------------------

void BerlinMode::DynamicsScreen::onEncoder(int index, int delta) {
    BerlinParams& p = mode_.editParams();
    switch (index) {
        case 1: {
            int v = p.velocityBase + delta;
            if (v < 1)   v = 1;
            if (v > 126) v = 126;
            p.velocityBase = static_cast<uint8_t>(v);
            // Velocity/Humanize/Accent are LIVE performance parameters (like Gate):
            // re-stamp every active step's velocity from its stable jitter + the
            // current params, in every behavior, so the knob is audible at once.
            mode_.editEngine().setParams(p);
            berlinStampVelocities(mode_.editEngine().sequenceMut(), p);
            break;
        }
        case 2: {
            int v = p.velocityHumanize + delta;
            if (v < 0)  v = 0;
            if (v > 30) v = 30;
            p.velocityHumanize = static_cast<uint8_t>(v);
            mode_.editEngine().setParams(p);
            berlinStampVelocities(mode_.editEngine().sequenceMut(), p);
            break;
        }
        case 3: {
            int v = p.accent + delta;
            if (v < 0)  v = 0;
            if (v > 27) v = 27;
            p.accent = static_cast<uint8_t>(v);
            mode_.editEngine().setParams(p);
            berlinStampVelocities(mode_.editEngine().sequenceMut(), p);
            break;
        }
        case 4:
            // Scatter applies to the Drunkard's Walk only; the cell is dimmed
            // and the knob locked under the other algorithms. It parameterizes
            // how a sequence is generated, so it takes effect at the next
            // Generate (no immediate live action).
            if (p.algorithm == BerlinAlgorithm::DrunkardWalk) {
                int v = p.scatter + delta;
                if (v < 1) v = 1;
                if (v > 7) v = 7;
                p.scatter = static_cast<uint8_t>(v);
            }
            break;
    }
}

void BerlinMode::DynamicsScreen::render(Display& d) const {
    const BerlinParams& p = mode_.editParams();
    char buf[12];
    snprintf(buf, sizeof buf, "%d", p.velocityBase);
    drawBerlinParamCell(d, 0, "VEL",   buf);
    snprintf(buf, sizeof buf, "+-%d", p.velocityHumanize);
    drawBerlinParamCell(d, 1, "HUMAN", buf);
    snprintf(buf, sizeof buf, "+%d", p.accent);
    drawBerlinParamCell(d, 2, "ACCENT", buf);
    snprintf(buf, sizeof buf, "%d", p.scatter);
    drawBerlinParamCell(d, 3, "SCATTER", buf,
                        p.algorithm != BerlinAlgorithm::DrunkardWalk);
    drawBerlinParamDividers(d);
    drawBerlinPianoRoll(d, mode_.engine().sequence(), mode_.engine().playhead(),
                        mode_.engine().soundingNote(), color::Green);
}

// ---- Behavior screen --------------------------------------------------------

static const char* behaviorName(BerlinBehavior b) {
    switch (b) {
        case BerlinBehavior::Locked: return "Lock";
        case BerlinBehavior::Evolve: return "Evolve";
        case BerlinBehavior::Live:   return "Live";
        default:                     return "?";
    }
}

void BerlinMode::BehaviorScreen::onEncoder(int index, int delta) {
    BerlinParams& p = mode_.editParams();
    switch (index) {
        case 1: p.behavior = cycleEnum(p.behavior, delta); break;
        case 2: { int v = p.morph + delta * 5;     if (v < 0) v = 0;  if (v > 100) v = 100;
                  p.morph = static_cast<uint8_t>(v); } break;
        case 3:
            // Evolve rate only matters under the Evolve behavior; the cell is
            // dimmed and the knob locked otherwise.
            if (p.behavior == BerlinBehavior::Evolve) {
                int v = p.evolveRate + delta;
                if (v < 1) v = 1;
                if (v > 8) v = 8;
                p.evolveRate = static_cast<uint8_t>(v);
            }
            break;
        case 4:
            // GateLen applies to Gate/Pitch Phasing only; dimmed/locked under
            // the other algorithms. It parameterizes how a sequence is
            // generated, so it takes effect at the next Generate (no immediate
            // live action).
            if (p.algorithm == BerlinAlgorithm::GatePitchPhasing) {
                int v = p.gateLen + delta;
                if (v < 3)  v = 3;
                if (v > 16) v = 16;
                p.gateLen = static_cast<uint8_t>(v);
            }
            break;
    }
}

void BerlinMode::BehaviorScreen::render(Display& d) const {
    const BerlinParams& p = mode_.editParams();
    char buf[12];
    drawBerlinParamCell(d, 0, "BEHAVIOR", behaviorName(p.behavior));
    snprintf(buf, sizeof buf, "%d%%", p.morph);
    drawBerlinParamCell(d, 1, "MORPH",    buf);
    snprintf(buf, sizeof buf, "%d", p.evolveRate);
    drawBerlinParamCell(d, 2, "EVOLVE",   buf,
                        p.behavior != BerlinBehavior::Evolve);
    snprintf(buf, sizeof buf, "%d", p.gateLen);
    drawBerlinParamCell(d, 3, "GATELEN",  buf,
                        p.algorithm != BerlinAlgorithm::GatePitchPhasing);
    drawBerlinParamDividers(d);
    drawBerlinPianoRoll(d, mode_.engine().sequence(), mode_.engine().playhead(),
                        mode_.engine().soundingNote(), color::Green);
}

} // namespace core
