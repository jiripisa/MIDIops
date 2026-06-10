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

Screen& BerlinMode::screen(int) { return structureScreen_; }

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

} // namespace core
