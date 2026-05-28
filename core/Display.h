#pragma once

#include <cstdint>

namespace core {

// Pack 8-bit RGB into ILI9341-native RGB565.
constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

namespace color {
    constexpr uint16_t Black     = 0x0000;
    constexpr uint16_t White     = 0xFFFF;
    constexpr uint16_t Red       = 0xF800;
    constexpr uint16_t Green     = 0x07E0;
    constexpr uint16_t Blue      = 0x001F;
    constexpr uint16_t Yellow    = 0xFFE0;
    constexpr uint16_t Cyan      = 0x07FF;
    constexpr uint16_t Magenta   = 0xF81F;
    constexpr uint16_t Gray      = 0x8410;
    constexpr uint16_t DarkGray  = 0x4208;
    constexpr uint16_t LightGray = 0xC618;
}

// Abstract drawing surface. Concrete implementations live in platform/.
// Coordinates are in pixels, origin top-left. Colors are RGB565.
class Display {
public:
    virtual ~Display() = default;

    virtual int  width()  const = 0;
    virtual int  height() const = 0;

    virtual void clear(uint16_t color) = 0;
    virtual void fillRect(int x, int y, int w, int h, uint16_t color) = 0;

    // Draws `text` at (x, y) using a built-in 5x7 font scaled by `size`.
    // bg == fg means transparent background.
    virtual void drawText(int x, int y, const char* text,
                          uint16_t fg, uint16_t bg, int size) = 0;

    // Flush to the underlying display (no-op for direct-draw backends).
    virtual void present() = 0;
};

} // namespace core
