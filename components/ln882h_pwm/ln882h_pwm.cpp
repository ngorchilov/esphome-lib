#include "ln882h_pwm.h"
#include "esphome/core/log.h"

namespace esphome::ln882h_pwm {

static const char *const TAG = "ln882h_pwm";

void LN882HPWM::setup() {
  this->pin_->setup();
  const bool off_high = this->is_inverted() != this->pin_->is_inverted();
  this->initialized_ = ln882h_pwm_init(&this->hardware_, this->pin_->get_pin(), this->timer_,
                                     this->frequency_, off_high);
  if (!this->initialized_) {
    ESP_LOGE(TAG, "Unable to initialize PWM timer %u", this->timer_);
    this->mark_failed();
    return;
  }
  this->turn_off();
}

void LN882HPWM::write_state(float state) {
  if (!this->initialized_)
    return;
  if (this->pin_->is_inverted())
    state = 1.0f - state;
  ln882h_pwm_write(&this->hardware_, state);
}

void LN882HPWM::dump_config() {
  ESP_LOGCONFIG(TAG, "LN882H PWM output:\n  Timer: %u\n  Frequency: %.1f Hz", this->timer_, this->frequency_);
  LOG_PIN("  Pin: ", this->pin_);
  LOG_FLOAT_OUTPUT(this);
}

}  // namespace esphome::ln882h_pwm
