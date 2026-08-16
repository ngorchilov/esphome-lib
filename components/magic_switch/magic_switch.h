#pragma once

#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/hal.h"

#include <cstdint>

namespace esphome::magic_switch {

class MagicSwitch final : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void set_pin(InternalGPIOPin *pin) { this->pin_ = pin; }
  void set_min_pulse_us(uint32_t value) { this->min_pulse_us_ = value; }
  void set_max_pulse_us(uint32_t value) { this->max_pulse_us_ = value; }
#ifdef USE_MAGIC_SWITCH_MISSING_PULSE_DETECTION
  void set_detect_missing_pulses(bool value) { this->detect_missing_pulses_ = value; }
#endif
  void set_adaptive_min_pulse_us(uint32_t value) { this->adaptive_min_pulse_us_ = value; }
  void set_adaptive_margin_us(uint32_t value) { this->adaptive_margin_us_ = value; }
  void set_phase_tolerance_us(uint32_t value) { this->phase_tolerance_us_ = value; }
  void set_recovery_pulses(uint8_t value) { this->recovery_pulses_ = value; }
  void set_recovery_timeout_ms(uint32_t value) { this->recovery_timeout_us_ = value * 1000UL; }
  void set_startup_mask_ms(uint32_t value) { this->startup_mask_us_ = value * 1000UL; }
  void set_debounce_ms(uint32_t value) { this->debounce_us_ = value * 1000UL; }

  Trigger<uint32_t> *get_switch_trigger() { return &this->switch_trigger_; }

  /// Suppress candidates for at least duration_ms from now.
  void mask(uint32_t duration_ms);

 protected:
  enum class FilterReason : uint8_t {
    NONE = 0,
    MASKED,
    TOO_LONG,
    RECOVERY_TIMEOUT,
    BELOW_THRESHOLD,
  };

  static void edge_intr(MagicSwitch *component);
  void handle_rising_(uint32_t now);
  void handle_falling_(uint32_t now);
  bool is_on_phase_(uint32_t rise) const;
  bool is_masked_(uint32_t now) const;
  uint32_t adaptive_threshold_() const;
  void learn_normal_(uint32_t rise, uint32_t width, bool relaxed_phase);
  void record_filtered_(FilterReason reason, uint32_t pulse_us);

  InternalGPIOPin *pin_{nullptr};
  ISRInternalGPIOPin isr_pin_;
  Trigger<uint32_t> switch_trigger_;

  uint32_t min_pulse_us_{1000};
  uint32_t max_pulse_us_{120000};
  uint32_t adaptive_min_pulse_us_{750};
  uint32_t adaptive_margin_us_{200};
  uint32_t phase_tolerance_us_{1000};
  uint32_t recovery_timeout_us_{150000};
  uint32_t startup_mask_us_{2000000};
  uint32_t debounce_us_{250000};
  uint8_t recovery_pulses_{2};
#ifdef USE_MAGIC_SWITCH_MISSING_PULSE_DETECTION
  bool detect_missing_pulses_{false};
#endif

  // Access these ISR fields only from the ISR or while holding InterruptLock.
  volatile bool last_state_{false};
  volatile bool rise_valid_{false};
  volatile bool rise_on_phase_{false};
  volatile bool calibrating_{true};
  volatile uint32_t rise_us_{0};
  volatile uint32_t last_regular_rise_us_{0};
  volatile uint32_t zero_cross_period_us_{10000};
  volatile uint32_t baseline_pulse_us_{650};
  volatile uint32_t mask_until_us_{0};
  volatile uint32_t calibration_until_us_{0};

  volatile bool candidate_pending_{false};
  volatile uint32_t candidate_pulse_us_{0};
  volatile uint32_t candidate_deadline_us_{0};
  volatile uint8_t recovery_count_{0};
  volatile bool event_ready_{false};
  volatile bool event_adaptive_{false};
  volatile uint32_t event_pulse_us_{0};
  volatile uint32_t accepted_count_{0};
#ifdef USE_MAGIC_SWITCH_MISSING_PULSE_DETECTION
  volatile uint8_t candidate_missing_pulses_{0};
  volatile uint8_t event_missing_pulses_{0};
#endif

  volatile FilterReason last_filter_reason_{FilterReason::NONE};
  volatile uint32_t last_filtered_pulse_us_{0};
  volatile uint32_t masked_count_{0};
  volatile uint32_t too_long_count_{0};
  volatile uint32_t recovery_timeout_count_{0};
  volatile uint32_t below_threshold_count_{0};

  bool calibration_logged_{false};
  uint32_t last_filter_log_ms_{0};
  uint32_t reported_masked_count_{0};
  uint32_t reported_too_long_count_{0};
  uint32_t reported_recovery_timeout_count_{0};
  uint32_t reported_below_threshold_count_{0};
};

template<typename... Ts> class MagicSwitchMaskAction final : public Action<Ts...> {
 public:
  explicit MagicSwitchMaskAction(MagicSwitch *parent) : parent_(parent) {}

  void set_duration(uint32_t duration_ms) { this->duration_ms_ = duration_ms; }

  void play(const Ts &...x) override { this->parent_->mask(this->duration_ms_); }

 protected:
  MagicSwitch *parent_;
  uint32_t duration_ms_{500};
};

}  // namespace esphome::magic_switch
