#include "TeensyEncoder.h"

#include <Encoder.h>

TeensyEncoder::TeensyEncoder(uint8_t pinA, uint8_t pinB, int stepsPerDetent)
    : enc_(new Encoder(pinA, pinB)),
      stepsPerDetent_(stepsPerDetent) {}

TeensyEncoder::~TeensyEncoder() {
    delete enc_;
}

int TeensyEncoder::poll() {
    const int32_t raw  = enc_->read();
    const int32_t diff = raw - lastCount_;

    // Integer division truncates toward zero, so partial-detent jitter in
    // either direction accumulates in (raw - lastCount_) until it crosses
    // a full detent threshold.
    const int steps = static_cast<int>(diff / stepsPerDetent_);
    lastCount_ += static_cast<int32_t>(steps) * stepsPerDetent_;
    return steps;
}
