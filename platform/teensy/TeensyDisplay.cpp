#include "TeensyDisplay.h"

#include <ILI9341_t3n.h>

// Off-screen RGB565 framebuffer for the 320x240 panel. Lives in DMAMEM
// (OCRAM) so the SPI DMA engine can stream it to the display without a
// copy. 320 * 240 * 2 = 153,600 bytes — comfortably inside the Teensy 4.1
// OCRAM region.
DMAMEM static uint16_t kFramebuffer[320 * 240];

TeensyDisplay::TeensyDisplay(ILI9341_t3n& tft) : tft_(tft) {}

void TeensyDisplay::begin() {
    tft_.begin();
    tft_.setRotation(1);             // landscape: 320x240
    tft_.setFrameBuffer(kFramebuffer);
    tft_.useFrameBuffer(true);
    tft_.fillScreen(ILI9341_BLACK);
    tft_.updateScreen();
}

int TeensyDisplay::width()  const { return tft_.width(); }
int TeensyDisplay::height() const { return tft_.height(); }

void TeensyDisplay::clear(uint16_t color) {
    // clear() opens every render frame, so this is also the natural barrier
    // that waits for the previous frame's async DMA push to finish before
    // we start mutating the framebuffer again. The wait is a no-op when
    // the DMA already completed during the loop's idle time.
    tft_.waitUpdateAsyncComplete();
    tft_.fillScreen(color);
}

void TeensyDisplay::fillRect(int x, int y, int w, int h, uint16_t color) {
    tft_.fillRect(x, y, w, h, color);
}

void TeensyDisplay::drawText(int x, int y, const char* text,
                             uint16_t fg, uint16_t bg, int size) {
    tft_.setCursor(x, y);
    tft_.setTextColor(fg, bg);
    tft_.setTextSize(size);
    tft_.print(text);
}

void TeensyDisplay::present() {
    // Kick off a DMA-driven SPI push of the framebuffer and return
    // immediately. The main loop keeps running through the ~20 ms it
    // takes to clock the pixels out — without this the loop would
    // block, USB MIDI input would queue up on the Mac side, the encoder
    // wouldn't be polled, and IntervalTimer-queued clock pulses would
    // pile up behind the SPI burst.
    tft_.updateScreenAsync();
}
