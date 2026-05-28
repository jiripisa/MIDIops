#include "SdlDisplay.h"

#include <SDL.h>

#include <cstdio>
#include <cstdlib>

#include "font5x7.h"

SdlDisplay::SdlDisplay(int logicalWidth, int logicalHeight, int scale,
                       const char* title)
    : w_(logicalWidth),
      h_(logicalHeight),
      framebuffer_(static_cast<size_t>(logicalWidth) * logicalHeight, 0) {

    window_ = SDL_CreateWindow(title,
                               SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                               logicalWidth * scale, logicalHeight * scale,
                               SDL_WINDOW_SHOWN);
    if (!window_) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        std::exit(1);
    }

    renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer_) {
        std::fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        std::exit(1);
    }

    texture_ = SDL_CreateTexture(renderer_,
                                 SDL_PIXELFORMAT_RGB565,
                                 SDL_TEXTUREACCESS_STREAMING,
                                 logicalWidth, logicalHeight);
    if (!texture_) {
        std::fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
        std::exit(1);
    }
}

SdlDisplay::~SdlDisplay() {
    if (texture_)  SDL_DestroyTexture(texture_);
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_)   SDL_DestroyWindow(window_);
}

void SdlDisplay::putPixel(int x, int y, uint16_t color) {
    if (x < 0 || y < 0 || x >= w_ || y >= h_) return;
    framebuffer_[static_cast<size_t>(y) * w_ + x] = color;
}

void SdlDisplay::clear(uint16_t color) {
    const size_t n = framebuffer_.size();
    for (size_t i = 0; i < n; ++i) framebuffer_[i] = color;
}

void SdlDisplay::fillRect(int x, int y, int w, int h, uint16_t color) {
    if (w <= 0 || h <= 0) return;
    const int x0 = (x < 0) ? 0 : x;
    const int y0 = (y < 0) ? 0 : y;
    const int x1 = (x + w > w_) ? w_ : x + w;
    const int y1 = (y + h > h_) ? h_ : y + h;
    for (int yy = y0; yy < y1; ++yy) {
        uint16_t* row = framebuffer_.data() + static_cast<size_t>(yy) * w_;
        for (int xx = x0; xx < x1; ++xx) row[xx] = color;
    }
}

void SdlDisplay::drawText(int x, int y, const char* text,
                          uint16_t fg, uint16_t bg, int size) {
    if (!text || size <= 0) return;

    const bool transparent = (fg == bg);
    int cx = x;
    for (const char* p = text; *p; ++p) {
        const uint8_t* g = host_font::glyph(*p);
        // 5 glyph columns + 1 trailing spacing column = 6 columns per char.
        for (int col = 0; col < 6; ++col) {
            const uint8_t bits = (col < 5) ? g[col] : 0x00;
            for (int row = 0; row < 8; ++row) {
                const bool on = (row < 7) && ((bits >> row) & 0x01);
                if (on) {
                    fillRect(cx + col * size, y + row * size, size, size, fg);
                } else if (!transparent) {
                    fillRect(cx + col * size, y + row * size, size, size, bg);
                }
            }
        }
        cx += 6 * size;
    }
}

void SdlDisplay::present() {
    SDL_UpdateTexture(texture_, nullptr, framebuffer_.data(),
                      w_ * static_cast<int>(sizeof(uint16_t)));
    SDL_RenderClear(renderer_);
    SDL_RenderCopy(renderer_, texture_, nullptr, nullptr);
    SDL_RenderPresent(renderer_);
}
