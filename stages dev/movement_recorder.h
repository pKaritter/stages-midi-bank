#ifndef STAGES_MOVEMENT_RECORDER_H_
#define STAGES_MOVEMENT_RECORDER_H_

#include <stdint.h>
#include <string.h>
#include <algorithm>
#include <cmath>

namespace stages {

const size_t kNumChannelsTotal = 6;
// 500 samples at 50Hz = 10 seconds per loop track
const size_t kMaxLoopSamples = 500;
const float kMotionThreshold = 0.035f;
const float kTouchClearThreshold = 0.045f;

enum LoopMode {
  LOOP_MODE_FORWARD = 0,
  LOOP_MODE_PINGPONG = 1,
};

enum TrackState {
  TRACK_STATE_IDLE = 0,
  TRACK_STATE_RECORDING = 1,
  TRACK_STATE_PLAYING = 2,
};

struct LoopTrack {
  uint8_t buffer[kMaxLoopSamples];
  size_t length;
  float playback_phase;      // Floating point phase for interpolation
  bool pingpong_reverse;
  TrackState state;
  LoopMode mode;
  float play_baseline;       // Physical resting position when loop started (for touch-to-clear)

  void Init() {
    memset(buffer, 0, sizeof(buffer));
    length = 0;
    playback_phase = 0.0f;
    pingpong_reverse = false;
    state = TRACK_STATE_IDLE;
    mode = LOOP_MODE_PINGPONG; // Always Ping-Pong by default
    play_baseline = 0.0f;
  }

  void Clear() {
    length = 0;
    playback_phase = 0.0f;
    pingpong_reverse = false;
    state = TRACK_STATE_IDLE;
  }

  void StartRecording(float current_val) {
    state = TRACK_STATE_RECORDING;
    mode = LOOP_MODE_PINGPONG;
    length = 0;
    buffer[0] = static_cast<uint8_t>(std::max(0.0f, std::min(127.0f, current_val * 127.0f)));
    length = 1;
    playback_phase = 0.0f;
    pingpong_reverse = false;
  }

  void RecordSample(float current_val) {
    if (state == TRACK_STATE_RECORDING && length < kMaxLoopSamples) {
      uint8_t val = static_cast<uint8_t>(std::max(0.0f, std::min(127.0f, current_val * 127.0f)));
      buffer[length++] = val;
    }
  }

  void StopRecording(float resting_val, size_t quantized_len = 0) {
    if (state == TRACK_STATE_RECORDING) {
      if (length < 4) {
        // Too short, abort
        state = TRACK_STATE_IDLE;
        length = 0;
      } else {
        if (quantized_len > 4 && quantized_len <= kMaxLoopSamples) {
          length = quantized_len;
        }
        state = TRACK_STATE_PLAYING;
        mode = LOOP_MODE_PINGPONG;
        playback_phase = 0.0f;
        pingpong_reverse = false;
        play_baseline = resting_val;
      }
    }
  }

  void StepPlayback() {
    if (state != TRACK_STATE_PLAYING || length <= 1) {
      return;
    }

    if (mode == LOOP_MODE_FORWARD) {
      playback_phase += 1.0f;
      if (playback_phase >= static_cast<float>(length)) {
        playback_phase -= static_cast<float>(length);
      }
    } else {
      // Ping-pong mode (default)
      if (!pingpong_reverse) {
        playback_phase += 1.0f;
        if (playback_phase >= static_cast<float>(length - 1)) {
          playback_phase = static_cast<float>(length - 1);
          pingpong_reverse = true;
        }
      } else {
        playback_phase -= 1.0f;
        if (playback_phase <= 0.0f) {
          playback_phase = 0.0f;
          pingpong_reverse = false;
        }
      }
    }
  }

  float GetCurrentValue(float live_val) const {
    if (state != TRACK_STATE_PLAYING || length == 0) {
      return live_val;
    }

    size_t idx0 = static_cast<size_t>(playback_phase);
    if (idx0 >= length) idx0 = length - 1;
    size_t idx1 = idx0 + 1;
    if (idx1 >= length) {
      idx1 = (mode == LOOP_MODE_FORWARD) ? 0 : (length > 1 ? length - 2 : 0);
    }

    float frac = playback_phase - static_cast<float>(idx0);
    float v0 = static_cast<float>(buffer[idx0]) / 127.0f;
    float v1 = static_cast<float>(buffer[idx1]) / 127.0f;
    return v0 + frac * (v1 - v0);
  }

  uint8_t GetMidiValue(float live_val) const {
    float val = GetCurrentValue(live_val);
    int midi = static_cast<int>(val * 127.0f + 0.5f);
    return static_cast<uint8_t>(std::max(0, std::min(127, midi)));
  }
};

class MovementRecorder {
 public:
  MovementRecorder() {}
  ~MovementRecorder() {}

  void Init() {
    for (size_t i = 0; i < kNumChannelsTotal; ++i) {
      fader_track_[i].Init();
      knob_track_[i].Init();
      channel_armed_[i] = false;
      cv_target_[i] = 0; // Default: CV modulates Fader (Green)
      arm_fader_baseline_[i] = 0.0f;
      arm_knob_baseline_[i] = 0.0f;
      last_slider_[i] = 0.0f;
      last_pot_[i] = 0.0f;
    }
    clock_interval_ = 0;
    clock_timer_ = 0;
    prev_clock_state_ = false;
    subsample_ = 0;
  }

  // Called at ~3906 Hz from main audio/IO loop
  void Process(
      const float* fader,
      const float* knob,
      float clock_input,
      bool clock_gate_input) {
    
    for (size_t i = 0; i < kNumChannelsTotal; ++i) {
      last_slider_[i] = fader[i];
      last_pot_[i] = knob[i];
    }

    // 1. Clock detection on patched Gate Input 1
    DetectClock(clock_input, clock_gate_input);

    // 2. Subsample to ~50 Hz for gesture recording/playback (3906 / 78 ≈ 50 Hz)
    ++subsample_;
    if (subsample_ >= 78) {
      subsample_ = 0;

      for (size_t i = 0; i < kNumChannelsTotal; ++i) {
        ProcessChannel50Hz(i, fader[i], knob[i]);
      }
    }
  }

  // Short press: Arm for recording (fader, knob, or BOTH simultaneously!) / Stop & Loop in Ping-Pong
  void OnSingleClick(size_t channel, float /*live_fader*/, float /*live_knob*/) {
    if (channel >= kNumChannelsTotal) return;

    bool is_recording = (fader_track_[channel].state == TRACK_STATE_RECORDING) ||
                        (knob_track_[channel].state == TRACK_STATE_RECORDING);

    if (is_recording) {
      // Stop recording and commit whichever track(s) were recorded into Ping-Pong playback!
      size_t rec_len = 0;
      if (fader_track_[channel].state == TRACK_STATE_RECORDING) {
        rec_len = fader_track_[channel].length;
      }
      if (knob_track_[channel].state == TRACK_STATE_RECORDING && knob_track_[channel].length > rec_len) {
        rec_len = knob_track_[channel].length;
      }
      size_t q_len = QuantizeLength(rec_len);

      if (fader_track_[channel].state == TRACK_STATE_RECORDING) {
        fader_track_[channel].StopRecording(last_slider_[channel], q_len);
      }
      if (knob_track_[channel].state == TRACK_STATE_RECORDING) {
        knob_track_[channel].StopRecording(last_pot_[channel], q_len);
      }
      channel_armed_[channel] = false;
    } else if (channel_armed_[channel]) {
      // Armed without moving anything -> cancel arming
      channel_armed_[channel] = false;
    } else {
      // Arm channel: moving fader, knob, or BOTH simultaneously will record them!
      channel_armed_[channel] = true;
      arm_fader_baseline_[channel] = last_slider_[channel];
      arm_knob_baseline_[channel] = last_pot_[channel];
    }
  }

  // Double press: Toggle CV modulation target between Fader (Green) and Knob (Orange)
  void OnDoubleClick(size_t channel) {
    if (channel >= kNumChannelsTotal) return;
    cv_target_[channel] ^= 1;
  }

  // Long press (>= 800ms): Clear both loops on this channel and disarm
  void OnButtonLongPress(size_t channel) {
    if (channel >= kNumChannelsTotal) return;
    fader_track_[channel].Clear();
    knob_track_[channel].Clear();
    channel_armed_[channel] = false;
  }

  inline uint8_t cv_target(size_t channel) const {
    return (channel < kNumChannelsTotal) ? cv_target_[channel] : 0;
  }

  // Get current fader output value (0.0f to 1.0f) for CV OUT jack and Slider LED
  float GetFaderValue(size_t channel, float live_fader) const {
    if (channel >= kNumChannelsTotal) return live_fader;
    return fader_track_[channel].GetCurrentValue(live_fader);
  }

  // Get current knob output value (0.0f to 1.0f)
  float GetKnobValue(size_t channel, float live_knob) const {
    if (channel >= kNumChannelsTotal) return live_knob;
    return knob_track_[channel].GetCurrentValue(live_knob);
  }

  // Get current fader 7-bit MIDI value (0 to 127)
  uint8_t GetFaderMidi(size_t channel, float live_fader) const {
    if (channel >= kNumChannelsTotal) return static_cast<uint8_t>(live_fader * 127.0f);
    return fader_track_[channel].GetMidiValue(live_fader);
  }

  // Get current knob 7-bit MIDI value (0 to 127)
  uint8_t GetKnobMidi(size_t channel, float live_knob) const {
    if (channel >= kNumChannelsTotal) return static_cast<uint8_t>(live_knob * 127.0f);
    return knob_track_[channel].GetMidiValue(live_knob);
  }

  bool IsRecording(size_t channel) const {
    if (channel >= kNumChannelsTotal) return false;
    return fader_track_[channel].state == TRACK_STATE_RECORDING ||
           knob_track_[channel].state == TRACK_STATE_RECORDING;
  }

  bool IsArmed(size_t channel) const {
    return (channel < kNumChannelsTotal) && channel_armed_[channel] && !IsRecording(channel);
  }

  bool IsPlaying(size_t channel) const {
    if (channel >= kNumChannelsTotal) return false;
    return fader_track_[channel].state == TRACK_STATE_PLAYING ||
           knob_track_[channel].state == TRACK_STATE_PLAYING;
  }

  const LoopTrack& fader_track(size_t channel) const { return fader_track_[channel]; }
  const LoopTrack& knob_track(size_t channel) const { return knob_track_[channel]; }

 private:
  LoopTrack fader_track_[kNumChannelsTotal];
  LoopTrack knob_track_[kNumChannelsTotal];
  bool channel_armed_[kNumChannelsTotal];
  uint8_t cv_target_[kNumChannelsTotal]; // 0 = Fader (Green), 1 = Knob (Orange)
  float arm_fader_baseline_[kNumChannelsTotal];
  float arm_knob_baseline_[kNumChannelsTotal];
  float last_slider_[kNumChannelsTotal];
  float last_pot_[kNumChannelsTotal];

  // Clock tracking
  uint32_t clock_timer_;
  uint32_t clock_interval_; // in 50Hz ticks
  bool prev_clock_state_;
  uint8_t subsample_;

  void ProcessChannel50Hz(size_t i, float live_fader, float live_knob) {
    // 1. If armed (or already recording one track), watch BOTH fader and knob!
    //    This allows recording fader only, knob only, or BOTH simultaneously!
    if (channel_armed_[i]) {
      if (fader_track_[i].state != TRACK_STATE_RECORDING) {
        if (std::abs(live_fader - arm_fader_baseline_[i]) > kMotionThreshold) {
          fader_track_[i].StartRecording(live_fader);
        }
      }
      if (knob_track_[i].state != TRACK_STATE_RECORDING) {
        if (std::abs(live_knob - arm_knob_baseline_[i]) > kMotionThreshold) {
          knob_track_[i].StartRecording(live_knob);
        }
      }
    } else {
      // 2. Not armed/recording: Touch-to-Clear!
      //    Moving the fader clears the fader loop; moving the knob clears the knob loop!
      if (fader_track_[i].state == TRACK_STATE_PLAYING) {
        if (std::abs(live_fader - fader_track_[i].play_baseline) > kTouchClearThreshold) {
          fader_track_[i].Clear();
        }
      }
      if (knob_track_[i].state == TRACK_STATE_PLAYING) {
        if (std::abs(live_knob - knob_track_[i].play_baseline) > kTouchClearThreshold) {
          knob_track_[i].Clear();
        }
      }
    }

    // 3. Record samples (and auto-close into Ping-Pong if max 10s buffer is reached)
    if (fader_track_[i].state == TRACK_STATE_RECORDING) {
      fader_track_[i].RecordSample(live_fader);
      if (fader_track_[i].length >= kMaxLoopSamples) {
        fader_track_[i].StopRecording(live_fader);
        if (knob_track_[i].state != TRACK_STATE_RECORDING) {
          channel_armed_[i] = false;
        }
      }
    }
    if (knob_track_[i].state == TRACK_STATE_RECORDING) {
      knob_track_[i].RecordSample(live_knob);
      if (knob_track_[i].length >= kMaxLoopSamples) {
        knob_track_[i].StopRecording(live_knob);
        if (fader_track_[i].state != TRACK_STATE_RECORDING) {
          channel_armed_[i] = false;
        }
      }
    }

    // 4. Step playback
    fader_track_[i].StepPlayback();
    knob_track_[i].StepPlayback();
  }

  void DetectClock(float /*clock_cv*/, bool clock_gate) {
    // Only use patched Gate Input 1 for clock quantization
    bool is_high = clock_gate;

    if (is_high && !prev_clock_state_) {
      if (clock_timer_ > 10 && clock_timer_ < 1000) {
        clock_interval_ = clock_timer_;
      }
      clock_timer_ = 0;
    } else {
      if (clock_timer_ < 1000) {
        ++clock_timer_;
      }
    }
    prev_clock_state_ = is_high;
  }

  size_t QuantizeLength(size_t raw_len) const {
    if (clock_interval_ < 5 || raw_len < 4) {
      return raw_len;
    }
    size_t beats = (raw_len + clock_interval_ / 2) / clock_interval_;
    if (beats < 1) beats = 1;
    size_t q_len = beats * clock_interval_;
    return std::min(q_len, kMaxLoopSamples);
  }
};

}  // namespace stages

#endif  // STAGES_MOVEMENT_RECORDER_H_
