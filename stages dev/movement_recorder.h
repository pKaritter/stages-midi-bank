#ifndef STAGES_MOVEMENT_RECORDER_H_
#define STAGES_MOVEMENT_RECORDER_H_

#include <stdint.h>
#include <string.h>
#include <algorithm>

namespace stages {

const size_t kNumChannelsTotal = 6;
// 500 samples at 50Hz = 10 seconds per loop track
const size_t kMaxLoopSamples = 500;
const float kMotionThreshold = 0.035f;

enum LoopMode {
  LOOP_MODE_FORWARD = 0,
  LOOP_MODE_PINGPONG = 1,
};

enum TrackState {
  TRACK_STATE_IDLE = 0,
  TRACK_STATE_ARMED = 1,
  TRACK_STATE_RECORDING = 2,
  TRACK_STATE_PLAYING = 3,
};

struct LoopTrack {
  uint8_t buffer[kMaxLoopSamples];
  size_t length;
  float playback_phase;      // Floating point phase for interpolation
  bool pingpong_reverse;
  TrackState state;
  LoopMode mode;
  float baseline;

  void Init() {
    memset(buffer, 0, sizeof(buffer));
    length = 0;
    playback_phase = 0.0f;
    pingpong_reverse = false;
    state = TRACK_STATE_IDLE;
    mode = LOOP_MODE_FORWARD;
    baseline = 0.0f;
  }

  void Clear() {
    length = 0;
    playback_phase = 0.0f;
    pingpong_reverse = false;
    state = TRACK_STATE_IDLE;
  }

  void Arm(float current_val) {
    baseline = current_val;
    state = TRACK_STATE_ARMED;
  }

  void StartRecording(float current_val) {
    state = TRACK_STATE_RECORDING;
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

  void StopRecording(size_t quantized_len = 0) {
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
        playback_phase = 0.0f;
        pingpong_reverse = false;
      }
    } else if (state == TRACK_STATE_ARMED) {
      // Disarm if nothing was recorded
      state = (length > 0) ? TRACK_STATE_PLAYING : TRACK_STATE_IDLE;
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
      // Ping-pong mode
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

  void ToggleMode() {
    mode = (mode == LOOP_MODE_FORWARD) ? LOOP_MODE_PINGPONG : LOOP_MODE_FORWARD;
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
    }
    clock_interval_ = 0;
    clock_timer_ = 0;
    prev_clock_state_ = false;
    subsample_ = 0;
  }

  // Called at 500 Hz (every 2ms) from main UI/audio loop
  void Process(
      const float* fader,
      const float* knob,
      float clock_input,
      bool clock_gate_input) {
    
    // 1. Clock detection on Time/CV Input 1 or Gate Input 1
    DetectClock(clock_input, clock_gate_input);

    // 2. Subsample to 50 Hz (every 20ms = 10 ticks) for gesture recording/playback
    ++subsample_;
    if (subsample_ >= 10) {
      subsample_ = 0;

      for (size_t i = 0; i < kNumChannelsTotal; ++i) {
        ProcessChannel50Hz(i, fader[i], knob[i]);
      }
    }
  }

  void OnSingleClick(size_t channel, float live_fader, float live_knob) {
    if (channel >= kNumChannelsTotal) return;

    if (fader_track_[channel].state == TRACK_STATE_RECORDING ||
        knob_track_[channel].state == TRACK_STATE_RECORDING) {
      // 1. If currently recording, close and commit the loop!
      size_t q_len = QuantizeLength(
          fader_track_[channel].state == TRACK_STATE_RECORDING
              ? fader_track_[channel].length
              : knob_track_[channel].length);
      fader_track_[channel].StopRecording(q_len);
      knob_track_[channel].StopRecording(q_len);
      channel_armed_[channel] = false;
    } else if (channel_armed_[channel]) {
      // 2. If armed and user clicked without moving anything -> cancel arm
      channel_armed_[channel] = false;
      fader_track_[channel].StopRecording();
      knob_track_[channel].StopRecording();
    } else if (fader_track_[channel].state == TRACK_STATE_PLAYING ||
               knob_track_[channel].state == TRACK_STATE_PLAYING) {
      // 3. If playing, toggle between Forward and Ping-Pong loop mode
      if (fader_track_[channel].state == TRACK_STATE_PLAYING) {
        fader_track_[channel].ToggleMode();
      }
      if (knob_track_[channel].state == TRACK_STATE_PLAYING) {
        knob_track_[channel].ToggleMode();
      }
    } else {
      // 4. If idle, arm channel and wait to see what is moved first
      channel_armed_[channel] = true;
      fader_track_[channel].Arm(live_fader);
      knob_track_[channel].Arm(live_knob);
    }
  }

  void OnDoubleClick(size_t channel) {
    if (channel >= kNumChannelsTotal) return;
    // Double click deletes fader recording and returns to live
    fader_track_[channel].Clear();
    channel_armed_[channel] = false;
  }

  void OnButtonLongPress(size_t channel) {
    if (channel >= kNumChannelsTotal) return;
    // Long press deletes knob recording and returns to live
    knob_track_[channel].Clear();
    channel_armed_[channel] = false;
  }

  // Get current fader output value (0.0f to 1.0f) for CV OUT jack and Slider LED
  float GetFaderValue(size_t channel, float live_fader) const {
    if (channel >= kNumChannelsTotal) return live_fader;
    return fader_track_[channel].GetCurrentValue(live_fader);
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

  // Button LED color state for UI
  // Returns: 0 = Off, 1 = Green (Normal loop), 2 = Yellow (PingPong), 3 = Red (Armed/Recording)
  uint8_t GetButtonLedState(size_t channel) const {
    if (channel >= kNumChannelsTotal) return 0;

    // Recording or Armed takes precedence (Red)
    if (channel_armed_[channel] ||
        fader_track_[channel].state == TRACK_STATE_RECORDING ||
        knob_track_[channel].state == TRACK_STATE_RECORDING) {
      return 3; // Red
    }

    // If either track is playing, show the loop mode
    if (fader_track_[channel].state == TRACK_STATE_PLAYING ||
        knob_track_[channel].state == TRACK_STATE_PLAYING) {
      LoopMode mode = (fader_track_[channel].state == TRACK_STATE_PLAYING)
          ? fader_track_[channel].mode
          : knob_track_[channel].mode;
      return (mode == LOOP_MODE_PINGPONG) ? 2 : 1; // 2 = Yellow, 1 = Green
    }

    return 0; // Off
  }

  bool IsArmed(size_t channel) const {
    return (channel < kNumChannelsTotal) && channel_armed_[channel];
  }

  const LoopTrack& fader_track(size_t channel) const { return fader_track_[channel]; }
  const LoopTrack& knob_track(size_t channel) const { return knob_track_[channel]; }

 private:
  LoopTrack fader_track_[kNumChannelsTotal];
  LoopTrack knob_track_[kNumChannelsTotal];
  bool channel_armed_[kNumChannelsTotal];

  // Clock tracking
  uint32_t clock_timer_;
  uint32_t clock_interval_; // in 50Hz ticks
  bool prev_clock_state_;
  uint8_t subsample_;

  void ProcessChannel50Hz(size_t i, float live_fader, float live_knob) {
    // Check for first movement if armed
    if (channel_armed_[i]) {
      float d_fader = std::abs(live_fader - fader_track_[i].baseline);
      float d_knob = std::abs(live_knob - knob_track_[i].baseline);

      if (d_fader > kMotionThreshold && d_fader >= d_knob) {
        // FADER moved first! Start recording fader track only
        channel_armed_[i] = false;
        fader_track_[i].StartRecording(live_fader);
        knob_track_[i].StopRecording(); // disarm knob track
      } else if (d_knob > kMotionThreshold) {
        // KNOB moved first! Start recording knob track only
        channel_armed_[i] = false;
        knob_track_[i].StartRecording(live_knob);
        fader_track_[i].StopRecording(); // disarm fader track
      }
    }

    // Record sample if recording
    if (fader_track_[i].state == TRACK_STATE_RECORDING) {
      fader_track_[i].RecordSample(live_fader);
    }
    if (knob_track_[i].state == TRACK_STATE_RECORDING) {
      knob_track_[i].RecordSample(live_knob);
    }

    // Step playback
    fader_track_[i].StepPlayback();
    knob_track_[i].StepPlayback();
  }

  void DetectClock(float clock_cv, bool clock_gate) {
    // CV input > 1.5V or digital Gate input high
    bool is_high = (clock_cv > 0.18f) || clock_gate;

    if (is_high && !prev_clock_state_) {
      // Rising edge detected
      if (clock_timer_ > 10 && clock_timer_ < 1000) {
        clock_interval_ = clock_timer_;
      }
      clock_timer_ = 0;

      // Resync playback phase to prevent drift
      for (size_t i = 0; i < kNumChannelsTotal; ++i) {
        if (fader_track_[i].state == TRACK_STATE_PLAYING && clock_interval_ > 0) {
          // Soft-align phase at beat boundaries
        }
      }
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
    // Snap to nearest multiple of clock interval
    size_t beats = (raw_len + clock_interval_ / 2) / clock_interval_;
    if (beats < 1) beats = 1;
    size_t q_len = beats * clock_interval_;
    return std::min(q_len, kMaxLoopSamples);
  }
};

}  // namespace stages

#endif  // STAGES_MOVEMENT_RECORDER_H_

