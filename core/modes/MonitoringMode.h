#pragma once
#include "core/app/Mode.h"
#include "core/NoteWormModel.h"
#include "core/render/NotationRenderer.h"

namespace core {

class MonitoringMode : public Mode {
public:
    MonitoringMode();

    const char* name() const override { return "Monitoring"; }
    int     screenCount() const override { return 2; }
    Screen& screen(int i) override { return i == 0 ? static_cast<Screen&>(wormsScreen_) : static_cast<Screen&>(notesScreen_); }

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

    class NotesScreen : public Screen {
    public:
        explicit NotesScreen(NoteWormModel& m) : model_(m) {}
        const char* name() const override { return "notes"; }
        void update(uint32_t nowMs) override { notation_.update(model_, nowMs); }
        void render(Display& d) const override { notation_.render(model_, d); }
    private:
        NoteWormModel&   model_;
        NotationRenderer notation_;
    };

    WormsScreen wormsScreen_{model_};
    NotesScreen notesScreen_{model_};
};

} // namespace core
