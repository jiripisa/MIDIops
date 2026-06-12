#include "FileStorage.h"

#include <sys/stat.h>

#include <cstdio>
#include <cstdlib>

FileStorage::FileStorage() {
    const char* home = std::getenv("HOME");
    dir_ = home && *home ? std::string(home) + "/.midiops" : ".midiops";
    ::mkdir(dir_.c_str(), 0755);        // EEXIST is fine
}

std::string FileStorage::pathFor(const char* key) const {
    return dir_ + "/" + key + ".bin";
}

bool FileStorage::load(const char* key, void* buf, int len) {
    std::FILE* f = std::fopen(pathFor(key).c_str(), "rb");
    if (!f) return false;
    // The Storage contract is exact-size: a file of any other length is
    // treated as absent so a stale/foreign file can never half-apply.
    bool ok = std::fread(buf, 1, static_cast<size_t>(len), f) ==
              static_cast<size_t>(len);
    ok = ok && std::fgetc(f) == EOF;
    std::fclose(f);
    return ok;
}

bool FileStorage::save(const char* key, const void* buf, int len) {
    std::FILE* f = std::fopen(pathFor(key).c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(buf, 1, static_cast<size_t>(len), f) ==
                    static_cast<size_t>(len);
    std::fclose(f);
    return ok;
}

bool FileStorage::remove(const char* key) {
    std::remove(pathFor(key).c_str());  // missing file is not an error
    return true;
}
