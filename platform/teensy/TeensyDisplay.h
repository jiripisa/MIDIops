#pragma once

#include "core/Display.h"

class ILI9341_t3n; // forward decl avoids pulling Arduino headers into core consumers

class TeensyDisplay : public core::Display {
public:
    explicit TeensyDisplay(ILI9341_t3n& tft);

    // Initializes the underlying display. Call once from setup().
    void begin();

    int  width()  const override;
    int  height() const override;
    void clear(uint16_t color) override;
    void fillRect(int x, int y, int w, int h, uint16_t color) override;
    void drawText(int x, int y, const char* text,
                  uint16_t fg, uint16_t bg, int size) override;
    void present() override;

private:
    ILI9341_t3n& tft_;
};
