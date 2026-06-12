#pragma once

namespace core {

// Abstract persistent key->blob store. Concrete implementations live in
// platform/ (Teensy: LittleFS on program flash; host: files on disk).
// Keys are short C strings ("settings"); blobs are fixed-size byte records
// owned by the caller.
class Storage {
public:
    virtual ~Storage() = default;

    // Reads exactly `len` bytes stored under `key` into `buf`. Returns false
    // (and leaves `buf` unspecified) when the key is absent or its stored
    // size differs from `len`.
    virtual bool load(const char* key, void* buf, int len) = 0;

    // Atomically-enough replaces the blob under `key`. Returns false on
    // backend failure.
    virtual bool save(const char* key, const void* buf, int len) = 0;

    // Removes the blob under `key` (missing key is not an error).
    virtual bool remove(const char* key) = 0;
};

} // namespace core
