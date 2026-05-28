#pragma once

#include <cstdint>

#include "core/Encoder.h"

class Encoder;   // forward decl from PJRC Encoder library

// Wraps the PJRC Encoder library for a KY-040 style mechanical encoder.
// The library counts every quadrature transition; KY-040 emits 4 per
// physical detent click, so we divide by `stepsPerDetent` (default 4)
// so poll() returns one step per "click" the user actually feels.
class TeensyEncoder : public core::EncoderInput {
public:
    TeensyEncoder(uint8_t pinA, uint8_t pinB, int stepsPerDetent = 4);
    ~TeensyEncoder();

    TeensyEncoder(const TeensyEncoder&) = delete;
    TeensyEncoder& operator=(const TeensyEncoder&) = delete;

    int poll() override;

private:
    Encoder* enc_;
    int32_t  lastCount_      = 0;
    int      stepsPerDetent_ = 4;
};
