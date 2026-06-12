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

// Berlin preset v1: 214 bytes — 'M','B','E','R', version, the BerlinParams
// fields, the sequence length, then all 32 steps (6 bytes each), so a load
// restores the exact realized pattern, not a re-roll.
bool saveBerlinPreset(Storage& st, int slot,
                      const BerlinParams& p, const BerlinSequence& seq);
bool loadBerlinPreset(Storage& st, int slot,
                      BerlinParams& outParams, BerlinSequence& outSeq);

} // namespace core
