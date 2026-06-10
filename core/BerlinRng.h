#pragma once

#include <cstdint>

namespace core {

// Small xorshift32 PRNG. Portable (no <random> / platform dependency),
// deterministic for a given seed → reproducible generation in unit tests.
struct BerlinRng {
    uint32_t state = 0x12345u;

    void seed(uint32_t v) { state = v ? v : 1u; }

    uint32_t next() {
        uint32_t x = state;
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        state = x;
        return x;
    }

    // Uniform-ish integer in [lo, hi] inclusive (hi >= lo). Modulo bias is
    // negligible for the small ranges used here.
    int range(int lo, int hi) {
        if (hi <= lo) return lo;
        return lo + static_cast<int>(next() % static_cast<uint32_t>(hi - lo + 1));
    }

    // Returns true with probability `percent`/100.
    bool chance(int percent) { return range(0, 99) < percent; }
};

} // namespace core
