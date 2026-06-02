#pragma once

#include <string>
#include <vector>

#include "core/app/Mode.h"
#include "core/Display.h"

// A screen that records the callbacks it receives.
struct FakeScreen : core::Screen {
    std::string label;
    int enters = 0, exits = 0, renders = 0;
    std::vector<std::pair<int,int>> encoders;  // (index, delta)
    std::vector<int> sws;

    explicit FakeScreen(std::string l) : label(std::move(l)) {}
    const char* name() const override { return label.c_str(); }
    void onEnter() override { ++enters; }
    void onExit() override { ++exits; }
    void onEncoder(int i, int d) override { encoders.push_back({i, d}); }
    void onEncoderSw(int i) override { sws.push_back(i); }
    void render(core::Display&) const override {}
};

// A mode holding N fake screens; records transport + raw input.
struct FakeMode : core::Mode {
    std::string label;
    std::vector<FakeScreen*> screens;
    std::vector<core::Transport> transports;
    int rawCount = 0, midiCount = 0, enters = 0, exits = 0;

    FakeMode(std::string l, int screenN) : label(std::move(l)) {
        for (int i = 0; i < screenN; ++i)
            screens.push_back(new FakeScreen(label + ":s" + std::to_string(i)));
    }
    ~FakeMode() override { for (auto* s : screens) delete s; }

    const char* name() const override { return label.c_str(); }
    int screenCount() const override { return static_cast<int>(screens.size()); }
    core::Screen& screen(int i) override { return *screens[i]; }
    void onEnter() override { ++enters; }
    void onExit() override { ++exits; }
    void onMidiIn(const core::MidiMessage&) override { ++midiCount; }
    void onTransport(core::Transport t) override { transports.push_back(t); }
    void onRawInput(const core::RawInput&) override { ++rawCount; }
};
