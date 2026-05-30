// Overrides the weak USB string descriptors in the Teensy 4 core so the
// device identifies itself to the host OS and to MIDI hosts as "MIDIops"
// instead of the defaults ("Teensyduino" / "Teensy MIDI/Serial" /
// "Teensy MIDI"). This is the documented PJRC name.c convention:
//   https://www.pjrc.com/teensy/td_midi.html
//
// The three string structs below are declared __attribute__((weak)) inside
// cores/teensy4/usb_desc.c; providing strong (non-weak) definitions here
// makes the linker pick ours.
//
// Each string is stored as an array of uint16_t (USB strings are UTF-16LE).
// The bLength field is `2 + len * 2` — two header bytes plus two bytes per
// character. Keep len in sync with the literal length.

#include "usb_names.h"

#define PRODUCT_NAME       {'M','I','D','I','o','p','s'}
#define PRODUCT_NAME_LEN   7

struct usb_string_descriptor_struct usb_string_manufacturer_name = {
    2 + PRODUCT_NAME_LEN * 2,
    3,
    PRODUCT_NAME
};

struct usb_string_descriptor_struct usb_string_product_name = {
    2 + PRODUCT_NAME_LEN * 2,
    3,
    PRODUCT_NAME
};

#ifdef MIDI_INTERFACE
struct usb_string_descriptor_struct usb_string_midi_name = {
    2 + PRODUCT_NAME_LEN * 2,
    3,
    PRODUCT_NAME
};
#endif
