#include "RtMidiOutput.h"

#include <RtMidi.h>

#include <cstdio>
#include <utility>

RtMidiOutput::RtMidiOutput(std::string portName)
    : portName_(std::move(portName)) {}

RtMidiOutput::~RtMidiOutput() {
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
    if (!midi_) return;
    midi_->sendMessage(&status, 1);
}

void RtMidiOutput::sendClock()    { sendByte(0xF8); }
void RtMidiOutput::sendStart()    { sendByte(0xFA); }
void RtMidiOutput::sendContinue() { sendByte(0xFB); }
void RtMidiOutput::sendStop()     { sendByte(0xFC); }
