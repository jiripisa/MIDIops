#pragma once

#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "core/Storage.h"

// In-memory Storage with call counters for persistence tests. Seed stored
// blobs through `data` directly so the `saves` counter only counts what the
// code under test wrote.
class FakeStorage : public core::Storage {
public:
    int saves   = 0;
    int removes = 0;
    std::map<std::string, std::vector<uint8_t>> data;

    bool load(const char* key, void* buf, int len) override {
        auto it = data.find(key);
        if (it == data.end() || static_cast<int>(it->second.size()) != len)
            return false;
        std::memcpy(buf, it->second.data(), static_cast<size_t>(len));
        return true;
    }
    bool save(const char* key, const void* buf, int len) override {
        ++saves;
        const uint8_t* p = static_cast<const uint8_t*>(buf);
        data[key] = std::vector<uint8_t>(p, p + len);
        return true;
    }
    bool remove(const char* key) override {
        ++removes;
        data.erase(key);
        return true;
    }
};
