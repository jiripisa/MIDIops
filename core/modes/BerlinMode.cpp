#include "core/modes/BerlinMode.h"

#include <cstdio>

#include "core/Display.h"
#include "core/MidiOutput.h"
#include "core/render/ParamGrid.h"   // cycleEnum, drawParamCell

namespace core {

BerlinMode::BerlinMode(AppServices& svc) : svc_(svc) {
    engine_.setGenerator(&walkGen_);
    engine_.setParams(params_);
}

Screen& BerlinMode::screen(int i) {
    if (i == 1) return characterScreen_;
    if (i == 2) return behaviorScreen_;
    return structureScreen_;
}

void BerlinMode::onEnter() {
    scale_ = svc_.scale();
    engine_.setScale(&scale_);
    engine_.setParams(params_);
    engine_.setOutChannel(svc_.midiOutChannel());
    if (!engine_.sequence().step(0).active) engine_.generate();  // ensure something to show/play
}

void BerlinMode::update(uint32_t /*nowMs*/) {
    scale_ = svc_.scale();
    engine_.setScale(&scale_);
    engine_.setParams(params_);
    engine_.setOutChannel(svc_.midiOutChannel());
}

void BerlinMode::onRawInput(const RawInput& in) {
    if (in.kind != RawInput::Kind::Latch) return;     // encoders go via Screen
    if (in.index < 1 || in.index > 3) return;
    const bool rising = in.on && !lastLatch_[in.index];
    lastLatch_[in.index] = in.on;
    switch (in.index) {
        case 1: in.on ? engine_.play() : engine_.pause(); break;  // Play/Pause (level)
        case 2: if (rising) engine_.stop();               break;  // Stop (edge)
        case 3: if (rising) engine_.generate();           break;  // Reset/Generate (edge)
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
        case 2: { int v = p.length + delta; if (v < 3) v = 3; if (v > 16) v = 16;
                  p.length = static_cast<uint8_t>(v); } break;
        case 3: p.resolution = cycleEnum(p.resolution, delta); break;
        case 4: { int v = p.density + delta * 5; if (v < 0) v = 0; if (v > 100) v = 100;
                  p.density = static_cast<uint8_t>(v); } break;
    }
}

void BerlinMode::StructureScreen::render(Display& d) const {
    const BerlinParams& p = mode_.params_;
    char buf[12];
    drawParamCell(d, 0, 0, "ALGO", algoName(p.algorithm));
    snprintf(buf, sizeof buf, "%d", p.length);      drawParamCell(d, 1, 0, "LENGTH", buf);
    drawParamCell(d, 0, 1, "RESOL", p.resolution == BerlinResolution::Sixteenth ? "16th" : "8th");
    snprintf(buf, sizeof buf, "%d%%", p.density);   drawParamCell(d, 1, 1, "DENSITY", buf);
}

// ---- Character screen -------------------------------------------------------

// MIDI note → octave label using C-1 = MIDI 0 convention, e.g. 48 → "C3".
static void octaveLabel(uint8_t note, char* buf, int n) {
    snprintf(buf, n, "C%d", note / 12 - 1);
}

void BerlinMode::CharacterScreen::onEncoder(int index, int delta) {
    BerlinParams& p = mode_.params_;
    switch (index) {
        case 1: { int v = p.gatePercent + delta;     if (v < 40) v = 40; if (v > 99) v = 99;
                  p.gatePercent = static_cast<uint8_t>(v); } break;
        case 2: { int v = p.tension + delta * 5;     if (v < 0) v = 0;  if (v > 100) v = 100;
                  p.tension = static_cast<uint8_t>(v); } break;
        case 3: { int v = p.octaveBase + delta * 12; if (v < 24) v = 24; if (v > 72) v = 72;
                  p.octaveBase = static_cast<uint8_t>(v); } break;
        case 4: { int v = p.octaveRange + delta;     if (v < 1) v = 1;  if (v > 3) v = 3;
                  p.octaveRange = static_cast<uint8_t>(v); } break;
    }
}

void BerlinMode::CharacterScreen::render(Display& d) const {
    const BerlinParams& p = mode_.params_;
    char buf[12];
    snprintf(buf, sizeof buf, "%d%%", p.gatePercent);    drawParamCell(d, 0, 0, "GATE", buf);
    snprintf(buf, sizeof buf, "%d%%", p.tension);        drawParamCell(d, 1, 0, "TENSION", buf);
    octaveLabel(p.octaveBase, buf, sizeof buf);          drawParamCell(d, 0, 1, "OCT", buf);
    snprintf(buf, sizeof buf, "%d", p.octaveRange);      drawParamCell(d, 1, 1, "RANGE", buf);
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
    drawParamCell(d, 0, 0, "BEHAVIOR", behaviorName(p.behavior));
    snprintf(buf, sizeof buf, "%d%%", p.morph);        drawParamCell(d, 1, 0, "MORPH", buf);
    snprintf(buf, sizeof buf, "%d", p.evolveRate);     drawParamCell(d, 0, 1, "EVOLVE", buf);
    drawParamCell(d, 1, 1, "-", "");
}

} // namespace core
