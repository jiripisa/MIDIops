#include "core/modes/SettingsMode.h"

#include <cstdio>

#include "core/Display.h"
#include "core/Scale.h"
#include "core/render/ParamGrid.h"

namespace {

const char* scaleTypeName(core::Scale::Type t) {
    switch (t) {
        case core::Scale::Type::Major:      return "Maj";
        case core::Scale::Type::Minor:      return "Min";
        case core::Scale::Type::Aug:        return "Aug";
        case core::Scale::Type::Dim:        return "Dim";
        case core::Scale::Type::PentaMajor: return "Pent+";
        case core::Scale::Type::PentaMinor:    return "Pent-";
        case core::Scale::Type::Dorian:        return "Dor";
        case core::Scale::Type::Phrygian:      return "Phr";
        case core::Scale::Type::HarmonicMinor: return "HMin";
        default:                               return "?";
    }
}

const char* rootName(uint8_t pc) {
    static const char* kNote[12] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
    return kNote[pc % 12];
}

const char* clockName(core::ClockSource s) {
    return s == core::ClockSource::External ? "Ext" : "Int";
}

const char* transportName(core::TransportMode m) {
    switch (m) {
        case core::TransportMode::Send:    return "Send";
        case core::TransportMode::Receive: return "Recv";
        default:                           return "Off";
    }
}

} // namespace

namespace core {

SettingsMode::SettingsMode(AppServices& svc) : svc_(svc) {}

// ---------------------------------------------------------------------------
// MidiScreen
// ---------------------------------------------------------------------------

void SettingsMode::MidiScreen::onEncoder(int index, int delta) {
    switch (index) {
        case 1: {
            int v = svc_.midiOutChannel() + delta;
            if (v < 1)  v = 1;
            if (v > 16) v = 16;
            svc_.setMidiOutChannel(static_cast<uint8_t>(v));
            break;
        }
        case 2: {
            int v = static_cast<int>(svc_.midiInChannel()) + delta;
            if (v < 0)  v = 0;
            if (v > 16) v = 16;
            svc_.setMidiInChannel(static_cast<uint8_t>(v));
            break;
        }
        case 3: {
            if (delta != 0)
                svc_.setClockSource(svc_.clockSource() == ClockSource::Internal
                                        ? ClockSource::External
                                        : ClockSource::Internal);
            break;
        }
        case 4: {
            if (delta != 0)
                svc_.setTransportMode(cycleEnum(svc_.transportMode(), delta));
            break;
        }
        default:
            break;
    }
}

void SettingsMode::MidiScreen::render(Display& d) const {
    drawParamGridDividers(d);

    // Cell (0,0): MIDI output channel
    char outBuf[8];
    snprintf(outBuf, sizeof outBuf, "%d", svc_.midiOutChannel());
    drawParamCell(d, 0, 0, "OUT CH", outBuf);

    // Cell (1,0): MIDI input channel (0 = OMNI)
    char inBuf[8];
    const char* inVal;
    if (svc_.midiInChannel() == 0) {
        inVal = "OMNI";
    } else {
        snprintf(inBuf, sizeof inBuf, "%d", svc_.midiInChannel());
        inVal = inBuf;
    }
    drawParamCell(d, 1, 0, "IN CH", inVal);

    // Cell (0,1): clock source
    drawParamCell(d, 0, 1, "CLOCK", clockName(svc_.clockSource()));

    // Cell (1,1): transport mode
    drawParamCell(d, 1, 1, "TRNSPT", transportName(svc_.transportMode()));
}

// ---------------------------------------------------------------------------
// ScaleScreen
// ---------------------------------------------------------------------------

void SettingsMode::ScaleScreen::onEncoder(int index, int delta) {
    switch (index) {
        case 1:
            svc_.setScaleType(cycleEnum(svc_.scale().type(), delta));
            break;
        case 2: {
            int pc = (static_cast<int>(svc_.scale().root()) + delta % 12 + 12) % 12;
            svc_.setScaleRoot(static_cast<uint8_t>(pc));
            break;
        }
        default:
            break;
    }
}

void SettingsMode::ScaleScreen::render(Display& d) const {
    drawParamGridDividers(d);
    drawParamCell(d, 0, 0, "SCALE", scaleTypeName(svc_.scale().type()));
    drawParamCell(d, 1, 0, "ROOT",  rootName(svc_.scale().root()));
}

// ---------------------------------------------------------------------------
// SystemScreen
// ---------------------------------------------------------------------------

void SettingsMode::SystemScreen::onEncoderSw(int index) {
    if (index != 1) return;
    if (state_ == ResetState::Armed) {
        svc_.factoryReset();
        state_ = ResetState::Done;
    } else {
        state_ = ResetState::Armed;
    }
    stateMs_ = nowMs_;
}

void SettingsMode::SystemScreen::update(uint32_t nowMs) {
    nowMs_ = nowMs;
    if (state_ == ResetState::Armed && nowMs_ - stateMs_ >= kArmWindowMs)
        state_ = ResetState::Idle;
    if (state_ == ResetState::Done && nowMs_ - stateMs_ >= kDoneShowMs)
        state_ = ResetState::Idle;
}

void SettingsMode::SystemScreen::render(Display& d) const {
    drawParamGridDividers(d);
    const char* value = state_ == ResetState::Armed ? "SURE?"
                      : state_ == ResetState::Done  ? "DONE"
                                                    : "RESET";
    drawParamCell(d, 0, 0, "FACTORY", value);
    const char* hint = state_ == ResetState::Armed
                           ? "press E1 again to confirm"
                           : "press E1 to factory-reset";
    d.drawText(8, 200, hint, color::Gray, color::Black, 1);
}

} // namespace core
