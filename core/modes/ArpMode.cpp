#include "core/modes/ArpMode.h"

#include <cstdio>

#include "core/Display.h"
#include "core/MidiMessage.h"
#include "core/render/NotationRenderer.h"
#include "core/render/WormsRenderer.h"

namespace core {

// ---------------------------------------------------------------------------
// Helper string converters
// ---------------------------------------------------------------------------

static const char* dirName(ArpDirection d) {
    switch (d) {
        case ArpDirection::Up:     return "Up";
        case ArpDirection::Down:   return "Down";
        case ArpDirection::UpDown: return "UpDown";
        case ArpDirection::DownUp: return "DownUp";
        case ArpDirection::Random: return "Random";
        default:                   return "?";
    }
}

static const char* rateName(ArpRate r) {
    switch (r) {
        case ArpRate::Quarter:       return "1/4";
        case ArpRate::Eighth:        return "1/8";
        case ArpRate::EighthT:       return "1/8T";
        case ArpRate::Sixteenth:     return "1/16";
        case ArpRate::SixteenthT:    return "1/16T";
        case ArpRate::ThirtySecond:  return "1/32";
        default:                     return "?";
    }
}

static const char* velModeName(ArpVelocityMode v) {
    switch (v) {
        case ArpVelocityMode::Fixed:       return "Fixed";
        case ArpVelocityMode::FollowInput: return "Follow";
        case ArpVelocityMode::Accent:      return "Accent";
        default:                           return "?";
    }
}

// Cycle an enum value by `delta` steps (wraps around kCount).
template<typename E>
static E cycleEnum(E current, int delta) {
    int n = static_cast<int>(E::kCount);
    int v = (static_cast<int>(current) + delta % n + n) % n;
    return static_cast<E>(v);
}

// ---------------------------------------------------------------------------
// ArpMode
// ---------------------------------------------------------------------------

ArpMode::ArpMode(AppServices& svc) : svc_(svc) {}

Screen& ArpMode::screen(int i) {
    switch (i) {
        case 0:  return paramScreenA_;
        case 1:  return paramScreenB_;
        case 2:  return wormsScreen_;
        default: return notesScreen_;
    }
}

void ArpMode::onEnter() {
    engine_.setEcho(&ArpMode::echoThunk, this);
    engine_.setOutChannel(outChannel_);
}

void ArpMode::onExit() {
    engine_.stop();
}

void ArpMode::onMidiIn(const MidiMessage& msg) {
    if (msg.type == MidiType::NoteOn && msg.data2 > 0) {
        engine_.noteOn(msg.data1, msg.data2, lastNowMs_);
    } else if (msg.type == MidiType::NoteOff ||
               (msg.type == MidiType::NoteOn && msg.data2 == 0)) {
        engine_.noteOff(msg.data1, lastNowMs_);
    }
}

void ArpMode::onTransport(Transport t) {
    switch (t) {
        case Transport::Stop:  engine_.stop();  break;
        case Transport::Reset: engine_.reset(); break;
        default:               break;
    }
}

void ArpMode::update(uint32_t nowMs) {
    lastNowMs_ = nowMs;
    engine_.setBpm(svc_.bpm());
    engine_.setScale(&svc_.scale());
    engine_.setParams(params_);
    engine_.tick(nowMs);
    model_.tick(nowMs);
}

// static
void ArpMode::echoThunk(void* user, bool isOn,
                        uint8_t channel, uint8_t note, uint8_t /*velocity*/) {
    auto* self = static_cast<ArpMode*>(user);
    if (isOn)
        self->model_.onEngineNoteOn(channel, note);
    else
        self->model_.onEngineNoteOff(channel, note);
}

// ---------------------------------------------------------------------------
// ParamScreenA — steps / rate / gate% / direction
// ---------------------------------------------------------------------------

void ArpMode::ParamScreenA::onEncoder(int index, int delta) {
    ArpParams& p = mode_.params_;
    switch (index) {
        case 1: {
            int v = static_cast<int>(p.steps) + delta;
            if (v < 1)  v = 1;
            if (v > 16) v = 16;
            p.steps = static_cast<uint8_t>(v);
            break;
        }
        case 2:
            p.rate = cycleEnum(p.rate, delta);
            break;
        case 3: {
            int v = static_cast<int>(p.gatePercent) + delta;
            if (v < 10)  v = 10;
            if (v > 100) v = 100;
            p.gatePercent = static_cast<uint8_t>(v);
            break;
        }
        case 4:
            p.direction = cycleEnum(p.direction, delta);
            break;
        default:
            break;
    }
}

void ArpMode::ParamScreenA::render(Display& d) const {
    const ArpParams& p = mode_.params_;
    d.clear(core::color::Black);

    char buf[32];
    const int x = 10;
    const int lineH = 24;

    snprintf(buf, sizeof(buf), "Steps: %d", static_cast<int>(p.steps));
    d.drawText(x, lineH * 0, buf, core::color::White, core::color::Black, 2);

    snprintf(buf, sizeof(buf), "Rate: %s", rateName(p.rate));
    d.drawText(x, lineH * 1, buf, core::color::White, core::color::Black, 2);

    snprintf(buf, sizeof(buf), "Gate: %d%%", static_cast<int>(p.gatePercent));
    d.drawText(x, lineH * 2, buf, core::color::White, core::color::Black, 2);

    snprintf(buf, sizeof(buf), "Dir: %s", dirName(p.direction));
    d.drawText(x, lineH * 3, buf, core::color::White, core::color::Black, 2);
}

// ---------------------------------------------------------------------------
// ParamScreenB — octave / swing% / velocity mode / latch
// ---------------------------------------------------------------------------

void ArpMode::ParamScreenB::onEncoder(int index, int delta) {
    ArpParams& p = mode_.params_;
    switch (index) {
        case 1: {
            int v = static_cast<int>(p.octave) + delta;
            if (v < -2) v = -2;
            if (v >  2) v =  2;
            p.octave = static_cast<int8_t>(v);
            break;
        }
        case 2: {
            int v = static_cast<int>(p.swingPercent) + delta;
            if (v < 50) v = 50;
            if (v > 75) v = 75;
            p.swingPercent = static_cast<uint8_t>(v);
            break;
        }
        case 3:
            p.velocityMode = cycleEnum(p.velocityMode, delta);
            break;
        case 4:
            if (delta != 0) p.latch = !p.latch;
            break;
        default:
            break;
    }
}

void ArpMode::ParamScreenB::render(Display& d) const {
    const ArpParams& p = mode_.params_;
    d.clear(core::color::Black);

    char buf[32];
    const int x = 10;
    const int lineH = 24;

    snprintf(buf, sizeof(buf), "Oct: %+d", static_cast<int>(p.octave));
    d.drawText(x, lineH * 0, buf, core::color::White, core::color::Black, 2);

    snprintf(buf, sizeof(buf), "Swing: %d", static_cast<int>(p.swingPercent));
    d.drawText(x, lineH * 1, buf, core::color::White, core::color::Black, 2);

    snprintf(buf, sizeof(buf), "Vel: %s", velModeName(p.velocityMode));
    d.drawText(x, lineH * 2, buf, core::color::White, core::color::Black, 2);

    snprintf(buf, sizeof(buf), "Latch: %s", p.latch ? "On" : "Off");
    d.drawText(x, lineH * 3, buf, core::color::White, core::color::Black, 2);
}

// ---------------------------------------------------------------------------
// WormsScreen
// ---------------------------------------------------------------------------

void ArpMode::WormsScreen::render(Display& d) const {
    WormsRenderer::render(mode_.model_, d);
}

} // namespace core
