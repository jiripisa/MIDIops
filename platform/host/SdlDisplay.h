#pragma once

#include <cstdint>
#include <vector>

#include "core/Display.h"

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;

// Software-rendered RGB565 framebuffer presented through an SDL2 window
// scaled by an integer factor. Mimics the Teensy's ILI9341 pixel format so
// the same Display calls produce identical output on both platforms.
class SdlDisplay : public core::Display {
public:
    SdlDisplay(int logicalWidth, int logicalHeight, int scale, const char* title);
    ~SdlDisplay() override;

    SdlDisplay(const SdlDisplay&)            = delete;
    SdlDisplay& operator=(const SdlDisplay&) = delete;

    int  width()  const override { return w_; }
    int  height() const override { return h_; }

    void clear(uint16_t color) override;
    void fillRect(int x, int y, int w, int h, uint16_t color) override;
    void drawText(int x, int y, const char* text,
                  uint16_t fg, uint16_t bg, int size) override;
    void present() override;

private:
    void putPixel(int x, int y, uint16_t color);

    int w_;
    int h_;
    std::vector<uint16_t> framebuffer_;

    SDL_Window*   window_   = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture*  texture_  = nullptr;
};
