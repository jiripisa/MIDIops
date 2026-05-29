#include "RtMidiOutput.h"

#include <RtMidi.h>

#include <chrono>
#include <cstdio>
#include <utility>

RtMidiOutput::RtMidiOutput(std::string portName)
    : portName_(std::move(portName)) {}

RtMidiOutput::~RtMidiOutput() {
    clockRunning_.store(false);
    if (clockThread_.joinable()) clockThread_.join();
    if (midi_) {
        try { midi_->closePort(); } catch (...) {}
    }
}

void RtMidiOutput::begin() {
    try {
        midi_ = std::make_unique<RtMidiOut>(RtMidi::MACOSX_CORE,
                                            "jp4midi simulator");
        midi_->openVirtualPort(portName_);
        std::fprintf(stderr,
                     "[RtMidi] virtual output port opened: \"%s\"\n",
                     portName_.c_str());
    } catch (RtMidiError& e) {
        std::fprintf(stderr,
                     "[RtMidi] failed to open output port: %s\n",
                     e.getMessage().c_str());
        midi_.reset();
    }
}

void RtMidiOutput::sendByte(unsigned char status) {
    std::lock_guard<std::mutex> lock(sendMutex_);
    if (!midi_) return;
    midi_->sendMessage(&status, 1);
}

void RtMidiOutput::setClockBpm(uint16_t bpm) {
    if (bpm == 0) {
        if (clockRunning_.load()) {
            clockRunning_.store(false);
            if (clockThread_.joinable()) clockThread_.join();
        }
        return;
    }
    // 24 PPQN.
    const uint32_t periodUs =
        60000000u / (static_cast<uint32_t>(bpm) * 24u);
    clockPeriodUs_.store(periodUs);
    if (!clockRunning_.load()) {
        clockRunning_.store(true);
        clockThread_ = std::thread(&RtMidiOutput::clockThreadFunc, this);
    }
}

void RtMidiOutput::clockThreadFunc() {
    // Accumulated target time so the clock doesn't drift even if
    // sleep_until overshoots a little.
    auto nextTick = std::chrono::steady_clock::now();
    while (clockRunning_.load()) {
        const auto period =
            std::chrono::microseconds(clockPeriodUs_.load());
        nextTick += period;
        std::this_thread::sleep_until(nextTick);
        sendByte(0xF8);
    }
}

void RtMidiOutput::sendStart()    { sendByte(0xFA); }
void RtMidiOutput::sendContinue() { sendByte(0xFB); }
void RtMidiOutput::sendStop()     { sendByte(0xFC); }
