#pragma once

#include <LittleFS.h>

#include "core/Storage.h"

// LittleFS-backed Storage on a region of the Teensy 4.1 program flash
// (LittleFS_Program). LittleFS wear-levels internally, so the debounced
// settings writes are gentle on the flash. One file per key at "/<key>".
class TeensyStorage : public core::Storage {
public:
    // Mounts (and on first use formats) the region. Call once in setup().
    // Returns false when the filesystem cannot be brought up; the instance
    // then fails every operation gracefully.
    bool begin();

    bool load(const char* key, void* buf, int len) override;
    bool save(const char* key, const void* buf, int len) override;
    bool remove(const char* key) override;

private:
    static constexpr uint32_t kRegionBytes = 64 * 1024;
    LittleFS_Program fs_;
    bool ok_ = false;

    void pathFor(const char* key, char* out, int outLen) const;
};
