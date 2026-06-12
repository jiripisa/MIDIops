#pragma once

#include "core/app/AppServices.h"
#include "core/app/Mode.h"
#include "core/render/ModeIcons.h"

namespace core {

class SettingsMode : public Mode {
public:
    explicit SettingsMode(AppServices& svc);
    const char* name() const override { return "Settings"; }
    const uint16_t* icon() const override { return icons::kSettings; }
    int     screenCount() const override { return 3; }
    Screen& screen(int i) override {
        if (i == 0) return midiScreen_;
        if (i == 1) return scaleScreen_;
        return systemScreen_;
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
    // Two-step factory reset on Enc1 press: Idle -> Armed ("SURE?", 3 s
    // window) -> reset + brief "DONE". The window expiring drops back to
    // Idle, so a stray later press only re-arms. Rotation does nothing here.
    class SystemScreen : public Screen {
    public:
        static constexpr uint32_t kArmWindowMs = 3000;
        static constexpr uint32_t kDoneShowMs  = 1500;
        explicit SystemScreen(AppServices& s) : svc_(s) {}
        const char* name() const override { return "system"; }
        void onEncoderSw(int index) override;
        void update(uint32_t nowMs) override;
        void render(Display& d) const override;
    private:
        enum class ResetState : uint8_t { Idle, Armed, Done };
        AppServices& svc_;
        ResetState   state_   = ResetState::Idle;
        uint32_t     nowMs_   = 0;
        uint32_t     stateMs_ = 0;
    };
    MidiScreen   midiScreen_{svc_};
    ScaleScreen  scaleScreen_{svc_};
    SystemScreen systemScreen_{svc_};
};

} // namespace core
