#include "stages/ui.h"

#include <algorithm>
#include "stmlib/system/system_clock.h"

using namespace std;
using namespace stmlib;

namespace stages {

const int32_t kLongPressDuration = 800; // 800ms for long press (deletes knob)

/* static */
const LedColor Ui::palette_[4] = {
  LED_COLOR_OFF,
  LED_COLOR_GREEN,
  LED_COLOR_YELLOW,
  LED_COLOR_RED
};

void Ui::Init(Settings* settings, MovementRecorder* movement_recorder) {
  leds_.Init();
  switches_.Init();
  
  system_clock.Init();
  fill(&press_time_[0], &press_time_[kNumSwitches], 0);
  fill(&click_timer_[0], &click_timer_[kNumSwitches], 0);
  fill(&click_pending_[0], &click_pending_[kNumSwitches], false);
  fill(&live_fader_[0], &live_fader_[kNumChannelsTotal], 0.0f);
  fill(&live_knob_[0], &live_knob_[kNumChannelsTotal], 0.0f);
  fill(&cv_targets_[0], &cv_targets_[kNumChannelsTotal], 0);
  fill(&slider_led_counter_[0], &slider_led_counter_[kNumLEDs], 0);
  
  settings_ = settings;
  movement_recorder_ = movement_recorder;
  mode_ = UI_MODE_NORMAL;
}

void Ui::Poll() {
  system_clock.Tick();
  UpdateLEDs();
  
  switches_.Debounce();
  
  if (movement_recorder_) {
    for (int i = 0; i < kNumSwitches; ++i) {
      if (switches_.pressed(i)) {
        if (press_time_[i] != -1) {
          if (click_pending_[i]) {
            // Second press arrived while waiting -> DOUBLE CLICK!
            click_pending_[i] = false;
            click_timer_[i] = 0;
            press_time_[i] = -1; // Prevent long-press or release click
            movement_recorder_->OnDoubleClick(i);
          } else {
            ++press_time_[i];
            if (press_time_[i] >= kLongPressDuration) {
              // Long press: deletes knob recording
              click_pending_[i] = false;
              click_timer_[i] = 0;
              press_time_[i] = -1; // Prevent repeated triggers
              movement_recorder_->OnButtonLongPress(i);
            }
          }
        }
      } else {
        if (press_time_[i] > 0) {
          // Short click released (< 800 ms) -> start double-tap window
          click_pending_[i] = true;
          click_timer_[i] = 350; // 350 ms window
        }
        press_time_[i] = 0;
      }

      // Decrement double-tap window timer (runs at 1 kHz SysTick)
      if (click_pending_[i]) {
        if (click_timer_[i] > 0) {
          --click_timer_[i];
          if (click_timer_[i] == 0) {
            // Window expired with no second tap -> confirmed SINGLE CLICK!
            click_pending_[i] = false;
            movement_recorder_->OnSingleClick(i, live_fader_[i], live_knob_[i]);
          }
        }
      }
    }
  }
}

void Ui::UpdateLEDs() {
  leds_.Clear();

  uint32_t now = system_clock.milliseconds();
  uint8_t pwm = now & 0x0F;

  for (size_t i = 0; i < kNumChannelsTotal; ++i) {
    if (!movement_recorder_) {
      continue;
    }

    // 1. Button LED
    uint8_t state = movement_recorder_->GetButtonLedState(i);
    LedColor color = LED_COLOR_OFF;

    if (state == 3) {
      // Red: Armed or Recording
      if (movement_recorder_->IsArmed(i)) {
        // Armed: Blink Red at ~4 Hz
        bool blink = (now >> 7) & 1;
        color = blink ? LED_COLOR_RED : LED_COLOR_OFF;
      } else {
        // Actively recording: Solid Red
        color = LED_COLOR_RED;
      }
    } else if (state == 1) {
      // Forward loop playing: Solid Green
      color = LED_COLOR_GREEN;
    } else if (state == 2) {
      // Ping-pong loop playing: Solid Yellow/Amber
      color = LED_COLOR_YELLOW;
    } else {
      // Idle (no loop): show CV target status
      // Brief yellow blink (every 0.8s) = CV modulates Knob, Off = CV modulates Fader
      if (cv_targets_[i] == 1) {
        color = ((now % 800) < 150) ? LED_COLOR_YELLOW : LED_COLOR_OFF;
      } else {
        color = LED_COLOR_OFF;
      }
    }
    leds_.set(LED_GROUP_UI + i, color);

    // 2. Slider LED: smooth PWM brightness tracking current fader value (live, loop, or modulated)
    float fader_val = live_fader_[i];
    int brightness = static_cast<int>(fader_val * 15.0f);
    if (brightness < 0) brightness = 0;
    if (brightness > 15) brightness = 15;

    leds_.set(
        LED_GROUP_SLIDER + i,
        (pwm <= brightness && brightness > 0) ? LED_COLOR_GREEN : LED_COLOR_OFF);
  }

  leds_.Write();
}

}  // namespace stages
