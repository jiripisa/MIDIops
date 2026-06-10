#pragma once

#include "core/app/AppServices.h"
#include "core/app/Mode.h"

namespace core {

class SettingsMode : public Mode {
public:
    explicit SettingsMode(AppServices& svc);
    const char* name() const override { return "Settings"; }
    int     screenCount() const override { return 2; }
    Screen& screen(int i) override {
        if (i == 0) return midiScreen_;
        return scaleScreen_;
    }

private:
    AppServices& svc_;

    class MidiScreen : public Screen {
    public:
        explicit MidiScreen(AppServices& s) : svc_(s) {}
        const char* name() const override { return "midi"; }
        void onEncoder(int index, int delta) override;
        void render(Display& d) const override;
    private:
        AppServices& svc_;
    };
    class ScaleScreen : public Screen {
    public:
        explicit ScaleScreen(AppServices& s) : svc_(s) {}
        const char* name() const override { return "scale"; }
        void onEncoder(int index, int delta) override;
        void render(Display& d) const override;
    private:
        AppServices& svc_;
    };
    MidiScreen  midiScreen_{svc_};
    ScaleScreen scaleScreen_{svc_};
};

} // namespace core
