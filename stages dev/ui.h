#ifndef STAGES_UI_H_
#define STAGES_UI_H_

#include "stmlib/stmlib.h"
#include "stmlib/ui/event_queue.h"

#include "stages/drivers/leds.h"
#include "stages/drivers/switches.h"
#include "stages/settings.h"
#include "stages/movement_recorder.h"

namespace stages {

enum UiMode {
  UI_MODE_NORMAL,
  UI_MODE_FACTORY_TEST
};

class Ui {
 public:
  Ui() : movement_recorder_(NULL) { }
  ~Ui() { }
  
  void Init(Settings* settings, MovementRecorder* movement_recorder);
  void Poll();
  void DoEvents() {}

  void set_live_values(const float* fader, const float* knob) {
    for (size_t i = 0; i < kNumLEDs; ++i) {
      live_fader_[i] = fader[i];
      live_knob_[i] = knob[i];
    }
  }

  void set_slider_led(int i, bool value, int duration) {
    if (value) {
      slider_led_counter_[i] = duration;
    }
  }
  
  inline void set_factory_test(bool factory_test) {
    mode_ = factory_test ? UI_MODE_FACTORY_TEST : UI_MODE_NORMAL;
  }
  
  inline const Switches& switches() const { return switches_; }
  
 private:
  void UpdateLEDs();

  Leds leds_;
  Switches switches_;
  
  int slider_led_counter_[kNumLEDs];
  int press_time_[kNumSwitches];
  int click_timer_[kNumSwitches];
  bool click_pending_[kNumSwitches];
  float live_fader_[kNumChannelsTotal];
  float live_knob_[kNumChannelsTotal];

  Settings* settings_;
  MovementRecorder* movement_recorder_;
  
  UiMode mode_;

  static const LedColor palette_[4];
  
  DISALLOW_COPY_AND_ASSIGN(Ui);
};

}  // namespace stages

#endif  // STAGES_UI_H_
