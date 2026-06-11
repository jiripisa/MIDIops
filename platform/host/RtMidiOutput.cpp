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
                                            "MIDIops simulator");
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

void RtMidiOutput::sendThree(unsigned char status,
                             unsigned char d1, unsigned char d2) {
    std::lock_guard<std::mutex> lock(sendMutex_);
    if (!midi_) return;
    unsigned char msg[3] = { status, d1, d2 };
    midi_->sendMessage(msg, 3);
}

void RtMidiOutput::setClockBpm(uint16_t bpm) {
    if (bpm == 0) {
        if (clockRunning_.load()) {
            clockRunning_.store(false);
            if (clockThread_.joinable()) clockThread_.join();
        }
        // Drop any pending ticks so switching back to Internal later doesn't
        // replay them as a phantom burst.
        clockTicks_.store(0, std::memory_order_relaxed);
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
    // Accumulated target time so the clock doesn't drift even if the sleep
    // overshoots a little (nextTick += period preserves the cadence).
    auto nextTick = std::chrono::steady_clock::now();
    while (clockRunning_.load()) {
        const auto period =
            std::chrono::microseconds(clockPeriodUs_.load());
        nextTick += period;
        // Sleep toward nextTick in small chunks, re-checking clockRunning_ each
        // chunk, so a stop()+join() returns promptly instead of stalling the UI
        // for up to a full period (≈83 ms at 30 BPM).
        constexpr auto kChunk = std::chrono::milliseconds(5);
        while (clockRunning_.load()) {
            const auto now = std::chrono::steady_clock::now();
            if (now >= nextTick) break;
            const auto remaining = nextTick - now;
            std::this_thread::sleep_for(remaining < kChunk ? remaining : kChunk);
        }
        // Re-check before emitting so we don't send one extra 0xF8 after stop.
        if (!clockRunning_.load()) break;
        sendByte(0xF8);
        clockTicks_.fetch_add(1, std::memory_order_relaxed);
    }
}

void RtMidiOutput::sendStart()    { sendByte(0xFA); }
void RtMidiOutput::sendContinue() { sendByte(0xFB); }
void RtMidiOutput::sendStop()     { sendByte(0xFC); }

void RtMidiOutput::sendNoteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
    if (channel < 1 || channel > 16) return;
    sendThree(static_cast<unsigned char>(0x90 | (channel - 1)),
              static_cast<unsigned char>(note & 0x7F),
              static_cast<unsigned char>(velocity & 0x7F));
}

void RtMidiOutput::sendNoteOff(uint8_t channel, uint8_t note) {
    if (channel < 1 || channel > 16) return;
    sendThree(static_cast<unsigned char>(0x80 | (channel - 1)),
              static_cast<unsigned char>(note & 0x7F),
              0);
}

uint32_t RtMidiOutput::consumeClockTicks() {
    return clockTicks_.exchange(0, std::memory_order_relaxed);
}
