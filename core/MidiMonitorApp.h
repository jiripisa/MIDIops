#pragma once

#include <cstddef>
#include <cstdint>

#include "MidiMessage.h"

namespace core {

class Display;

// Note-focused MIDI monitor: a piano keyboard at the bottom of the screen
// lights up under pressed notes, and "worms" (piano-roll bars) scroll upward
// from the keyboard while notes are held. Non-note channel-voice messages
// are accepted but not rendered.
class MidiMonitorApp {
public:
    // ============================================================
    //  Change this to listen on a different MIDI channel.
    //   0      = OMNI (accept all channels)
    //   1..16  = a single channel
    // ============================================================
    static constexpr uint8_t kDefaultChannel = 0;

    MidiMonitorApp();

    void    setChannel(uint8_t channel);
    uint8_t channel() const { return channel_; }

    // Increments / decrements the listened channel by `delta` detents.
    // Range is [0..16] where 0 = OMNI; values are clamped at both ends so
    // turning past the limits is a no-op. Clears the visible note/worm
    // state on actual change.
    void onChannelKnob(int delta);

    // Master monitoring switch. When OFF, incoming MIDI is ignored and the
    // held-note + worm state is cleared so the screen reflects "muted".
    void setMonitoring(bool on);
    void toggleMonitoring() { setMonitoring(!monitoring_); }
    bool monitoring() const { return monitoring_; }

    void onMessage(const MidiMessage& msg);

    // Advances the scrolling animation. Caller passes a monotonic millisecond
    // clock (millis() on Teensy, SDL_GetTicks() on host). Safe to call as
    // often as you like — internal accumulator keeps a constant scroll speed.
    void tick(uint32_t nowMs);

    void render(Display& display);

private:
    // ---- Layout (320x240 logical pixels) -----------------------------
    static constexpr int kScreenW     = 320;
    static constexpr int kScreenH     = 240;
    static constexpr int kHeaderH     = 20;
    static constexpr int kRollTop     = 22;       // inclusive upper edge
    static constexpr int kRollBottom  = 180;      // exclusive lower edge
    static constexpr int kKeyboardTop = 180;
    static constexpr int kKeyboardBot = 240;
    static constexpr int kBlackKeyH   = 32;

    // Keyboard window. Must start on a C and end on a B.
    static constexpr uint8_t kLowestNote  = 36;   // C2
    static constexpr uint8_t kHighestNote = 83;   // B5
    static constexpr int     kWhiteKeysVisible = 28;          // 4 octaves
    static constexpr int     kWhiteKeyW = 11;                 // 28 * 11 = 308
    static constexpr int     kBlackKeyW = 7;
    static constexpr int     kKeyboardX0 =
        (kScreenW - kWhiteKeysVisible * kWhiteKeyW) / 2;

    // Scroll speed for the worm roll.
    static constexpr uint32_t kScrollPxPerSec = 50;

    // ---- State -------------------------------------------------------
    struct Worm {
        bool    live    = false;
        bool    growing = false;
        uint8_t note    = 0;
        uint8_t channel = 0;
        int16_t topY    = 0;   // smaller y == higher on screen
        int16_t bottomY = 0;
    };
    static constexpr int kMaxWorms = 64;

    struct KeyRect { int x; int w; bool isBlack; };

    uint8_t  channel_                = kDefaultChannel;
    bool     monitoring_             = true;

    // Per-note bitmap of which channels are currently holding it. Bit (ch-1)
    // is set when channel `ch` (1..16) is sustaining MIDI note `n`. This
    // tracks all 16 channels independently — required for per-channel chord
    // detection and for correct keyboard highlighting when more than one
    // channel plays the same note simultaneously.
    uint16_t notePressedBy_[128]     = {};

    Worm     worms_[kMaxWorms]       {};
    uint32_t lastTickMs_             = 0;
    uint32_t scrollAccumMs_          = 0;

    // ---- Helpers -----------------------------------------------------
    void onNoteOn (const MidiMessage& msg);
    void onNoteOff(const MidiMessage& msg);
    void advanceWorms(int dy);

    void drawHeader(Display& d) const;
    void drawWorms(Display& d)  const;
    void drawKeyboard(Display& d) const;

    static bool     isBlackPc(int pc);
    static int      whiteKeyIdx(uint8_t note);   // white-key index from kLowestNote
    static uint8_t  whiteKeyAt(int idx);
    static KeyRect  keyRectFor(uint8_t note);
    static uint16_t channelColor(uint8_t channel);
    static bool     noteVisible(uint8_t note) {
        return note >= kLowestNote && note <= kHighestNote;
    }

    // Lowest-numbered channel currently holding `note`, or 0 if none.
    // Used by the keyboard-highlight code to pick a fill colour when more
    // than one channel is sustaining the same note.
    uint8_t pressedChannelFor(uint8_t note) const;

    // Writes a chord name like "Cmaj7", "Am", "G/B" to `out` when channel
    // `ch` is sustaining 3+ notes that match a known triad/seventh.
    // Writes an empty string otherwise.
    void detectChordOnChannel(uint8_t ch,
                              char* out, std::size_t outSize) const;

    void drawChordNames(Display& d) const;
};

} // namespace core
