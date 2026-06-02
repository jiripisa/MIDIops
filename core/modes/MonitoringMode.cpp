#include "core/modes/MonitoringMode.h"
#include "core/Display.h"
#include "core/render/WormsRenderer.h"

namespace core {

MonitoringMode::MonitoringMode() = default;

void MonitoringMode::onMidiIn(const MidiMessage& msg) {
    if (msg.type == MidiType::NoteOn && msg.data2 > 0) {
        // NoteOn with velocity > 0: note on
        model_.onNoteOn(msg.channel, msg.data1);
    } else if (msg.type == MidiType::NoteOff ||
               (msg.type == MidiType::NoteOn && msg.data2 == 0)) {
        // NoteOff, or NoteOn with velocity == 0 (running-status NoteOff)
        model_.onNoteOff(msg.channel, msg.data1);
    } else if (msg.type == MidiType::ControlChange) {
        // CC 120 = All Sound Off, CC 123 = All Notes Off
        if (msg.data1 == 120 || msg.data1 == 123) {
            model_.releaseAllOnChannel(msg.channel);
        }
    }
}

void MonitoringMode::WormsScreen::render(Display& d) const {
    WormsRenderer::render(model_, d);
}

} // namespace core
