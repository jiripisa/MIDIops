#include "core/Presets.h"

#include <cstdio>

#include "core/Storage.h"

namespace {

constexpr uint8_t kPresetVersion = 1;

constexpr int kArpBlobLen    = 5 + 9;                                   // 14
constexpr int kBerlinBlobLen = 5 + 16 + 1 + core::BerlinSequence::kMaxSteps * 6;  // 214

bool validSlot(int slot) { return slot >= 0 && slot < core::kPresetSlots; }

bool magicOk(const uint8_t* b, char m1, char m2, char m3) {
    return b[0] == 'M' && b[1] == static_cast<uint8_t>(m1) &&
           b[2] == static_cast<uint8_t>(m2) && b[3] == static_cast<uint8_t>(m3) &&
           b[4] == kPresetVersion;
}

} // namespace

namespace core {

void presetKey(const char* prefix, int slot, char* out, int outLen) {
    snprintf(out, static_cast<size_t>(outLen), "%s.s%02d", prefix, slot + 1);
}

bool presetExists(Storage& st, const char* prefix, int slot) {
    if (!validSlot(slot)) return false;
    char key[24];
    presetKey(prefix, slot, key, sizeof key);
    return st.exists(key);
}

bool deletePreset(Storage& st, const char* prefix, int slot) {
    if (!validSlot(slot)) return false;
    char key[24];
    presetKey(prefix, slot, key, sizeof key);
    return st.remove(key);
}

// ---------------------------------------------------------------------------
// Arp
// ---------------------------------------------------------------------------

bool saveArpPreset(Storage& st, int slot, const ArpParams& p) {
    if (!validSlot(slot)) return false;
    const uint8_t b[kArpBlobLen] = {
        'M', 'A', 'R', 'P', kPresetVersion,
        p.steps,
        static_cast<uint8_t>(p.rate),
        p.gatePercent,
        static_cast<uint8_t>(p.direction),
        static_cast<uint8_t>(p.octave),       // int8 bit pattern
        p.swingPercent,
        static_cast<uint8_t>(p.velocityMode),
        p.fixedVelocity,
        static_cast<uint8_t>(p.latch ? 1 : 0),
    };
    char key[24];
    presetKey("arp", slot, key, sizeof key);
    return st.save(key, b, kArpBlobLen);
}

bool loadArpPreset(Storage& st, int slot, ArpParams& out) {
    if (!validSlot(slot)) return false;
    char key[24];
    presetKey("arp", slot, key, sizeof key);
    uint8_t b[kArpBlobLen];
    if (!st.load(key, b, kArpBlobLen)) return false;
    if (!magicOk(b, 'A', 'R', 'P')) return false;
    ArpParams p;
    p.steps         = b[5];
    p.rate          = static_cast<ArpRate>(b[6]);
    p.gatePercent   = b[7];
    p.direction     = static_cast<ArpDirection>(b[8]);
    p.octave        = static_cast<int8_t>(b[9]);
    p.swingPercent  = b[10];
    p.velocityMode  = static_cast<ArpVelocityMode>(b[11]);
    p.fixedVelocity = b[12];
    p.latch         = b[13] != 0;
    // Whole-blob validation: ranges mirror the editing screens' clamps.
    if (p.steps < 1 || p.steps > 16) return false;
    if (b[6] >= static_cast<uint8_t>(ArpRate::kCount)) return false;
    if (p.gatePercent < 10 || p.gatePercent > 100) return false;
    if (b[8] >= static_cast<uint8_t>(ArpDirection::kCount)) return false;
    if (p.octave < -2 || p.octave > 2) return false;
    if (p.swingPercent < 50 || p.swingPercent > 75) return false;
    if (b[11] >= static_cast<uint8_t>(ArpVelocityMode::kCount)) return false;
    if (p.fixedVelocity < 1 || p.fixedVelocity > 127) return false;
    if (b[13] > 1) return false;
    out = p;
    return true;
}

// ---------------------------------------------------------------------------
// Berlin
// ---------------------------------------------------------------------------

bool saveBerlinPreset(Storage& st, int slot,
                      const BerlinParams& p, const BerlinSequence& seq) {
    if (!validSlot(slot)) return false;
    uint8_t b[kBerlinBlobLen] = {'M', 'B', 'E', 'R', kPresetVersion};
    int o = 5;
    b[o++] = static_cast<uint8_t>(p.algorithm);
    b[o++] = p.length;
    b[o++] = static_cast<uint8_t>(p.resolution);
    b[o++] = p.density;
    b[o++] = p.gatePercent;
    b[o++] = p.tension;
    b[o++] = p.octaveBase;
    b[o++] = p.octaveRange;
    b[o++] = p.velocityBase;
    b[o++] = p.velocityHumanize;
    b[o++] = p.accent;
    b[o++] = p.scatter;
    b[o++] = p.gateLen;
    b[o++] = static_cast<uint8_t>(p.behavior);
    b[o++] = p.morph;
    b[o++] = p.evolveRate;
    b[o++] = static_cast<uint8_t>(seq.length());
    for (int i = 0; i < BerlinSequence::kMaxSteps; ++i) {
        const BerlinStep& s = seq.step(i);
        b[o++] = s.active ? 1 : 0;
        b[o++] = s.note;
        b[o++] = s.velocity;
        b[o++] = s.accent ? 1 : 0;
        b[o++] = static_cast<uint8_t>(s.gateTicks > 255 ? 255 : s.gateTicks);
        b[o++] = static_cast<uint8_t>(s.velJitter);   // int8 bit pattern
    }
    char key[24];
    presetKey("berlin", slot, key, sizeof key);
    return st.save(key, b, kBerlinBlobLen);
}

bool loadBerlinPreset(Storage& st, int slot,
                      BerlinParams& outParams, BerlinSequence& outSeq) {
    if (!validSlot(slot)) return false;
    char key[24];
    presetKey("berlin", slot, key, sizeof key);
    uint8_t b[kBerlinBlobLen];
    if (!st.load(key, b, kBerlinBlobLen)) return false;
    if (!magicOk(b, 'B', 'E', 'R')) return false;
    BerlinParams p;
    int o = 5;
    const uint8_t algorithm  = b[o++];
    p.length                 = b[o++];
    const uint8_t resolution = b[o++];
    p.density                = b[o++];
    p.gatePercent            = b[o++];
    p.tension                = b[o++];
    p.octaveBase             = b[o++];
    p.octaveRange            = b[o++];
    p.velocityBase           = b[o++];
    p.velocityHumanize       = b[o++];
    p.accent                 = b[o++];
    p.scatter                = b[o++];
    p.gateLen                = b[o++];
    const uint8_t behavior   = b[o++];
    p.morph                  = b[o++];
    p.evolveRate             = b[o++];
    const uint8_t seqLen     = b[o++];
    // Whole-blob validation (ranges mirror BerlinTypes.h + the screens).
    if (algorithm >= static_cast<uint8_t>(BerlinAlgorithm::kCount)) return false;
    if (p.length < 3 || p.length > BerlinSequence::kMaxSteps) return false;
    if (resolution >= static_cast<uint8_t>(BerlinResolution::kCount)) return false;
    if (p.density > 100) return false;
    if (p.gatePercent < 40 || p.gatePercent > 99) return false;
    if (p.tension > 100) return false;
    if (p.octaveBase < 24 || p.octaveBase > 72) return false;
    if (p.octaveRange < 1 || p.octaveRange > 3) return false;
    if (p.velocityBase < 1 || p.velocityBase > 126) return false;
    if (p.velocityHumanize > 30) return false;
    if (p.accent > 27) return false;
    if (p.scatter < 1 || p.scatter > 7) return false;
    if (p.gateLen < 3 || p.gateLen > 16) return false;
    if (behavior >= static_cast<uint8_t>(BerlinBehavior::kCount)) return false;
    if (p.morph > 100) return false;
    if (p.evolveRate < 1 || p.evolveRate > 8) return false;
    if (seqLen < 1 || seqLen > BerlinSequence::kMaxSteps) return false;
    for (int i = 0; i < BerlinSequence::kMaxSteps; ++i) {
        const uint8_t* s = b + 5 + 16 + 1 + i * 6;
        if (s[0] > 1 || s[1] > 127 || s[2] > 127 || s[3] > 1) return false;
    }
    p.algorithm  = static_cast<BerlinAlgorithm>(algorithm);
    p.resolution = static_cast<BerlinResolution>(resolution);
    p.behavior   = static_cast<BerlinBehavior>(behavior);
    BerlinSequence seq;
    seq.setLength(seqLen);
    for (int i = 0; i < BerlinSequence::kMaxSteps; ++i) {
        const uint8_t* s = b + 5 + 16 + 1 + i * 6;
        BerlinStep& step = seq.step(i);
        step.active    = s[0] != 0;
        step.note      = s[1];
        step.velocity  = s[2];
        step.accent    = s[3] != 0;
        step.gateTicks = s[4];
        step.velJitter = static_cast<int8_t>(s[5]);
    }
    outParams = p;
    outSeq    = seq;
    return true;
}

} // namespace core
