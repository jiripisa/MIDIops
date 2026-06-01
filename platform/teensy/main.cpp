// Teensy 4.1 entrypoint. The app itself lives in core/; this file only wires
// the hardware backends to it.

#include <Arduino.h>
#include <SPI.h>
#include <ILI9341_t3n.h>

// Arduino's WProgram.h defines `swap` (and a few others) as a macro, which
// breaks <string> / <string_view> when they're included after Arduino.h.
// Undefine them at the boundary so core/ stays portable.
#undef swap
#undef abs
#undef min
#undef max
#undef round

#include "core/MidiMonitorApp.h"
#include "platform/teensy/TeensyButton.h"
#include "platform/teensy/TeensyDisplay.h"
#include "platform/teensy/TeensyEncoder.h"
#include "platform/teensy/TeensyMidiInput.h"
#include "platform/teensy/TeensyMidiOutput.h"

// ============================================================
//  ILI9341 SPI wiring  (2.8" 320x240 display)
// ------------------------------------------------------------
//  These three pins are configurable. The hardware SPI bus is
//  fixed on Teensy 4.1: MOSI=11, MISO=12, SCK=13. The display
//  shares VCC=3.3V and GND with the board; the LED backlight
//  pin can go to 3.3V directly or to a PWM-capable pin if you
//  want brightness control later.
// ============================================================
constexpr uint8_t kPinTftCs  = 10;  // chip select
constexpr uint8_t kPinTftDc  = 9;   // data/command
constexpr uint8_t kPinTftRst = 8;   // reset

// ============================================================
//  Front-panel buttons & encoder
// ------------------------------------------------------------
//  - Monitor button: latching front-panel switch. Active-LOW
//    via INPUT_PULLUP — see TeensyButton.cpp for polarity notes.
//  - Channel encoder: KY-040 with CLK on `kPinEncoderClk`, DT on
//    `kPinEncoderDt`. Each detent click steps the listened
//    channel by ±1 (CW = +, CCW = −).
//    The integrated SW pin is wired to `kPinEncoderSw` but not
//    used yet — reserved for a future short-press action.
// ============================================================
constexpr uint8_t kPinMonitorButton = 2;
constexpr uint8_t kPinEncoderSw     = 3;
constexpr uint8_t kPinEncoderClk    = 4;
constexpr uint8_t kPinEncoderDt     = 5;

// Second KY-040 — BPM control for the MIDI Clock master.
// CLK/DT on the opposite long edge of the Teensy so the two encoders sit
// on physically separate strips of the breadboard.
constexpr uint8_t kPinBpmEncoderClk = 14;
constexpr uint8_t kPinBpmEncoderDt  = 15;
constexpr uint8_t kPinBpmEncoderSw  = 16;   // wired, no behaviour yet

// Third KY-040 — view selector. Rotation cycles through Monitor /
// big-BPM / notation views; the shaft button is wired but currently
// no-op (reserved). Lives next to the BPM encoder so the two "mode"
// knobs sit together on the right side of the breadboard.
constexpr uint8_t kPinViewEncoderClk = 17;
constexpr uint8_t kPinViewEncoderDt  = 18;
constexpr uint8_t kPinViewEncoderSw  = 19;

// Fourth + fifth KY-040 — wired for testing, no app behaviour mapped
// yet. Visible in the Debug view (rotation count + press count + last
// change time) so you can verify the hardware works before assigning
// a real function. Pin 0 is Serial1 RX — we don't use the UART.
constexpr uint8_t kPinEnc4Clk = 6;
constexpr uint8_t kPinEnc4Dt  = 7;
constexpr uint8_t kPinEnc4Sw  = 0;
constexpr uint8_t kPinEnc5Clk = 20;
constexpr uint8_t kPinEnc5Dt  = 21;
constexpr uint8_t kPinEnc5Sw  = 22;

static ILI9341_t3n     tft(kPinTftCs, kPinTftDc, kPinTftRst);
static TeensyDisplay   display(tft);
static TeensyMidiInput midiIn;
static TeensyMidiOutput midiOut;
static TeensyButton    monitorButton(kPinMonitorButton);
// Channel encoder shaft button — momentary push, active-LOW (shorts to GND).
// Used as a one-shot "restart app" trigger so we can re-test the splash
// screen without unplugging USB.
static TeensyButton    encoderSwitch(kPinEncoderSw, /*activeHigh=*/false);
// BPM encoder shaft button — momentary push, active-LOW. Fires the
// "release all stuck notes" panic so the user can recover from a sender
// (e.g. Ableton edited mid-loop) that forgot to send a NoteOff.
static TeensyButton    bpmSwitch(kPinBpmEncoderSw, /*activeHigh=*/false);
// Pin order here picks the direction: passing DT first yields CW = positive
// (channel goes up) on the KY-040 module silkscreen we have. If you ever
// swap to a differently-laid-out encoder and find rotation reversed, swap
// these two arguments — no wiring change needed.
static TeensyEncoder   channelKnob(kPinEncoderDt, kPinEncoderClk);
static TeensyEncoder   bpmKnob(kPinBpmEncoderDt, kPinBpmEncoderClk);
// Third encoder — view selector.
static TeensyButton    viewSwitch(kPinViewEncoderSw, /*activeHigh=*/false);
static TeensyEncoder   viewKnob(kPinViewEncoderDt, kPinViewEncoderClk);
// Fourth + fifth encoders — wired but unmapped, surfaced only in the
// Debug view.
static TeensyButton    enc4Switch(kPinEnc4Sw, /*activeHigh=*/false);
static TeensyEncoder   enc4Knob(kPinEnc4Dt, kPinEnc4Clk);
static TeensyButton    enc5Switch(kPinEnc5Sw, /*activeHigh=*/false);
static TeensyEncoder   enc5Knob(kPinEnc5Dt, kPinEnc5Clk);
static core::MidiMonitorApp app;

void setup() {
    display.begin();
    midiIn.begin();
    app.setMidiOutput(&midiOut);
    monitorButton.begin();
    encoderSwitch.begin();
    bpmSwitch.begin();
    viewSwitch.begin();
    enc4Switch.begin();
    enc5Switch.begin();
    app.tick(millis());
    app.render(display);
}

void loop() {
    // The latching panel switch now drives the chord-mapping editor.
    // LED on = mapping mode active. Monitoring itself is always on.
    app.setMappingMode(monitorButton.pollOn());

    // Edge-triggered. In normal mode this restarts the app; inside
    // mapping mode it cycles the edit's chord direction
    // (BLOCK / UP / DOWN). The app's onChannelSwPress() routes both.
    static bool channelSwLast = false;
    const bool channelSwNow = encoderSwitch.pollOn();
    if (channelSwNow && !channelSwLast) {
        app.onChannelSwPress();
    }
    channelSwLast = channelSwNow;

    // Edge-triggered. Normal mode: cycle monitor / big-BPM / notation
    // views. Mapping mode: browse to the next existing mapping in the
    // engine.
    static bool bpmSwLast = false;
    const bool bpmSwNow = bpmSwitch.pollOn();
    if (bpmSwNow && !bpmSwLast) {
        app.onBpmSwPress();
    }
    bpmSwLast = bpmSwNow;

    const int channelDetents = channelKnob.poll();
    if (channelDetents != 0) {
        app.onChannelKnob(channelDetents);
    }
    const int bpmDetents = bpmKnob.poll();
    if (bpmDetents != 0) {
        app.onBpmKnob(bpmDetents);
    }
    const int viewDetents = viewKnob.poll();
    if (viewDetents != 0) {
        app.onViewKnob(viewDetents);
    }
    // Edge-triggered SW on the view encoder; currently reserved.
    static bool viewSwLast = false;
    const bool viewSwNow = viewSwitch.pollOn();
    if (viewSwNow && !viewSwLast) {
        app.onViewSwPress();
    }
    viewSwLast = viewSwNow;

    // Fourth + fifth encoders — wired for testing only; surface in
    // the Debug view but have no app-level action attached yet.
    const int enc4Detents = enc4Knob.poll();
    if (enc4Detents != 0) {
        app.onEnc4Knob(enc4Detents);
    }
    static bool enc4SwLast = false;
    const bool enc4SwNow = enc4Switch.pollOn();
    if (enc4SwNow && !enc4SwLast) {
        app.onEnc4SwPress();
    }
    enc4SwLast = enc4SwNow;

    const int enc5Detents = enc5Knob.poll();
    if (enc5Detents != 0) {
        app.onEnc5Knob(enc5Detents);
    }
    static bool enc5SwLast = false;
    const bool enc5SwNow = enc5Switch.pollOn();
    if (enc5SwNow && !enc5SwLast) {
        app.onEnc5SwPress();
    }
    enc5SwLast = enc5SwNow;

    core::MidiMessage msg;
    while (midiIn.poll(msg)) {
        app.onMessage(msg);
    }

    const uint32_t now = millis();
    app.tick(now);

    // Cap full repaints to ~30 fps — ILI9341_t3 paints directly to the panel
    // over SPI, so a tight render loop would saturate the bus.
    static uint32_t lastRender = 0;
    if (now - lastRender >= 33) {
        app.render(display);
        lastRender = now;
    }
}
