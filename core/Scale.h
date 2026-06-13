#pragma once

#include <cstdint>

namespace core {

class Scale {
public:
    enum class Type : uint8_t {
        Major = 0, Minor, Aug, Dim, PentaMajor, PentaMinor, kCount
    };

    Scale() = default;
    Scale(Type type, uint8_t root) : type_(type), root_(root % 12) {}

    Type    type() const { return type_; }
    uint8_t root() const { return root_; }      // 0..11 pitch class
    void setType(Type t) { type_ = t; }
    void setRoot(uint8_t pc) { root_ = pc % 12; }

    bool    contains(uint8_t note) const;       // is note's pitch-class in scale
    uint8_t quantize(uint8_t note) const;       // nearest in-scale note, ties round down
    // MIDI note `degrees` scale-steps above fromNote (quantized first if needed);
    // degrees may exceed the scale length, wrapping through octaves. degrees=0
    // returns fromNote quantized. Result clamped to 0..127.
    uint8_t degreeNote(uint8_t fromNote, int degrees) const;
    // Absolute scale-degree index of `note` (signed), including octaves:
    // octave * degreeCount() + position-in-octave, after quantizing to scale.
    // Differences between two indices give the signed scale-degree distance.
    int degreeIndex(uint8_t note) const;
    int degreeCount() const;                     // number of notes in the scale (5..8)

private:
    int intervals(const uint8_t** out) const;   // semitone offsets from root, ascending, within one octave

    Type    type_ = Type::Major;
    uint8_t root_ = 0;
};

} // namespace core
