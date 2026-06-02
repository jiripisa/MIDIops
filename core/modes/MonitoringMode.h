#pragma once
#include "core/app/Mode.h"
#include "core/NoteWormModel.h"

namespace core {

class MonitoringMode : public Mode {
public:
    MonitoringMode();

    const char* name() const override { return "Monitoring"; }
    int     screenCount() const override { return 1; }
    Screen& screen(int) override { return wormsScreen_; }

    void onMidiIn(const MidiMessage& msg) override;
    void update(uint32_t nowMs) override { model_.tick(nowMs); }

private:
    NoteWormModel model_;

    class WormsScreen : public Screen {
    public:
        explicit WormsScreen(NoteWormModel& m) : model_(m) {}
        const char* name() const override { return "worms"; }
        void render(Display& d) const override;
    private:
        NoteWormModel& model_;
    };

    WormsScreen wormsScreen_{model_};
};

} // namespace core
