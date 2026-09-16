#pragma once

#include "esphome/components/output/float_output.h"
#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "ln882h_pwm_hal.h"

namespace esphome::ln882h_pwm {

class LN882HPWM : public output::FloatOutput, public Component {
 public:
  explicit LN882HPWM(InternalGPIOPin *pin) : pin_(pin) {}
  void set_timer(uint8_t timer) { this->timer_ = timer; }
  void set_frequency(float frequency) { this->frequency_ = frequency; }
  void setup() override;
  void dump_config() override;
  void on_shutdown() override { this->turn_off(); }
  float get_setup_priority() const override { return setup_priority::HARDWARE; }

 protected:
  void write_state(float state) override;

  InternalGPIOPin *pin_;
  uint8_t timer_{0};
  float frequency_{4000.0f};
  bool initialized_{false};
  ln882h_pwm_state_t hardware_{};
};

}  // namespace esphome::ln882h_pwm
