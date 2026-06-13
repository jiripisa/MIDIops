#pragma once

#include <string>
#include <vector>

#include "core/Display.h"

// Records drawing calls so tests can assert on rendered output.
struct StubDisplay : core::Display {
    int clears = 0, presents = 0, rectCount = 0;
    std::vector<std::string> texts;

    struct Rect { int x, y, w, h; uint16_t color; };
    std::vector<Rect> rects;

    struct Text { int x, y; std::string s; uint16_t fg; int size; };
    std::vector<Text> textDraws;

    int  width()  const override { return 320; }
    int  height() const override { return 240; }
    void clear(uint16_t) override { ++clears; }
    void fillRect(int x, int y, int w, int h, uint16_t c) override {
        ++rectCount;
        rects.push_back({x, y, w, h, c});
    }
    void drawText(int x, int y, const char* t, uint16_t fg, uint16_t, int size) override {
        texts.emplace_back(t);
        textDraws.push_back({x, y, t, fg, size});
    }
    void present() override { ++presents; }

    bool drewText(const std::string& needle) const {
        for (const auto& t : texts)
            if (t.find(needle) != std::string::npos) return true;
        return false;
    }

    // Foreground colour the most recent text containing `needle` was drawn
    // with (0 if never drawn). Lets tests assert highlight vs. greyed.
    uint16_t textColor(const std::string& needle) const {
        for (auto it = textDraws.rbegin(); it != textDraws.rend(); ++it)
            if (it->s.find(needle) != std::string::npos) return it->fg;
        return 0;
    }
};
