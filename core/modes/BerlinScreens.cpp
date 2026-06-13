#include "core/modes/BerlinMode.h"

#include <cstdio>

#include "core/BerlinGen.h"           // berlinGateTicks (live gate re-stamp), berlinStampVelocities
#include "core/Display.h"
#include "core/render/BerlinLayout.h" // drawBerlinParamCell, drawBerlinVoiceCell, drawBerlinParamDividers
#include "core/render/ParamGrid.h"   // cycleEnum

namespace core {

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
    const char* algoP[kVoices]; const char* lenP[kVoices];
    const char* densP[kVoices]; const char* aprmP[kVoices];
    for (int v = 0; v < kVoices; ++v) {
        algoP[v] = algo[v]; lenP[v] = len[v]; densP[v] = dens[v]; aprmP[v] = aprm[v];
    }
    drawBerlinVoiceCell(d, 0, "ALGO",    algoP, kVoices, active);
    drawBerlinVoiceCell(d, 1, "LENGTH",  lenP,  kVoices, active);
    drawBerlinVoiceCell(d, 2, "DENSITY", densP, kVoices, active);
    drawBerlinVoiceCell(d, 3, aprmName,  aprmP, kVoices, active);
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
    const char* gateP[kVoices]; const char* tensP[kVoices];
    const char* octP[kVoices];  const char* rngP[kVoices];
    for (int v = 0; v < kVoices; ++v) {
        gateP[v] = gate[v]; tensP[v] = tens[v]; octP[v] = oct[v]; rngP[v] = rng[v];
    }
    drawBerlinVoiceCell(d, 0, "GATE",    gateP, kVoices, active);
    drawBerlinVoiceCell(d, 1, "TENSION", tensP, kVoices, active);
    drawBerlinVoiceCell(d, 2, "OCT",     octP,  kVoices, active);
    drawBerlinVoiceCell(d, 3, "RANGE",   rngP,  kVoices, active);
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
