#include "core/modes/BerlinMode.h"

#include <cstdio>

#include "core/BerlinGen.h"           // berlinGateTicks (live gate re-stamp)
#include "core/Display.h"
#include "core/MidiOutput.h"
#include "core/Presets.h"
#include "core/render/BerlinLayout.h" // drawBerlinParamCell, drawBerlinParamDividers, drawBerlinMultiRoll
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
    return st && berlinPreset2Usable(*st, slot);
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
    return saveBerlinPreset2(*st, slot, v);
}

bool BerlinMode::loadPreset(int slot) {
    Storage* st = svc_.storage();
    if (!st) return false;
    BerlinVoicePreset v[kVoices];
    if (!loadBerlinPreset2(*st, slot, v)) return false;
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
    if (generated) enforceConsonance();
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
                enforceConsonance();
            }
            break;
    }
}

// ---- Combined multi-voice piano-roll --------------------------------------

void BerlinMode::renderRoll(Display& d) const {
    BerlinRollVoice rv[kVoices];
    for (int v = 0; v < kVoices; ++v) {
        const BerlinEngine& e = voices_[v].engine;
        rv[v].seq          = &e.sequence();
        rv[v].playhead     = e.playhead();
        rv[v].soundingNote = e.soundingNote();
        rv[v].color        = kBerlinVoiceColors[v];
        rv[v].muted        = e.muted();
        rv[v].edited       = (v == editVoice_);
        rv[v].name         = kBerlinVoiceNames[v];
    }
    drawBerlinMultiRoll(d, rv, kVoices);
}

// ---- Voices screen (mixer) ------------------------------------------------

constexpr int kMutedLabelDy = 52;   // below the cell's value text

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
        drawBerlinParamCell(d, v, kBerlinVoiceNames[v], buf, muted);
        if (muted)
            d.drawText(v * kBerlinCellW + 4, kBerlinParamTop + kMutedLabelDy, "MUTED",
                       color::Gray, color::Black, 1);
    }
    drawBerlinParamDividers(d);
    mode_.renderRoll(d);
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
    const bool bass = mode_.editVoice() == kBass;
    switch (index) {
        case 1:  // Algorithm — Mid/High only; applies at the next Generate.
            if (!bass) p.algorithm = cycleEnum(p.algorithm, delta);
            break;
        case 2: { int v = p.length + delta; if (v < 3) v = 3; if (v > 32) v = 32;
                  p.length = static_cast<uint8_t>(v);
                  if (mode_.live()) { mode_.editEngine().setParams(p); mode_.editEngine().applyLiveLength(); }
                } break;
        case 3: { int v = p.density + delta * 5; if (v < 0) v = 0; if (v > 100) v = 100;
                  p.density = static_cast<uint8_t>(v);
                  if (mode_.live()) { mode_.editEngine().setParams(p); mode_.editEngine().applyLiveDensity(); }
                } break;
        case 4:  // AlgoPrm: Scatter under Walk, GateLen under Phase; locked for
                 // Bass and under Degree. Applies at the next Generate.
            if (bass) break;
            if (p.algorithm == BerlinAlgorithm::DrunkardWalk) {
                int v = p.scatter + delta; if (v < 1) v = 1; if (v > 7) v = 7;
                p.scatter = static_cast<uint8_t>(v);
            } else if (p.algorithm == BerlinAlgorithm::GatePitchPhasing) {
                int v = p.gateLen + delta; if (v < 3) v = 3; if (v > 16) v = 16;
                p.gateLen = static_cast<uint8_t>(v);
            }
            break;
    }
}

void BerlinMode::StructureScreen::onEncoderSw(int index) { mode_.onVoiceScreenPress(index); }

void BerlinMode::StructureScreen::render(Display& d) const {
    const int active = mode_.editVoice();
    // Build all three voices' values per cell; drawBerlinVoiceCell stacks them
    // Bass / Mid / High with the active voice highlighted.
    char algo[kVoices][12], len[kVoices][8], dens[kVoices][8], aprm[kVoices][8];
    for (int v = 0; v < kVoices; ++v) {
        const BerlinParams& p = mode_.voices_[v].params;
        // Bass always uses its own anchor generator: its algorithm is fixed.
        snprintf(algo[v], sizeof algo[v], "%s",
                 v == kBass ? "Bass" : algoName(p.algorithm));
        snprintf(len[v],  sizeof len[v],  "%d", p.length);
        snprintf(dens[v], sizeof dens[v], "%d%%", p.density);
        if (v != kBass && p.algorithm == BerlinAlgorithm::DrunkardWalk)
            snprintf(aprm[v], sizeof aprm[v], "%d", p.scatter);
        else if (v != kBass && p.algorithm == BerlinAlgorithm::GatePitchPhasing)
            snprintf(aprm[v], sizeof aprm[v], "%d", p.gateLen);
        else
            snprintf(aprm[v], sizeof aprm[v], "-");
    }
    // The AlgoPrm cell name follows the active voice's algorithm (what Enc4
    // edits): Scatter under Walk, GateLen under Phase, else generic.
    const BerlinParams& ep = mode_.voices_[active].params;
    const char* aprmName =
        (active != kBass && ep.algorithm == BerlinAlgorithm::DrunkardWalk)     ? "SCATTER"
      : (active != kBass && ep.algorithm == BerlinAlgorithm::GatePitchPhasing) ? "GATELEN"
                                                                               : "ALGOPRM";
    drawBerlinVoiceCell(d, 0, "ALGO",    algo[0], algo[1], algo[2], active);
    drawBerlinVoiceCell(d, 1, "LENGTH",  len[0],  len[1],  len[2],  active);
    drawBerlinVoiceCell(d, 2, "DENSITY", dens[0], dens[1], dens[2], active);
    drawBerlinVoiceCell(d, 3, aprmName,  aprm[0], aprm[1], aprm[2], active);
    drawBerlinParamDividers(d);
    mode_.renderRoll(d);
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

void BerlinMode::CharacterScreen::onEncoderSw(int index) { mode_.onVoiceScreenPress(index); }

void BerlinMode::CharacterScreen::render(Display& d) const {
    const int active = mode_.editVoice();
    char gate[kVoices][8], tens[kVoices][8], oct[kVoices][8], rng[kVoices][8];
    for (int v = 0; v < kVoices; ++v) {
        const BerlinParams& p = mode_.voices_[v].params;
        snprintf(gate[v], sizeof gate[v], "%d%%", p.gatePercent);
        snprintf(tens[v], sizeof tens[v], "%d%%", p.tension);
        octaveLabel(p.octaveBase, oct[v], sizeof oct[v]);
        snprintf(rng[v],  sizeof rng[v],  "%d", p.octaveRange);
    }
    drawBerlinVoiceCell(d, 0, "GATE",    gate[0], gate[1], gate[2], active);
    drawBerlinVoiceCell(d, 1, "TENSION", tens[0], tens[1], tens[2], active);
    drawBerlinVoiceCell(d, 2, "OCT",     oct[0],  oct[1],  oct[2],  active);
    drawBerlinVoiceCell(d, 3, "RANGE",   rng[0],  rng[1],  rng[2],  active);
    drawBerlinParamDividers(d);
    mode_.renderRoll(d);
}

// ---- Dynamics screen --------------------------------------------------------

void BerlinMode::DynamicsScreen::onEncoder(int index, int delta) {
    BerlinParams& g = mode_.voices_[0].params;             // canonical globals
    switch (index) {
        case 1: { int v = g.velocityBase + delta;     if (v < 1) v = 1;   if (v > 126) v = 126;
                  g.velocityBase = static_cast<uint8_t>(v); } break;
        case 2: { int v = g.velocityHumanize + delta; if (v < 0) v = 0;   if (v > 30)  v = 30;
                  g.velocityHumanize = static_cast<uint8_t>(v); } break;
        case 3: { int v = g.accent + delta;           if (v < 0) v = 0;   if (v > 27)  v = 27;
                  g.accent = static_cast<uint8_t>(v); } break;
        case 4:  // Resolution is global (one step grid) and inherently live.
            g.resolution = cycleEnum(g.resolution, delta);
            break;
        default: return;
    }
    mode_.syncGlobals();
    // Velocity knobs re-stamp at once in every behavior; resolution re-stamps
    // the baked gateTicks so the roll widths match. Doing both is harmless.
    for (int v = 0; v < kVoices; ++v) {
        berlinStampVelocities(mode_.voices_[v].engine.sequenceMut(),
                              mode_.voices_[v].params);
        restampGateTicks(mode_.voices_[v].engine, mode_.voices_[v].params);
    }
}

void BerlinMode::DynamicsScreen::render(Display& d) const {
    const BerlinParams& g = mode_.voices_[0].params;
    char buf[12];
    snprintf(buf, sizeof buf, "%d", g.velocityBase);
    drawBerlinParamCell(d, 0, "VEL",   buf);
    snprintf(buf, sizeof buf, "+-%d", g.velocityHumanize);
    drawBerlinParamCell(d, 1, "HUMAN", buf);
    snprintf(buf, sizeof buf, "+%d", g.accent);
    drawBerlinParamCell(d, 2, "ACCENT", buf);
    drawBerlinParamCell(d, 3, "RESOL",
                        g.resolution == BerlinResolution::Sixteenth ? "16th" : "8th");
    drawBerlinParamDividers(d);
    mode_.renderRoll(d);
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
    BerlinParams& g = mode_.voices_[0].params;
    switch (index) {
        case 1: g.behavior = cycleEnum(g.behavior, delta); break;
        case 2: { int v = g.morph + delta * 5; if (v < 0) v = 0; if (v > 100) v = 100;
                  g.morph = static_cast<uint8_t>(v); } break;
        case 3:
            if (g.behavior == BerlinBehavior::Evolve) {
                int v = g.evolveRate + delta; if (v < 1) v = 1; if (v > 8) v = 8;
                g.evolveRate = static_cast<uint8_t>(v);
            }
            break;
        default: return;
    }
    mode_.syncGlobals();
}

void BerlinMode::BehaviorScreen::render(Display& d) const {
    const BerlinParams& g = mode_.voices_[0].params;
    char buf[12];
    drawBerlinParamCell(d, 0, "BEHAVIOR", behaviorName(g.behavior));
    snprintf(buf, sizeof buf, "%d%%", g.morph);
    drawBerlinParamCell(d, 1, "MORPH",    buf);
    snprintf(buf, sizeof buf, "%d", g.evolveRate);
    drawBerlinParamCell(d, 2, "EVOLVE",   buf, g.behavior != BerlinBehavior::Evolve);
    drawBerlinParamCell(d, 3, "-", "-", true);             // unused
    drawBerlinParamDividers(d);
    mode_.renderRoll(d);
}

} // namespace core
