#pragma once

#include "core/app/AppServices.h"
#include "core/app/Mode.h"

namespace core {

// Big current-tempo readout. Enc1 adjusts BPM via AppServices. One screen.
class BpmMode : public Mode {
public:
    explicit BpmMode(AppServices& svc);
    const char* name() const override { return "BPM"; }
    int     screenCount() const override { return 1; }
    Screen& screen(int) override { return screen_; }

private:
    AppServices& svc_;

    class BpmScreen : public Screen {
    public:
        explicit BpmScreen(AppServices& s) : svc_(s) {}
        const char* name() const override { return "tempo"; }
        void onEncoder(int index, int delta) override {
            if (index == 1) {
                int v = static_cast<int>(svc_.bpm()) + delta;
                if (v < 30)  v = 30;
                if (v > 300) v = 300;
                svc_.setBpm(static_cast<uint16_t>(v));
            }
        }
        void render(Display& d) const override;
    private:
        AppServices& svc_;
    };
    BpmScreen screen_{svc_};
};

} // namespace core
