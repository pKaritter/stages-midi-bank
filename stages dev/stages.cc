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
  // 1. Process movement recorder (recording, playback, clock quantization)
  // Time/CV Input 1 or Gate Input 1 serves as clock sync
  bool clock_gate = (block->input[0][0] & GATE_FLAG_HIGH) != 0;
  movement_recorder.Process(
      block->cv_slider,
      block->pot,
      block->cv_slider[0],
      clock_gate);

  // 2. Feed live fader & knob positions to UI for LED animation
  ui.set_live_values(block->cv_slider, block->pot);

  // 3. Front-panel analog CV Output (0V to +8V) for each fader (recorded or live)
  for (size_t channel = 0; channel < kNumChannels; ++channel) {
    float cv_val = movement_recorder.GetFaderValue(channel, block->cv_slider[channel]);
    uint16_t dac_val = settings.dac_code(channel, cv_val);
    for (size_t i = 0; i < size; ++i) {
      block->output[channel][i] = dac_val;
    }
  }

  // 4. MIDI CC output via rear 3.5mm jacks (both Left and Right)
  uint8_t fader_midi[kNumChannels];
  uint8_t knob_midi[kNumChannels];
  for (size_t channel = 0; channel < kNumChannels; ++channel) {
    fader_midi[channel] = movement_recorder.GetFaderMidi(channel, block->cv_slider[channel]);
    knob_midi[channel] = movement_recorder.GetKnobMidi(channel, block->pot[channel]);
  }

  // Emit standard 7-bit MIDI CC messages
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
