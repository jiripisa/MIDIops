// Mac (SDL) entrypoint for the MIDIops simulator.
//
//  * Opens a 960x720 SDL window (320x240 logical, scaled 3x).
//  * Creates a CoreMIDI virtual input port named "MIDIops" so external
//    apps (Ableton -> IAC Driver -> MIDIops) can drive the monitor.
//  * Maps keyboard keys A..G to inject local Note On/Off messages so the
//    UI can be verified without any external MIDI source.
//  * Number keys 1..9 pick the MIDI channel that subsequent A..G presses
//    are injected on — useful to see the per-channel colour palette
//    without an external multi-channel MIDI source. ESC quits.

#include <SDL.h>

#include <cstdio>

#include "core/MidiMessage.h"
#include "core/MidiMonitorApp.h"
#include "platform/host/RtMidiInput.h"
#include "platform/host/RtMidiOutput.h"
#include "platform/host/SdlDisplay.h"

namespace {

constexpr int kLogicalW = 320;
constexpr int kLogicalH = 240;
constexpr int kScale    = 3;

// White-key A..G -> MIDI note numbers, all in octave 4.
int keyToNote(SDL_Keycode k) {
    switch (k) {
        case SDLK_c: return 60; // C4
        case SDLK_d: return 62; // D4
        case SDLK_e: return 64; // E4
        case SDLK_f: return 65; // F4
        case SDLK_g: return 67; // G4
        case SDLK_a: return 69; // A4
        case SDLK_b: return 71; // B4
        default:     return -1;
    }
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
    core::MidiMonitorApp app;

    midiIn.begin();
    midiOut.begin();
    app.setMidiOutput(&midiOut);

    std::fprintf(stderr,
                 "MIDIops simulator running.\n"
                 "  A..G          inject Note On/Off\n"
                 "  1..9          set the test injection channel (default 1)\n"
                 "  LEFT / RIGHT  cycle monitored channel (OMNI..16)\n"
                 "  UP / DOWN     adjust BPM (MIDI Clock master)\n"
                 "  SPACE         toggle chord-mapping mode (= panel switch)\n"
                 "  F5            channel-SW press (restart, or cycle dir in MAP)\n"
                 "  BACKSPACE     panic — release stuck notes\n"
                 "  TAB           BPM-SW press (cycle view, or next mapping in MAP)\n"
                 "  ESC           quit\n");

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
                        // Simulates flipping the latching panel switch:
                        // toggles in/out of mapping mode.
                        app.setMappingMode(!app.mappingMode());
                        std::fprintf(stderr, "[sim] mapping mode = %s\n",
                                     app.mappingMode() ? "ON" : "OFF");
                        break;
                    }

                    if (ev.key.keysym.sym == SDLK_F5) {
                        app.onChannelSwPress();
                        std::fprintf(stderr, "[sim] channel-SW press\n");
                        break;
                    }

                    if (ev.key.keysym.sym == SDLK_BACKSPACE) {
                        app.panic();
                        std::fprintf(stderr, "[sim] panic — released stuck notes\n");
                        break;
                    }

                    if (ev.key.keysym.sym == SDLK_TAB) {
                        app.onBpmSwPress();
                        std::fprintf(stderr, "[sim] bpm-SW press\n");
                        break;
                    }

                    // Horizontal arrows = channel encoder. RIGHT = CW
                    // (channel++), LEFT = CCW (channel--).
                    if (ev.key.keysym.sym == SDLK_RIGHT ||
                        ev.key.keysym.sym == SDLK_LEFT) {
                        app.onChannelKnob(
                            ev.key.keysym.sym == SDLK_RIGHT ? +1 : -1);
                        char buf[8];
                        if (app.channel() == 0) std::snprintf(buf, sizeof(buf), "OMNI");
                        else std::snprintf(buf, sizeof(buf), "%u", app.channel());
                        std::fprintf(stderr, "[sim] monitored channel = %s\n", buf);
                        break;
                    }

                    // Vertical arrows = BPM encoder.
                    if (ev.key.keysym.sym == SDLK_UP ||
                        ev.key.keysym.sym == SDLK_DOWN) {
                        app.onBpmKnob(
                            ev.key.keysym.sym == SDLK_UP ? +1 : -1);
                        std::fprintf(stderr, "[sim] BPM = %u\n", app.bpm());
                        break;
                    }

                    // Channel selector keys. Use the layout-independent
                    // scancode so this works on non-US keyboards too — on a
                    // Czech layout, for instance, the "1" key produces "ě"
                    // without Shift and the SDLK_1 symbol never arrives.
                    if (ev.key.keysym.scancode >= SDL_SCANCODE_1 &&
                        ev.key.keysym.scancode <= SDL_SCANCODE_9) {
                        injectChannel = static_cast<uint8_t>(
                            ev.key.keysym.scancode - SDL_SCANCODE_1 + 1);
                        std::fprintf(stderr,
                                     "[sim] test injection channel = %u\n",
                                     injectChannel);
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
            app.onMessage(msg);
        }

        // Re-render every frame so worms scroll smoothly even when no new
        // MIDI is arriving.
        app.tick(SDL_GetTicks());
        app.render(display);

        SDL_Delay(16);  // ~60 Hz pacing
    }

    SDL_Quit();
    return 0;
}
