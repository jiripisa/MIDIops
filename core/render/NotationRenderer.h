#pragma once
#include <cstdint>
namespace core {
class Display;
class NoteWormModel;
class NotationRenderer {
public:
    void update(const NoteWormModel& model, uint32_t nowMs);
    void render(const NoteWormModel& model, Display& d) const;
private:
    struct NameDisplay {
        bool     live=false;
        uint8_t  note=0, channel=0;
        int16_t  x=0;
        uint32_t releasedMs=0;   // 0 = still held
    };
    static constexpr int kMaxNameDisplays = 32;
    NameDisplay nameDisplays_[kMaxNameDisplays] = {};
    uint32_t    nowMs_ = 0;
};
} // namespace core
