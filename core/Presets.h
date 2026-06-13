#pragma once

#include <cstdint>

#include "core/ArpTypes.h"
#include "core/BerlinSequence.h"
#include "core/BerlinTypes.h"

namespace core {

class Storage;

// Preset slots for the modes' Save/Load/Delete screens. Each slot is one
// Storage key ("arp.s01".."arp.s20", "berlin.s01".."berlin.s20") holding a
// fixed-size versioned blob (explicit bytes — no struct memcpy, no padding
// or endianness concerns). Loads validate the whole blob first: a corrupt
// or out-of-range slot behaves exactly like an empty one.
constexpr int kPresetSlots = 20;

// Writes "<prefix>.sNN" (slot 0-based -> NN = 01..20) into out.
void presetKey(const char* prefix, int slot, char* out, int outLen);

bool presetExists(Storage& st, const char* prefix, int slot);
bool deletePreset(Storage& st, const char* prefix, int slot);

// Arp preset v1: 14 bytes — 'M','A','R','P', version, then ArpParams.
bool saveArpPreset(Storage& st, int slot, const ArpParams& p);
bool loadArpPreset(Storage& st, int slot, ArpParams& out);

// Berlin preset v3: one slot = the whole voice stack (`count` voices, 1..4).
// Bytes: 'M','B','E','R', version 3, then count x (16 params bytes, seq
// length, 32x6 step bytes, channel, mute). A blob of a different voice count
// (different size) or older version fails the load and reads as empty.
struct BerlinVoicePreset {
    BerlinParams   params;
    BerlinSequence seq;
    uint8_t        channel = 1;
    bool           muted   = false;
};

bool saveBerlinPreset2(Storage& st, int slot, const BerlinVoicePreset* v, int count);
bool loadBerlinPreset2(Storage& st, int slot, BerlinVoicePreset* v, int count);
bool berlinPreset2Usable(Storage& st, int slot, int count);

} // namespace core
