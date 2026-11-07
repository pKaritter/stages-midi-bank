#ifndef STAGES_MIDI_CONFIG_H_
#define STAGES_MIDI_CONFIG_H_

#include <stdint.h>

// =============================================================================
// MIDI CONFIGURATION FOR BEHRINGER STEPS / MI STAGES
// =============================================================================
// Edit this file to customize MIDI channels, CC assignments, and baud rate.

namespace stages {

// Standard MIDI is 31250 baud.
// (Use 115200 only if connecting directly to a microcontroller or USB serial).
const uint32_t kMidiBaudRate = 31250;

// Set module chain role: 1 = Master (CC 1-12), 2 = Slave (CC 13-24)
#ifndef STAGES_ROLE
#define STAGES_ROLE 1
#endif

// Set to 1 to invert the serial TX signal polarity on the rear jacks.
// Use this if the hardware has an inverting buffer between the MCU and the
// jack (MIDI requires the line to idle HIGH and pull LOW for data).
#ifndef MIDI_INVERT_TX
#define MIDI_INVERT_TX 0
#endif

// -----------------------------------------------------------------------------
// MODULE 1 (Single module or Master / Left in a chained pair)
// -----------------------------------------------------------------------------
const uint8_t kMod1MidiChannel = 1;     // MIDI Channel (1 - 16)

// MIDI CC numbers for the 6 Faders (Sliders 1 to 6)
const uint8_t kMod1FaderCC[6] = {
  1,   // Fader 1 (Modulation Wheel)
  2,   // Fader 2 (Breath)
  3,   // Fader 3
  4,   // Fader 4 (Foot Controller)
  5,   // Fader 5 (Portamento Time)
  6    // Fader 6 (Data Entry)
};

// MIDI CC numbers for the 6 small Knobs (Pots 1 to 6)
const uint8_t kMod1KnobCC[6] = {
  7,   // Knob 1 (Volume)
  8,   // Knob 2 (Balance)
  9,   // Knob 3
  10,  // Knob 4 (Pan)
  11,  // Knob 5 (Expression)
  12   // Knob 6
};

// -----------------------------------------------------------------------------
// MODULE 2 (Slave / Right module when chained via the rear 3.5mm link cable)
// -----------------------------------------------------------------------------
const uint8_t kMod2MidiChannel = 1;     // MIDI Channel (1 - 16)

// MIDI CC numbers for Module 2 Faders (Sliders 7 to 12)
const uint8_t kMod2FaderCC[6] = {
  13,  // Fader 7
  14,  // Fader 8
  15,  // Fader 9
  16,  // Fader 10
  17,  // Fader 11
  18   // Fader 12
};

// MIDI CC numbers for Module 2 Knobs (Pots 7 to 12)
const uint8_t kMod2KnobCC[6] = {
  19,  // Knob 7
  20,  // Knob 8
  21,  // Knob 9
  22,  // Knob 10
  23,  // Knob 11
  24   // Knob 12
};

}  // namespace stages

#endif  // STAGES_MIDI_CONFIG_H_



