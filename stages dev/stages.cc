// Copyright 2017 Emilie Gillet.
// 
// Author: Emilie Gillet (emilie.o.gillet@gmail.com)
// 
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
// 
// See http://creativecommons.org/licenses/MIT/ for more information.

#include <stm32f37x_conf.h>
#include <algorithm>

#include "stmlib/dsp/dsp.h"
#include "stages/drivers/dac.h"
#include "stages/drivers/gate_inputs.h"
#include "stages/drivers/serial_link.h"
#include "stages/drivers/system.h"
#include "stages/cv_reader.h"
#include "stages/io_buffer.h"
#include "stages/settings.h"
#include "stages/ui.h"
#include "stages/midi_config.h"
#include "stages/movement_recorder.h"
#include "stages/midi_streamer.h"

using namespace stages;
using namespace std;
using namespace stmlib;

CvReader cv_reader;
Dac dac;
GateFlags no_gate[kBlockSize];
GateInputs gate_inputs;
IOBuffer io_buffer;
SerialLink left_link;
SerialLink right_link;
uint8_t left_rx_buffer[128];
uint8_t right_rx_buffer[128];
Settings settings;
Ui ui;

MovementRecorder movement_recorder;
MidiStreamer midi_streamer;

// Default interrupt handlers.
extern "C" {

void NMI_Handler() { }
void HardFault_Handler() { while (1); }
void MemManage_Handler() { while (1); }
void BusFault_Handler() { while (1); }
void UsageFault_Handler() { while (1); }
void SVC_Handler() { }
void DebugMon_Handler() { }
void PendSV_Handler() { }

}

// SysTick handle (1 kHz)
extern "C" {

void SysTick_Handler() {
  IWDG_ReloadCounter();
  ui.Poll();
}

}

IOBuffer::Slice FillBuffer(size_t size) {
  IOBuffer::Slice s = io_buffer.NextSlice(size);
  gate_inputs.Read(s, size);
  if (io_buffer.new_block()) {
    cv_reader.Read(s.block);
    gate_inputs.ReadNormalization(s.block);
  }
  return s;
}

void Process(IOBuffer::Block* block, size_t size) {
  // 1. Process movement recorder (simultaneous fader/knob recording, ping-pong playback, touch-to-clear)
  // Patched Gate Input 1 can optionally serve as clock sync
  bool clock_gate = block->input_patched[0] && ((block->input[0][0] & GATE_FLAG_HIGH) != 0);
  movement_recorder.Process(
      block->slider,
      block->pot,
      block->cv[0],
      clock_gate);

  // 2. Apply Time/Level CV modulation to selected target (0 = Fader [Green], 1 = Knob [Orange])
  //    Switched per channel via Double Press on the channel button!
  float effective_fader[kNumChannels];
  float effective_knob[kNumChannels];
  uint8_t fader_midi[kNumChannels];
  uint8_t knob_midi[kNumChannels];

  for (size_t c = 0; c < kNumChannels; ++c) {
    float base_f = movement_recorder.GetFaderValue(c, block->slider[c]);
    float base_k = movement_recorder.GetKnobValue(c, block->pot[c]);
    float cv_in = block->cv[c];

    if (movement_recorder.cv_target(c) == 0) {
      // CV modulates FADER
      effective_fader[c] = base_f + cv_in;
      effective_knob[c] = base_k;
    } else {
      // CV modulates KNOB
      effective_knob[c] = base_k + cv_in;
      effective_fader[c] = base_f;
    }

    CONSTRAIN(effective_fader[c], 0.0f, 1.0f);
    CONSTRAIN(effective_knob[c], 0.0f, 1.0f);

    fader_midi[c] = static_cast<uint8_t>(effective_fader[c] * 127.0f);
    knob_midi[c] = static_cast<uint8_t>(effective_knob[c] * 127.0f);
  }

  // 3. Feed values to UI for LED animations
  ui.set_live_values(effective_fader, effective_knob);

  // 4. Front-panel analog CV Output (0V to +8V) for each fader (recorded or live, modulated)
  for (size_t channel = 0; channel < kNumChannels; ++channel) {
    uint16_t dac_val = settings.dac_code(channel, effective_fader[channel]);
    for (size_t i = 0; i < size; ++i) {
      block->output[channel][i] = dac_val;
    }
  }

  // 5. MIDI CC output via rear 3.5mm jacks
  midi_streamer.Process(fader_midi, knob_midi);
}

void Init() {
  System sys;
  sys.Init(true);
  dac.Init(int(kSampleRate), 2);
  gate_inputs.Init();
  io_buffer.Init();
  
  settings.Init();
  std::fill(&no_gate[0], &no_gate[kBlockSize], GATE_FLAG_LOW);

  cv_reader.Init(&settings);

  // Initialize movement recorder
  movement_recorder.Init();

  // Left Link = rear 3.5mm jack (receives from Module 1 or outputs TRS Type B to Synth)
  left_link.Init(SERIAL_LINK_DIRECTION_LEFT, kMidiBaudRate, left_rx_buffer, sizeof(left_rx_buffer));
  // Right Link = rear 3.5mm jack (transmits to Module 2 or outputs TRS Type B to Synth)
  right_link.Init(SERIAL_LINK_DIRECTION_RIGHT, kMidiBaudRate, right_rx_buffer, sizeof(right_rx_buffer));

  // Initialize MIDI streamer
  midi_streamer.Init(&right_link, &left_link);

  ui.Init(&settings, &movement_recorder);
  
  sys.StartTimers();
  dac.Start(&FillBuffer);
}

int main(void) {
  Init();
  while (1) {
    io_buffer.Process(&Process);
  }
}
