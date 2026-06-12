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

// Berlin preset v2: one slot = the whole three-voice stack. 638 bytes —
// 'M','B','E','R', version 2, then 3x (16 params bytes, seq length, 32x6
// step bytes, channel, mute). A v1 (214-byte) blob fails the exact-size
// load, so old single-voice slots read as empty and get overwritten.
struct BerlinVoicePreset {
    BerlinParams   params;
    BerlinSequence seq;
    uint8_t        channel = 1;
    bool           muted   = false;
};

bool saveBerlinPreset2(Storage& st, int slot, const BerlinVoicePreset v[3]);
bool loadBerlinPreset2(Storage& st, int slot, BerlinVoicePreset v[3]);
bool berlinPreset2Usable(Storage& st, int slot);   // size+magic+version probe

} // namespace core
