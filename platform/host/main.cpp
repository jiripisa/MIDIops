// Mac (SDL) entrypoint for the MIDIops simulator.
//
//  * Opens a 960x720 SDL window (320x240 logical, scaled 3x).
//  * Creates a CoreMIDI virtual input port named "MIDIops" so external
//    apps (Ableton -> IAC Driver -> MIDIops) can drive the monitor.
//  * Maps the bottom-row keys z x c v b n m to inject local Note On/Off
//    messages (white keys C4..B4) so the UI can be verified without any
//    external MIDI source.
//  * Shift + number row (1..9) picks the MIDI channel that subsequent
//    note presses are injected on — useful to see the per-channel colour
//    palette without an external multi-channel MIDI source.
//  * The five rotary encoders are each driven by a key trio
//    {left, click, right}; see kEncoderTrios below. SPACE = Play/Pause
//    (Latch1), BACKSPACE = Stop (Latch2), RETURN = Reset (Latch3), ESC quits.

#include <SDL.h>

#include <cstdio>

#include "core/MidiMessage.h"
#include "core/app/AppShell.h"
#include "core/modes/ArpMode.h"
#include "core/modes/BerlinMode.h"
#include "core/modes/BpmMode.h"
#include "core/modes/DebugMode.h"
#include "core/modes/MonitoringMode.h"
#include "core/modes/SettingsMode.h"
#include "platform/host/RtMidiInput.h"
#include "platform/host/RtMidiOutput.h"
#include "platform/host/SdlDisplay.h"

namespace {

constexpr int kLogicalW = 320;
constexpr int kLogicalH = 240;
constexpr int kScale    = 3;

// Bottom-row white keys z x c v b n m -> C4..B4 MIDI note numbers, laid
// out like a little piano. Keyed by SDL_Keycode (the layout's letter, not
// the physical position) so a Czech QWERTZ keyboard — where Y/Z are
// swapped — still injects from the key actually labelled "z". The encoder
// trios below deliberately use scancodes instead; the two schemes don't
// overlap (no note key is a digit / minus / equals / q / w / e).
int keyToNote(SDL_Keycode k) {
    switch (k) {
        case SDLK_z: return 60; // C4
        case SDLK_x: return 62; // D4
        case SDLK_c: return 64; // E4
        case SDLK_v: return 65; // F4
        case SDLK_b: return 67; // G4
        case SDLK_n: return 69; // A4
        case SDLK_m: return 71; // B4
        default:     return -1;
    }
}

// Each rotary encoder is exercised by a trio of physical keys
// {left, click, right}. Keyed by SDL_Scancode (position, not the produced
// character) so the Czech-keyboard renderings the user picked land on the
// right keys regardless of layout: trio 1 is "+ě š", trio 2 "č ř ž",
// trio 3 "ý á í", trio 4 "é = ´", trio 5 "q w e".
enum class EncoderId { Enc1, Enc2, Enc3, Enc4, Enc5 };

struct EncoderTrio {
    SDL_Scancode left;
    SDL_Scancode click;
    SDL_Scancode right;
    EncoderId    id;
};

constexpr EncoderTrio kEncoderTrios[] = {
    {SDL_SCANCODE_1, SDL_SCANCODE_2,     SDL_SCANCODE_3,      EncoderId::Enc1},
    {SDL_SCANCODE_4, SDL_SCANCODE_5,     SDL_SCANCODE_6,      EncoderId::Enc2},
    {SDL_SCANCODE_7, SDL_SCANCODE_8,     SDL_SCANCODE_9,      EncoderId::Enc3},
    {SDL_SCANCODE_0, SDL_SCANCODE_MINUS, SDL_SCANCODE_EQUALS, EncoderId::Enc4},
    {SDL_SCANCODE_Q, SDL_SCANCODE_W,     SDL_SCANCODE_E,      EncoderId::Enc5},
};

// Dispatches a key press to the matching encoder entry point. Returns true
// if the scancode belonged to an encoder trio (so the caller can stop).
bool handleEncoderKey(core::AppShell& app, SDL_Scancode sc) {
    for (const EncoderTrio& t : kEncoderTrios) {
        int  delta = 0;
        bool click = false;
        if      (sc == t.left)  delta = -1;
        else if (sc == t.right) delta = +1;
        else if (sc == t.click) click = true;
        else continue;

        const int idx = static_cast<int>(t.id) + 1;   // Enc1->1 .. Enc5->5
        if (click) app.onEncoderSw(idx); else app.onEncoderKnob(idx, delta);
        return true;
    }
    return false;
}

} // namespace

int main(int /*argc*/, char* /*argv*/[]) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SdlDisplay   display(kLogicalW, kLogicalH, kScale, "MIDIops simulator");
    RtMidiInput  midiIn("MIDIops");
    RtMidiOutput midiOut("MIDIops Clock");
    core::AppShell       app;
    core::MonitoringMode monitoringMode;
    core::DebugMode      debugMode;
    core::BpmMode        bpmMode(app);
    core::ArpMode        arpMode(app);
    core::BerlinMode     berlinMode(app);
    core::SettingsMode   settingsMode(app);

    midiIn.begin();
    midiOut.begin();
    app.setMidiOutput(&midiOut);
    arpMode.setMidiOutput(&midiOut);
    berlinMode.setMidiOutput(&midiOut);
    app.addMode(&monitoringMode);
    app.addMode(&arpMode);
    app.addMode(&berlinMode);
    app.addMode(&bpmMode);
    app.addMode(&settingsMode);
    app.addMode(&debugMode);
    app.setBpm(120);
    app.begin();

    std::fprintf(stderr,
                 "MIDIops simulator running.\n"
                 "  Modes: 1=Monitoring  2=Arp (arpeggiates held/injected notes)\n"
                 "         3=BPM         4=Settings  5=Debug\n"
                 "  Boots into Monitoring (worms) view — MIDI in drives the worms.\n"
                 "  z x c v b n m   inject Note On/Off (white keys C4..B4)\n"
                 "  Shift + 1..9    set the test injection channel (default 1)\n"
                 "  Encoders are key trios {left, click, right}:\n"
                 "    1 2 3 (+ě š)  Enc1               left / SW / right\n"
                 "    4 5 6 (čř ž)  Enc2               left / SW / right\n"
                 "    7 8 9 (ýá í)  Enc3               left / SW / right\n"
                 "    0 - = (é= ´)  Enc4               left / SW / right\n"
                 "    q w e         Enc5 (switch screen / mode overlay)\n"
                 "  SPACE           Play / Pause  (Latch1)\n"
                 "  BACKSPACE       Stop          (Latch2)\n"
                 "  RETURN          Reset         (Latch3)\n"
                 "  ESC             quit\n");

    // Test injection state. Each held note remembers which channel it was
    // pressed on so that NoteOff goes back to the same channel even if the
    // user switches channels mid-hold.
    uint8_t injectChannel = 1;
    uint8_t notePressedOnChannel[128] = {};

    app.tick(SDL_GetTicks());
    app.render(display);

    bool running = true;
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
                case SDL_QUIT:
                    running = false;
                    break;

                case SDL_KEYDOWN: {
                    if (ev.key.keysym.sym == SDLK_ESCAPE) {
                        running = false;
                        break;
                    }
                    if (ev.key.repeat) break;

                    if (ev.key.keysym.sym == SDLK_SPACE) {
                        static bool s = false; s = !s; app.onLatch(1, s);
                        std::fprintf(stderr, "[sim] latch1 (play/pause) = %s\n", s ? "on" : "off");
                        break;
                    }

                    if (ev.key.keysym.sym == SDLK_BACKSPACE) {
                        static bool s = false; s = !s; app.onLatch(2, s);
                        std::fprintf(stderr, "[sim] latch2 (stop) = %s\n", s ? "on" : "off");
                        break;
                    }

                    if (ev.key.keysym.sym == SDLK_RETURN) {
                        static bool s = false; s = !s; app.onLatch(3, s);
                        std::fprintf(stderr, "[sim] latch3 (reset) = %s\n", s ? "on" : "off");
                        break;
                    }

                    // Shift + number row picks the test injection channel.
                    // Keyed by scancode so it's layout-independent: on a
                    // Czech keyboard Shift over the number row produces the
                    // actual digits, and unshifted those same keys are the
                    // first three encoder trios (handled just below).
                    if ((ev.key.keysym.mod & KMOD_SHIFT) &&
                        ev.key.keysym.scancode >= SDL_SCANCODE_1 &&
                        ev.key.keysym.scancode <= SDL_SCANCODE_9) {
                        injectChannel = static_cast<uint8_t>(
                            ev.key.keysym.scancode - SDL_SCANCODE_1 + 1);
                        std::fprintf(stderr,
                                     "[sim] test injection channel = %u\n",
                                     injectChannel);
                        break;
                    }

                    // Encoder key trios {left, click, right}. Layout-
                    // independent (scancode-based). See kEncoderTrios.
                    if (handleEncoderKey(app, ev.key.keysym.scancode)) {
                        break;
                    }

                    const int note = keyToNote(ev.key.keysym.sym);
                    if (note >= 0) {
                        notePressedOnChannel[note] = injectChannel;
                        core::MidiMessage m;
                        m.type    = core::MidiType::NoteOn;
                        m.channel = injectChannel;
                        m.data1   = static_cast<uint8_t>(note);
                        m.data2   = 100;
                        midiIn.inject(m);
                    }
                    break;
                }

                case SDL_KEYUP: {
                    const int note = keyToNote(ev.key.keysym.sym);
                    if (note >= 0) {
                        const uint8_t ch = notePressedOnChannel[note] != 0
                            ? notePressedOnChannel[note]
                            : injectChannel;
                        notePressedOnChannel[note] = 0;
                        core::MidiMessage m;
                        m.type    = core::MidiType::NoteOff;
                        m.channel = ch;
                        m.data1   = static_cast<uint8_t>(note);
                        m.data2   = 0;
                        midiIn.inject(m);
                    }
                    break;
                }

                default:
                    break;
            }
        }

        core::MidiMessage msg;
        while (midiIn.poll(msg)) {
            app.onMidiIn(msg);
        }

        // Tick the engine every loop iteration (~1 ms granularity) so that
        // arp step timing is accurate and not limited to 16 ms.
        app.tick(SDL_GetTicks());

        // Throttle rendering to ~60 Hz (16 ms) to keep CPU usage reasonable.
        static Uint32 lastRender = 0;
        Uint32 now = SDL_GetTicks();
        if (now - lastRender >= 16) {
            app.render(display);
            lastRender = now;
        }

        SDL_Delay(1);  // ~1 ms granularity for engine timing
    }

    SDL_Quit();
    return 0;
}
