#include "core/modes/BpmMode.h"

#include <cstdio>

#include "core/Display.h"

namespace core {

BpmMode::BpmMode(AppServices& svc) : svc_(svc) {}

void BpmMode::BpmScreen::render(Display& d) const {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%u", svc_.bpm());
    // Big number centred-ish; size 6 ~ 30x42 px per glyph.
    d.drawText(90, 90, buf, color::Cyan, color::Black, 6);
    d.drawText(140, 150, "BPM", color::White, color::Black, 2);
}

} // namespace core
