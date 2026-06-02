#pragma once

#include <cstdint>

#include "core/app/AppServices.h"
#include "core/app/Mode.h"

namespace core {

class Display;
class MidiOutput;
struct MidiMessage;

// The runtime. Hosts a fixed array of modes, routes hardware input to the
// active mode/screen, runs the mode-change overlay, owns tempo + transport.
class AppShell : public AppServices {
public:
    static constexpr int      kMaxModes        = 12;
    static constexpr uint32_t kOverlayTimeoutMs = 3000;

    void addMode(Mode* mode);          // call once per mode before begin()
    void setMidiOutput(MidiOutput* o); // for transport realtime messages
    void begin();                      // enters mode 0

    // Hardware input. Encoder index 1..5, latch index 1..3.
    void onEncoderKnob(int index, int delta);
    void onEncoderSw(int index);
    void onLatch(int index, bool on);
    void onMidiIn(const MidiMessage& msg);

    void tick(uint32_t nowMs);
    void render(Display& d);

    // AppServices
    uint16_t bpm() const override { return bpm_; }
    void     setBpm(uint16_t bpm) override;
    Transport transport() const override { return transport_; }

    // Inspectors for tests.
    int activeModeIndex() const { return activeMode_; }
    int activeScreenIndex() const { return screenIndex_; }
    bool overlayOpen() const { return overlayOpen_; }
    int overlayChoice() const { return overlayChoice_; }

private:
    enum class TransportState { Stopped, Playing, Paused };

    Mode*  modes_[kMaxModes] = {};
    int    modeCount_ = 0;
    int    activeMode_ = 0;
    int    screenIndex_ = 0;

    bool     overlayOpen_ = false;
    int      overlayChoice_ = 0;
    uint32_t overlayLastInputMs_ = 0;

    MidiOutput*    out_ = nullptr;
    uint16_t       bpm_ = 120;
    TransportState transportState_ = TransportState::Stopped;
    Transport      transport_ = Transport::Stop;
    bool           lastLatchOn_[4] = {};   // 1-based; [0] unused

    uint32_t nowMs_ = 0;

    Screen& activeScreen();
    void switchScreen(int delta);
    void enterMode(int index);
    void fireRaw(const RawInput& in);
    void applyTransport(Transport t);
    void drawTopBar(Display& d) const;
    void drawOverlay(Display& d) const;
};

} // namespace core
