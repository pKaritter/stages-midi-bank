#ifndef STAGES_MIDI_STREAMER_H_
#define STAGES_MIDI_STREAMER_H_

#include <stdint.h>
#include <string.h>
#include <algorithm>

#include "stages/drivers/serial_link.h"
#include "stages/midi_config.h"

namespace stages {

const size_t kMidiTxBufferSize = 512;

class MidiStreamer {
 public:
  MidiStreamer() {}
  ~MidiStreamer() {}

  void Init(SerialLink* right_link, SerialLink* left_link) {
    right_link_ = right_link;
    left_link_ = left_link;

    tx_head_right_ = 0;
    tx_tail_right_ = 0;
    tx_head_left_ = 0;
    tx_tail_left_ = 0;

    memset(last_local_fader_, 0xff, sizeof(last_local_fader_));
    memset(last_local_knob_, 0xff, sizeof(last_local_knob_));
    memset(fader_throttle_, 0, sizeof(fader_throttle_));
    memset(knob_throttle_, 0, sizeof(knob_throttle_));
    heartbeat_counter_ = 0;
    heartbeat_index_ = 0;

#if STAGES_ROLE == 1
    // MODULE 1 (Master / Synth Output):
    // Link IN (Left Jack): Powers Tip (PB9) to +3.3V for TRS Type B to Synth
    // Transmits all 24 CCs on PB8 (Ring) to Synth!
    if (left_link_) {
      left_link_->SetPowerPinHigh();
    }
#elif STAGES_ROLE == 2
    // MODULE 2 (Slave / Link Transmitter):
    // Link IN (Left Jack): Transmits CC 13-24 on PB8 (Ring) over patch cable to Module 1 Link OUT
    // Tip (PB9) remains an input (USART3_RX, unpowered)
#endif
  }

  // Called periodically (~4000 Hz) to process MIDI transmission & reception
  void Process(
      const uint8_t* local_fader_midi,
      const uint8_t* local_knob_midi) {
    
#if STAGES_ROLE == 1
    // MODULE 1: Read incoming CC 13-24 from Module 2 (received on Link OUT PC5)
    // and forward directly to Link IN (Synth output)!
    if (right_link_) {
      while (right_link_->available() > 0) {
        uint8_t byte = right_link_->Read();
        PushByteLeft(byte);
      }
    }
#endif

    // Decrement throttle timers
    for (size_t i = 0; i < 6; ++i) {
      if (fader_throttle_[i] > 0) --fader_throttle_[i];
      if (knob_throttle_[i] > 0) --knob_throttle_[i];
    }

#if STAGES_ROLE == 1
    const uint8_t midi_channel = kMod1MidiChannel;
    const uint8_t* fader_cc = kMod1FaderCC;
    const uint8_t* knob_cc = kMod1KnobCC;
#else
    const uint8_t midi_channel = kMod2MidiChannel;
    const uint8_t* fader_cc = kMod2FaderCC;
    const uint8_t* knob_cc = kMod2KnobCC;
#endif

    // Process local controls with rate limiting (~80 updates/sec at 4000Hz)
    for (size_t i = 0; i < 6; ++i) {
      if (local_fader_midi[i] != last_local_fader_[i]) {
        if (last_local_fader_[i] == 0xff || fader_throttle_[i] == 0) {
          last_local_fader_[i] = local_fader_midi[i];
          fader_throttle_[i] = 50; // Max ~80 updates/sec per fader
          QueueMidiCC(midi_channel, fader_cc[i], local_fader_midi[i]);
        }
      }

      if (local_knob_midi[i] != last_local_knob_[i]) {
        if (last_local_knob_[i] == 0xff || knob_throttle_[i] == 0) {
          last_local_knob_[i] = local_knob_midi[i];
          knob_throttle_[i] = 50; // Max ~80 updates/sec per knob
          QueueMidiCC(midi_channel, knob_cc[i], local_knob_midi[i]);
        }
      }
    }

    // Continuous background refresh: resend one CC every ~25ms (100 ticks @ 3906Hz)
    // Ensures Stages is ALWAYS actively streaming MIDI and synths never lose sync
    ++heartbeat_counter_;
    if (heartbeat_counter_ >= 100) {
      heartbeat_counter_ = 0;
      if (heartbeat_index_ < 6) {
        QueueMidiCC(midi_channel, fader_cc[heartbeat_index_], local_fader_midi[heartbeat_index_]);
      } else if (heartbeat_index_ < 12) {
        size_t k_idx = heartbeat_index_ - 6;
        QueueMidiCC(midi_channel, knob_cc[k_idx], local_knob_midi[k_idx]);
      }
      heartbeat_index_ = (heartbeat_index_ + 1) % 12;
    }

    // Flush outgoing transmissions on Link IN (left_link_)
    FlushTxLeft();
  }

  inline bool is_slave() const { return STAGES_ROLE == 2; }

 private:
  SerialLink* right_link_;
  SerialLink* left_link_;

  uint8_t tx_buffer_right_[kMidiTxBufferSize];
  volatile size_t tx_head_right_;
  volatile size_t tx_tail_right_;
  uint8_t dma_tx_chunk_right_[32];

  uint8_t tx_buffer_left_[kMidiTxBufferSize];
  volatile size_t tx_head_left_;
  volatile size_t tx_tail_left_;
  uint8_t dma_tx_chunk_left_[32];

  uint8_t last_local_fader_[6];
  uint8_t last_local_knob_[6];
  uint8_t fader_throttle_[6];
  uint8_t knob_throttle_[6];
  uint32_t heartbeat_counter_;
  uint8_t heartbeat_index_;

  void QueueMidiCC(uint8_t channel, uint8_t cc, uint8_t value) {
    uint8_t status = 0xB0 | ((channel - 1) & 0x0F);

    // Both Module 1 and Module 2 transmit out of left_link_ (Link IN)!
    // Module 1 transmits CC 1-12 to Synth.
    // Module 2 transmits CC 13-24 over patch cable to Module 1.
    size_t head_l = tx_head_left_;
    size_t tail_l = tx_tail_left_;
    size_t free_l = (tail_l > head_l)
        ? (tail_l - head_l - 1)
        : (kMidiTxBufferSize - (head_l - tail_l) - 1);

    if (free_l >= 3) {
      PushByteLeft(status);
      PushByteLeft(cc & 0x7F);
      PushByteLeft(value & 0x7F);
    }
  }

  void PushByteLeft(uint8_t b) {
    size_t next_head = (tx_head_left_ + 1) % kMidiTxBufferSize;
    if (next_head != tx_tail_left_) {
      tx_buffer_left_[tx_head_left_] = b;
      tx_head_left_ = next_head;
    }
  }

  void FlushTxLeft() {
    if (!left_link_ || !left_link_->tx_complete()) {
      return;
    }

    if (tx_head_left_ == tx_tail_left_) {
      return;
    }

    size_t available = (tx_head_left_ >= tx_tail_left_)
        ? (tx_head_left_ - tx_tail_left_)
        : (kMidiTxBufferSize - tx_tail_left_ + tx_head_left_);

    if (available == 0) {
      return;
    }

    size_t count = std::min(available, sizeof(dma_tx_chunk_left_));
    for (size_t i = 0; i < count; ++i) {
      dma_tx_chunk_left_[i] = tx_buffer_left_[tx_tail_left_];
      tx_tail_left_ = (tx_tail_left_ + 1) % kMidiTxBufferSize;
    }

    left_link_->Transmit(dma_tx_chunk_left_, count);
  }
};

}  // namespace stages

#endif  // STAGES_MIDI_STREAMER_H_
