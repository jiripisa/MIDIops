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

#include "core/app/AppShell.h"
#include "core/modes/ArpMode.h"
#include "core/modes/BerlinMode.h"
#include "core/modes/BpmMode.h"
#include "core/modes/DebugMode.h"
#include "core/modes/MonitoringMode.h"
#include "core/modes/SettingsMode.h"
#include "platform/teensy/TeensyButton.h"
#include "platform/teensy/TeensyDisplay.h"
#include "platform/teensy/TeensyEncoder.h"
#include "platform/teensy/TeensyMidiInput.h"
#include "platform/teensy/TeensyMidiOutput.h"
#include "platform/teensy/TeensyStorage.h"

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
//  layer, not in these pin names. Role is assigned by the active
//  mode/screen at runtime (see core/app/AppShell).
//
//  - Latch1: latching front-panel switch (DFR0789). Active-HIGH via
//    INPUT_PULLUP — see TeensyButton.cpp for polarity notes. Role is
//    assigned by the active mode/screen at runtime (see core/app/AppShell).
//  - Enc1: KY-040 with CLK on `kPinEnc1Clk`, DT on `kPinEnc1Dt`.
//    Role is assigned by the active mode/screen at runtime (see core/app/AppShell).
// ============================================================
constexpr uint8_t kPinLatch1 = 2;
constexpr uint8_t kPinEnc1Sw  = 3;
constexpr uint8_t kPinEnc1Clk = 4;
constexpr uint8_t kPinEnc1Dt  = 5;

// Enc2 — second KY-040. CLK/DT on the opposite long edge of the Teensy
// so the two encoders sit on physically separate strips of the
// breadboard. Role is assigned by the active mode/screen at runtime
// (see core/app/AppShell).
constexpr uint8_t kPinEnc2Clk = 14;
constexpr uint8_t kPinEnc2Dt  = 15;
constexpr uint8_t kPinEnc2Sw  = 16;

// Enc3 — third KY-040. Role is assigned by the active mode/screen at
// runtime (see core/app/AppShell). Lives next to Enc2 so the two
// "mode" knobs sit together on the right side of the breadboard.
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
static TeensyStorage   storage;
static core::AppShell       app;
static core::MonitoringMode monitoringMode;
static core::DebugMode      debugMode;
static core::BpmMode        bpmMode(app);
static core::ArpMode        arpMode(app);
static core::BerlinMode     berlinMode(app);
static core::SettingsMode   settingsMode(app);

void setup() {
    display.begin();
    midiIn.begin();
    latch1Button.begin();
    enc1Switch.begin();
    enc2Switch.begin();
    enc3Switch.begin();
    enc4Switch.begin();
    enc5Switch.begin();
    latch2Button.begin();
    latch3Button.begin();
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
    storage.begin();             // LittleFS on program flash; settings persist
    app.setStorage(&storage);
    app.begin(2);   // boot into Berlin (addMode order: Monitoring, Arp, Berlin, ...)
    app.tick(millis());
    app.render(display);
}

void loop() {
    // Latches — push current polled state every loop; shell edge-detects.
    app.onLatch(1, latch1Button.pollOn());
    app.onLatch(2, latch2Button.pollOn());
    app.onLatch(3, latch3Button.pollOn());

    // Encoder rotation — forward non-zero detents to shell.
    const int d1 = enc1Knob.poll(); if (d1) app.onEncoderKnob(1, d1);
    const int d2 = enc2Knob.poll(); if (d2) app.onEncoderKnob(2, d2);
    const int d3 = enc3Knob.poll(); if (d3) app.onEncoderKnob(3, d3);
    const int d4 = enc4Knob.poll(); if (d4) app.onEncoderKnob(4, d4);
    const int d5 = enc5Knob.poll(); if (d5) app.onEncoderKnob(5, d5);

    // Encoder switches — edge-detect here, pass press events to shell.
    static bool sw1Last = false; const bool sw1 = enc1Switch.pollOn();
    if (sw1 && !sw1Last) app.onEncoderSw(1);
    sw1Last = sw1;
    static bool sw2Last = false; const bool sw2 = enc2Switch.pollOn();
    if (sw2 && !sw2Last) app.onEncoderSw(2);
    sw2Last = sw2;
    static bool sw3Last = false; const bool sw3 = enc3Switch.pollOn();
    if (sw3 && !sw3Last) app.onEncoderSw(3);
    sw3Last = sw3;
    static bool sw4Last = false; const bool sw4 = enc4Switch.pollOn();
    if (sw4 && !sw4Last) app.onEncoderSw(4);
    sw4Last = sw4;
    static bool sw5Last = false; const bool sw5 = enc5Switch.pollOn();
    if (sw5 && !sw5Last) app.onEncoderSw(5);
    sw5Last = sw5;

    core::MidiMessage msg;
    while (midiIn.poll(msg)) {
        app.onMidiIn(msg);
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
