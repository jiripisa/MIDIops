#pragma once

#include <string>

#include "core/Storage.h"

// File-backed Storage for the simulator: one file per key under
// $HOME/.midiops/ (falls back to the working directory when HOME is unset).
class FileStorage : public core::Storage {
public:
    FileStorage();                      // resolves + creates the directory

    bool load(const char* key, void* buf, int len) override;
    bool save(const char* key, const void* buf, int len) override;
    bool remove(const char* key) override;
    bool exists(const char* key) override;

private:
    std::string dir_;
    std::string pathFor(const char* key) const;
};
