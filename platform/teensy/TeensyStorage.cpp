#include "TeensyStorage.h"

#include <cstdio>

bool TeensyStorage::begin() {
    ok_ = fs_.begin(kRegionBytes);
    return ok_;
}

void TeensyStorage::pathFor(const char* key, char* out, int outLen) const {
    snprintf(out, static_cast<size_t>(outLen), "/%s", key);
}

bool TeensyStorage::load(const char* key, void* buf, int len) {
    if (!ok_) return false;
    char path[32];
    pathFor(key, path, sizeof path);
    File f = fs_.open(path, FILE_READ);
    if (!f) return false;
    // Exact-size contract: any other stored length is treated as absent.
    bool ok = static_cast<int>(f.size()) == len &&
              f.read(buf, static_cast<size_t>(len)) == static_cast<size_t>(len);
    f.close();
    return ok;
}

bool TeensyStorage::save(const char* key, const void* buf, int len) {
    if (!ok_) return false;
    char path[32];
    pathFor(key, path, sizeof path);
    fs_.remove(path);                  // replace, never append
    File f = fs_.open(path, FILE_WRITE);
    if (!f) return false;
    const bool ok = f.write(buf, static_cast<size_t>(len)) ==
                    static_cast<size_t>(len);
    f.close();
    return ok;
}

bool TeensyStorage::remove(const char* key) {
    if (!ok_) return false;
    char path[32];
    pathFor(key, path, sizeof path);
    fs_.remove(path);                  // missing file is not an error
    return true;
}
