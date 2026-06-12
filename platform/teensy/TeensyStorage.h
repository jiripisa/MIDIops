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
    bool exists(const char* key) override;

private:
    // LittleFS_Program on the Teensy 4.1 uses 64 KB erase blocks
    // (SECTOR_SIZE in the core's LittleFS.cpp), and littlefs needs at least
    // two blocks just for its superblock pair — a 64 KB region is a single
    // block, so begin() fails and every operation silently no-ops. 512 KB
    // (8 blocks) is still negligible next to the ~7.6 MB free and gives the
    // wear-leveller room to rotate.
    static constexpr uint32_t kRegionBytes = 512 * 1024;
    LittleFS_Program fs_;
    bool ok_ = false;

    void pathFor(const char* key, char* out, int outLen) const;
};
