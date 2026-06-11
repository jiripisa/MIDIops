#include "core/modes/BerlinMode.h"

#include <cstdio>

#include "core/Display.h"
#include "core/MidiOutput.h"
#include "core/render/BerlinLayout.h" // drawBerlinParamCell, drawBerlinParamDividers, drawBerlinPianoRoll
#include "core/render/ParamGrid.h"   // cycleEnum

namespace core {

BerlinMode::BerlinMode(AppServices& svc) : svc_(svc) {
    applyGenerator();
    engine_.setParams(params_);
}

void BerlinMode::liveRegen() {
    if (params_.behavior != BerlinBehavior::Live) return;
    engine_.setScale(&scale_);
    engine_.setParams(params_);
    applyGenerator();
    engine_.generateFull();   // structural edit takes full effect, independent of Morph
}

void BerlinMode::applyGenerator() {
    switch (params_.algorithm) {
        case BerlinAlgorithm::DegreeWeighted:   engine_.setGenerator(&degreeGen_);  break;
        case BerlinAlgorithm::GatePitchPhasing: engine_.setGenerator(&phasingGen_); break;
        case BerlinAlgorithm::DrunkardWalk:
        default:                                engine_.setGenerator(&walkGen_);    break;
    }
}

Screen& BerlinMode::screen(int i) {
    if (i == 1) return characterScreen_;
    if (i == 2) return dynamicsScreen_;
    if (i == 3) return behaviorScreen_;
    return structureScreen_;
}

void BerlinMode::onEnter() {
    scale_ = svc_.scale();
    engine_.setScale(&scale_);
    engine_.setParams(params_);
    engine_.setOutChannel(svc_.midiOutChannel());
    applyGenerator();
    for (int i = 0; i < 4; ++i) latchSynced_[i] = false;  // re-absorb first delivery per index
    if (!engine_.sequence().step(0).active) engine_.generate();  // ensure something to show/play
}

void BerlinMode::update(uint32_t /*nowMs*/) {
    scale_ = svc_.scale();
    engine_.setScale(&scale_);
    engine_.setParams(params_);
    applyGenerator();
    engine_.setOutChannel(svc_.midiOutChannel());
}

void BerlinMode::onRawInput(const RawInput& in) {
    if (in.kind != RawInput::Kind::Latch) return;     // encoders go via Screen
    if (in.index < 1 || in.index > 3) return;
    // The shell delivers the latch level every main-loop frame, so we act on a
    // state CHANGE, not the level. Play/Pause is level-driven (the switch
    // position is the run state). Stop and Generate are momentary actions on a
    // latching switch, so they fire on EITHER flip (up or down) — one toggle =
    // one action, instead of needing an off-then-on to re-trigger.
    // First delivery per index after onEnter() is absorbed: record the level so
    // a stale shadow across mode re-entry cannot fire a phantom flip action that
    // (for Latch3) would regenerate and destroy a Locked sequence. Latch1 is
    // LEVEL-driven (the switch position IS the run state), so it still adopts
    // the switch position on the sync frame; Latch2/Latch3 (flip-triggered) take
    // no action on that frame.
    const bool firstDelivery = !latchSynced_[in.index];
    const bool changed = in.on != lastLatch_[in.index];
    lastLatch_[in.index] = in.on;
    latchSynced_[in.index] = true;
    const bool flip = changed && !firstDelivery;
    switch (in.index) {
        case 1: in.on ? engine_.play() : engine_.pause(); break;  // Play/Pause (level)
        case 2: if (flip) engine_.stop();                 break;  // Stop (any flip)
        case 3: if (flip) { engine_.setParams(params_); applyGenerator(); engine_.generate(); } break;  // Reset/Generate (any flip)
    }
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

void BerlinMode::StructureScreen::onEncoder(int index, int delta) {
    BerlinParams& p = mode_.params_;
    switch (index) {
        case 1: p.algorithm = cycleEnum(p.algorithm, delta); break;
        case 2: { int v = p.length + delta; if (v < 3) v = 3; if (v > 32) v = 32;
                  p.length = static_cast<uint8_t>(v); } break;
        case 3: p.resolution = cycleEnum(p.resolution, delta); break;
        case 4: { int v = p.density + delta * 5; if (v < 0) v = 0; if (v > 100) v = 100;
                  p.density = static_cast<uint8_t>(v); } break;
    }
    mode_.liveRegen();  // all structure params are structural
}

void BerlinMode::StructureScreen::render(Display& d) const {
    const BerlinParams& p = mode_.params_;
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
    BerlinParams& p = mode_.params_;
    bool structural = false;
    switch (index) {
        case 1: { int v = p.gatePercent + delta;     if (v < 40) v = 40; if (v > 99) v = 99;
                  p.gatePercent = static_cast<uint8_t>(v); } break;
        case 2: { int v = p.tension + delta * 5;     if (v < 0) v = 0;  if (v > 100) v = 100;
                  p.tension = static_cast<uint8_t>(v); structural = true; } break;
        case 3: { int v = p.octaveBase + delta * 12; if (v < 24) v = 24; if (v > 72) v = 72;
                  p.octaveBase = static_cast<uint8_t>(v); structural = true; } break;
        case 4: { int v = p.octaveRange + delta;     if (v < 1) v = 1;  if (v > 3) v = 3;
                  p.octaveRange = static_cast<uint8_t>(v); structural = true; } break;
    }
    if (structural) mode_.liveRegen();
}

void BerlinMode::CharacterScreen::render(Display& d) const {
    const BerlinParams& p = mode_.params_;
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
    BerlinParams& p = mode_.params_;
    switch (index) {
        case 1: {
            int v = p.velocityBase + delta;
            if (v < 1)   v = 1;
            if (v > 126) v = 126;
            p.velocityBase = static_cast<uint8_t>(v);
            break;
        }
        case 2: {
            int v = p.velocityHumanize + delta;
            if (v < 0)  v = 0;
            if (v > 30) v = 30;
            p.velocityHumanize = static_cast<uint8_t>(v);
            break;
        }
        case 3: {
            int v = p.accent + delta;
            if (v < 0)  v = 0;
            if (v > 27) v = 27;
            p.accent = static_cast<uint8_t>(v);
            break;
        }
        case 4:
            if (p.algorithm == BerlinAlgorithm::DrunkardWalk) {
                int v = p.scatter + delta;
                if (v < 1) v = 1;
                if (v > 7) v = 7;
                p.scatter = static_cast<uint8_t>(v);
            } else if (p.algorithm == BerlinAlgorithm::GatePitchPhasing) {
                int v = p.gateLen + delta;
                if (v < 3)  v = 3;
                if (v > 16) v = 16;
                p.gateLen = static_cast<uint8_t>(v);
            }
            mode_.liveRegen();  // scatter and gateLen are structural
            break;
    }
}

void BerlinMode::DynamicsScreen::render(Display& d) const {
    const BerlinParams& p = mode_.params_;
    char buf[12];
    snprintf(buf, sizeof buf, "%d", p.velocityBase);
    drawBerlinParamCell(d, 0, "VEL",   buf);
    snprintf(buf, sizeof buf, "+-%d", p.velocityHumanize);
    drawBerlinParamCell(d, 1, "HUMAN", buf);
    snprintf(buf, sizeof buf, "+%d", p.accent);
    drawBerlinParamCell(d, 2, "ACCENT", buf);
    if (p.algorithm == BerlinAlgorithm::DrunkardWalk) {
        snprintf(buf, sizeof buf, "%d", p.scatter);
        drawBerlinParamCell(d, 3, "SCATTER", buf);
    } else if (p.algorithm == BerlinAlgorithm::GatePitchPhasing) {
        snprintf(buf, sizeof buf, "%d", p.gateLen);
        drawBerlinParamCell(d, 3, "GATELEN", buf);
    } else {
        drawBerlinParamCell(d, 3, "-", "-");
    }
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
    BerlinParams& p = mode_.params_;
    switch (index) {
        case 1: p.behavior = cycleEnum(p.behavior, delta); break;
        case 2: { int v = p.morph + delta * 5;     if (v < 0) v = 0;  if (v > 100) v = 100;
                  p.morph = static_cast<uint8_t>(v); } break;
        case 3: { int v = p.evolveRate + delta;    if (v < 1) v = 1;  if (v > 8) v = 8;
                  p.evolveRate = static_cast<uint8_t>(v); } break;
        case 4: break;  // unused
    }
}

void BerlinMode::BehaviorScreen::render(Display& d) const {
    const BerlinParams& p = mode_.params_;
    char buf[12];
    drawBerlinParamCell(d, 0, "BEHAVIOR", behaviorName(p.behavior));
    snprintf(buf, sizeof buf, "%d%%", p.morph);
    drawBerlinParamCell(d, 1, "MORPH",    buf);
    snprintf(buf, sizeof buf, "%d", p.evolveRate);
    drawBerlinParamCell(d, 2, "EVOLVE",   buf);
    drawBerlinParamCell(d, 3, "-",        "");
    drawBerlinParamDividers(d);
    drawBerlinPianoRoll(d, mode_.engine().sequence(), mode_.engine().playhead(),
                        mode_.engine().soundingNote(), color::Green);
}

} // namespace core
