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

static ILI9341_t3n     tft(kPinTftCs, kPinTftDc, kPinTftRst);
static TeensyDisplay   display(tft);
static TeensyMidiInput midi;
static TeensyButton    monitorButton(kPinMonitorButton);
// Pin order here picks the direction: passing DT first yields CW = positive
// (channel goes up) on the KY-040 module silkscreen we have. If you ever
// swap to a differently-laid-out encoder and find rotation reversed, swap
// these two arguments — no wiring change needed.
static TeensyEncoder   channelKnob(kPinEncoderDt, kPinEncoderClk);
static core::MidiMonitorApp app;

void setup() {
    display.begin();
    midi.begin();
    monitorButton.begin();
    app.tick(millis());
    app.render(display);
}

void loop() {
    // The button is a latching switch — its LED reflects the switch's
    // mechanical position. Mirror that position into the app so the
    // monitoring state always matches what the user sees on the panel.
    app.setMonitoring(monitorButton.pollOn());

    const int detents = channelKnob.poll();
    if (detents != 0) {
        app.onChannelKnob(detents);
    }

    core::MidiMessage msg;
    while (midi.poll(msg)) {
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
