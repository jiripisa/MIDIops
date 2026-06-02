#pragma once

#include <string>
#include <vector>

#include "core/Display.h"

// Records drawing calls so tests can assert on rendered output.
struct StubDisplay : core::Display {
    int clears = 0, presents = 0, rects = 0;
    std::vector<std::string> texts;

    int  width()  const override { return 320; }
    int  height() const override { return 240; }
    void clear(uint16_t) override { ++clears; }
    void fillRect(int, int, int, int, uint16_t) override { ++rects; }
    void drawText(int, int, const char* t, uint16_t, uint16_t, int) override {
        texts.emplace_back(t);
    }
    void present() override { ++presents; }

    bool drewText(const std::string& needle) const {
        for (const auto& t : texts)
            if (t.find(needle) != std::string::npos) return true;
        return false;
    }
};
