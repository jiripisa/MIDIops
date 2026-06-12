#pragma once

#include <cstdint>

#include "core/ClockFollower.h"
#include "core/Scale.h"
#include "core/app/AppServices.h"
#include "core/app/Mode.h"

namespace core {

class Display;
class MidiOutput;
class Storage;
struct MidiMessage;

// The runtime. Hosts a fixed array of modes, routes hardware input to the
// active mode/screen, runs the mode-change overlay, owns tempo + transport.
class AppShell : public AppServices {
public:
    static constexpr int      kMaxModes        = 12;
    static constexpr uint32_t kOverlayTimeoutMs = 3000;
    static constexpr int      kOverlayAnimTauMs = 80;   // carousel ease time constant
    static constexpr uint32_t kSettingsSaveDebounceMs = 2000;

    void addMode(Mode* mode);          // call once per mode before begin()
    void setMidiOutput(MidiOutput* o); // for transport realtime messages
    void setStorage(Storage* s);       // optional; call before begin()
    void begin(int startMode = 0);     // enters the given mode — call exactly once

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
    const Scale& scale() const override { return scale_; }
    void setScaleType(Scale::Type t) override { scale_.setType(t); markSettingsDirty(); }
    void setScaleRoot(uint8_t pc) override { scale_.setRoot(pc); markSettingsDirty(); }
    uint8_t  midiOutChannel() const override { return midiOutChannel_; }
    void     setMidiOutChannel(uint8_t ch) override {
        if (ch >= 1 && ch <= 16) { midiOutChannel_ = ch; markSettingsDirty(); }
    }
    uint8_t  midiInChannel() const override { return midiInChannel_; }
    void     setMidiInChannel(uint8_t ch) override {
        if (ch <= 16) { midiInChannel_ = ch; markSettingsDirty(); }
    }
    ClockSource clockSource() const override { return clockSource_; }
    void     setClockSource(ClockSource s) override;
    TransportMode transportMode() const override { return transportMode_; }
    void     setTransportMode(TransportMode m) override { transportMode_ = m; markSettingsDirty(); }
    void     notifyLocalTransport(Transport t) override;
    void     factoryReset() override;
    Storage* storage() const override { return storage_; }

    // Inspectors for tests.
    int activeModeIndex() const { return activeMode_; }
    int activeScreenIndex() const { return screenIndex_; }
    bool overlayOpen() const { return overlayOpen_; }
    int overlayChoice() const { return overlayChoice_; }
    int overlayAnimPos256() const { return overlayAnimPos256_; }

private:
    enum class TransportState { Stopped, Playing, Paused };

    Mode*  modes_[kMaxModes] = {};
    int    modeCount_ = 0;
    int    activeMode_ = 0;
    int    screenIndex_ = 0;

    bool     overlayOpen_ = false;
    int      overlayChoice_ = 0;
    uint32_t overlayLastInputMs_ = 0;
    // Carousel tape position in 1/256 mode-index units, kept in
    // [0, modeCount_*256). tick() eases it toward overlayChoice_*256 along
    // the shortest wrapped path so the row slides instead of jumping.
    int      overlayAnimPos256_ = 0;

    MidiOutput*    out_ = nullptr;
    uint16_t       bpm_ = 120;
    Scale          scale_{Scale::Type::Major, 0};
    uint8_t        midiOutChannel_ = 1;
    uint8_t        midiInChannel_  = 0;     // OMNI
    ClockSource    clockSource_    = ClockSource::Internal;
    TransportMode  transportMode_  = TransportMode::Send;
    ClockFollower  clockFollower_;
    // transportState_ is the authoritative playback state (used from the
    // transport task on); transport_ holds the last command issued.
    TransportState transportState_ = TransportState::Stopped;
    Transport      transport_ = Transport::Stop;
    bool           lastLatchOn_[4] = {};   // 1-based; [0] unused
    bool           latchSeen_[4]   = {};   // 1-based; first-delivery absorb per index

    // Settings persistence: every settings setter marks dirty; tick() writes
    // once kSettingsSaveDebounceMs after the LAST change (a knob twist is a
    // single flash write). Nullptr storage = volatile settings, as before.
    Storage* storage_ = nullptr;
    bool     settingsDirty_   = false;
    uint32_t settingsDirtyMs_ = 0;

    uint32_t nowMs_ = 0;

    Screen& activeScreen();
    void switchScreen(int delta);
    void enterMode(int index);
    void fireRaw(const RawInput& in);
    void applyTransport(Transport t);
    void drawTopBar(Display& d) const;
    void drawOverlay(Display& d) const;
    void markSettingsDirty() { settingsDirty_ = true; settingsDirtyMs_ = nowMs_; }
    void loadSettings();
    void saveSettings();
};

} // namespace core
