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
//  Front-panel buttons & encoders
// ------------------------------------------------------------
//  Hardware controls are named by their physical identity (Latch1..3,
//  Enc1..5), NOT by what they currently do — the function of each
//  encoder/button changes per screen, so the role lives in the app
//  layer, not in these pin names. The "currently" notes below are just
//  a snapshot of today's behaviour.
//
//  - Latch1: latching front-panel switch (DFR0789). Active-HIGH via
//    INPUT_PULLUP — see TeensyButton.cpp for polarity notes. Currently
//    drives chord-mapping mode.
//  - Enc1: KY-040 with CLK on `kPinEnc1Clk`, DT on `kPinEnc1Dt`.
//    Currently steps the listened channel by ±1 (CW = +, CCW = −); the
//    SW pin currently fires the app restart.
// ============================================================
constexpr uint8_t kPinLatch1 = 2;
constexpr uint8_t kPinEnc1Sw  = 3;
constexpr uint8_t kPinEnc1Clk = 4;
constexpr uint8_t kPinEnc1Dt  = 5;

// Enc2 — second KY-040. CLK/DT on the opposite long edge of the Teensy
// so the two encoders sit on physically separate strips of the
// breadboard. Currently controls the MIDI Clock master's BPM; SW is a
// no-op in normal mode (browses mappings in mapping mode).
constexpr uint8_t kPinEnc2Clk = 14;
constexpr uint8_t kPinEnc2Dt  = 15;
constexpr uint8_t kPinEnc2Sw  = 16;

// Enc3 — third KY-040. Currently the view selector: rotation cycles
// Monitor / big-BPM / notation / debug; the shaft button is a no-op
// (reserved). Lives next to Enc2 so the two "mode" knobs sit together
// on the right side of the breadboard.
constexpr uint8_t kPinEnc3Clk = 17;
constexpr uint8_t kPinEnc3Dt  = 18;
constexpr uint8_t kPinEnc3Sw  = 19;

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

// Latch2 + Latch3 — two more DFR0789-style latching switches, same
// hardware as Latch1. Wired but unmapped; surfaced in the Debug view as
// LATCH2 / LATCH3 (latched-state pill + toggle counter).
constexpr uint8_t kPinLatch2 = 1;
constexpr uint8_t kPinLatch3 = 23;

static ILI9341_t3n     tft(kPinTftCs, kPinTftDc, kPinTftRst);
static TeensyDisplay   display(tft);
static TeensyMidiInput midiIn;
static TeensyMidiOutput midiOut;
static TeensyButton    latch1Button(kPinLatch1);
// Encoder shaft buttons — momentary push, active-LOW (shorts to GND).
// Each forwards to app.onEncNSwPress(); the app decides what the press
// does for the current screen.
static TeensyButton    enc1Switch(kPinEnc1Sw, /*activeHigh=*/false);
static TeensyButton    enc2Switch(kPinEnc2Sw, /*activeHigh=*/false);
// Pin order here picks the direction: passing DT first yields CW = positive
// on the KY-040 module silkscreen we have. If you ever swap to a
// differently-laid-out encoder and find rotation reversed, swap these two
// arguments — no wiring change needed.
static TeensyEncoder   enc1Knob(kPinEnc1Dt, kPinEnc1Clk);
static TeensyEncoder   enc2Knob(kPinEnc2Dt, kPinEnc2Clk);
static TeensyButton    enc3Switch(kPinEnc3Sw, /*activeHigh=*/false);
static TeensyEncoder   enc3Knob(kPinEnc3Dt, kPinEnc3Clk);
// Enc4 + Enc5 — wired but unmapped, surfaced only in the Debug view.
static TeensyButton    enc4Switch(kPinEnc4Sw, /*activeHigh=*/false);
static TeensyEncoder   enc4Knob(kPinEnc4Dt, kPinEnc4Clk);
static TeensyButton    enc5Switch(kPinEnc5Sw, /*activeHigh=*/false);
static TeensyEncoder   enc5Knob(kPinEnc5Dt, kPinEnc5Clk);
// Latch2 + Latch3 — DFR0789-style, active-HIGH on INPUT_PULLUP (signal
// pin HIGH when latched closed). Matches the latch1Button default.
static TeensyButton    latch2Button(kPinLatch2);
static TeensyButton    latch3Button(kPinLatch3);
static core::MidiMonitorApp app;

void setup() {
    display.begin();
    midiIn.begin();
    app.setMidiOutput(&midiOut);
    latch1Button.begin();
    enc1Switch.begin();
    enc2Switch.begin();
    enc3Switch.begin();
    enc4Switch.begin();
    enc5Switch.begin();
    latch2Button.begin();
    latch3Button.begin();
    app.tick(millis());
    app.render(display);
}

void loop() {
    // The latching panel switch now drives the chord-mapping editor.
    // LED on = mapping mode active. Monitoring itself is always on.
    app.onLatch1(latch1Button.pollOn());

    // Edge-triggered. In normal mode this restarts the app; inside
    // mapping mode it cycles the edit's chord direction
    // (BLOCK / UP / DOWN). The app's onEnc1SwPress() routes both.
    static bool enc1SwLast = false;
    const bool enc1SwNow = enc1Switch.pollOn();
    if (enc1SwNow && !enc1SwLast) {
        app.onEnc1SwPress();
    }
    enc1SwLast = enc1SwNow;

    // Edge-triggered. Normal mode: cycle monitor / big-BPM / notation
    // views. Mapping mode: browse to the next existing mapping in the
    // engine.
    static bool enc2SwLast = false;
    const bool enc2SwNow = enc2Switch.pollOn();
    if (enc2SwNow && !enc2SwLast) {
        app.onEnc2SwPress();
    }
    enc2SwLast = enc2SwNow;

    const int enc1Detents = enc1Knob.poll();
    if (enc1Detents != 0) {
        app.onEnc1Knob(enc1Detents);
    }
    const int enc2Detents = enc2Knob.poll();
    if (enc2Detents != 0) {
        app.onEnc2Knob(enc2Detents);
    }
    const int enc3Detents = enc3Knob.poll();
    if (enc3Detents != 0) {
        app.onEnc3Knob(enc3Detents);
    }
    // Edge-triggered SW on the view encoder; currently reserved.
    static bool enc3SwLast = false;
    const bool enc3SwNow = enc3Switch.pollOn();
    if (enc3SwNow && !enc3SwLast) {
        app.onEnc3SwPress();
    }
    enc3SwLast = enc3SwNow;

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

    // Latching panel buttons #2 + #3 — push state every loop; the app
    // edge-detects internally and only bumps the debug counter on real
    // transitions.
    app.onLatch2(latch2Button.pollOn());
    app.onLatch3(latch3Button.pollOn());

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
