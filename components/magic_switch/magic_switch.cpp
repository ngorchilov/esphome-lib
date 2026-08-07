#include "magic_switch.h"

#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#include <cinttypes>

#if defined(USE_ESP32_FRAMEWORK_ESP_IDF) && defined(USE_MAGIC_SWITCH_IRAM_SAFE_INTERRUPT)
#include "esp_err.h"
#include "esp_intr_alloc.h"

extern "C" esp_err_t __real_gpio_install_isr_service(int intr_alloc_flags);

extern "C" esp_err_t __wrap_gpio_install_isr_service(int intr_alloc_flags) {
  return __real_gpio_install_isr_service(intr_alloc_flags | ESP_INTR_FLAG_IRAM);
}
#endif

namespace esphome::magic_switch {

static const char *const TAG = "magic_switch";

void MagicSwitch::setup() {
  this->pin_->setup();
  this->isr_pin_ = this->pin_->to_isr();

  const uint32_t now = micros();
  this->last_state_ = this->pin_->digital_read();
  this->rise_valid_ = false;
  this->calibration_until_us_ = now + this->startup_mask_us_;
  this->mask_until_us_ = this->calibration_until_us_;

  this->pin_->attach_interrupt(MagicSwitch::edge_intr, this, gpio::INTERRUPT_ANY_EDGE);
}

void MagicSwitch::loop() {
  const uint32_t now_us = micros();
  bool event_ready = false;
  bool event_adaptive = false;
  bool calibration_finished = false;
  uint32_t event_pulse_us = 0;
  uint32_t event_count = 0;
  uint32_t baseline_pulse_us = 0;
  uint32_t zero_cross_period_us = 0;
  FilterReason filter_reason = FilterReason::NONE;
  uint32_t filtered_pulse_us = 0;
  uint32_t masked_count = 0;
  uint32_t too_long_count = 0;
  uint32_t recovery_timeout_count = 0;
  uint32_t below_threshold_count = 0;

  {
    InterruptLock lock;

    if (this->mask_until_us_ != 0 && static_cast<int32_t>(now_us - this->mask_until_us_) >= 0) {
      this->mask_until_us_ = 0;
    }

    if (this->calibrating_ && static_cast<int32_t>(now_us - this->calibration_until_us_) >= 0) {
      this->calibrating_ = false;
      calibration_finished = true;
    }

    if (this->candidate_pending_ && static_cast<int32_t>(now_us - this->candidate_deadline_us_) >= 0) {
      this->candidate_pending_ = false;
      this->recovery_count_ = 0;
      this->recovery_timeout_count_++;
      this->record_filtered_(FilterReason::RECOVERY_TIMEOUT, this->candidate_pulse_us_);
    }

    if (this->event_ready_) {
      event_ready = true;
      event_adaptive = this->event_adaptive_;
      event_pulse_us = this->event_pulse_us_;
      event_count = this->accepted_count_;
      this->event_ready_ = false;
    }

    baseline_pulse_us = this->baseline_pulse_us_;
    zero_cross_period_us = this->zero_cross_period_us_;
    filter_reason = this->last_filter_reason_;
    filtered_pulse_us = this->last_filtered_pulse_us_;
    masked_count = this->masked_count_;
    too_long_count = this->too_long_count_;
    recovery_timeout_count = this->recovery_timeout_count_;
    below_threshold_count = this->below_threshold_count_;
  }

  if (calibration_finished && !this->calibration_logged_) {
    this->calibration_logged_ = true;
    ESP_LOGD(TAG, "Calibration complete: normal pulse=%" PRIu32 "us, zero-cross period=%" PRIu32 "us",
             baseline_pulse_us, zero_cross_period_us);
  }

  if (event_ready) {
    ESP_LOGI(TAG, "Wall-switch event #%" PRIu32 ": pulse=%" PRIu32 "us (%s threshold)", event_count,
             event_pulse_us, event_adaptive ? "adaptive off-phase" : "fixed");
    this->switch_trigger_.trigger(event_pulse_us);
  }

  const bool filter_counts_changed = masked_count != this->reported_masked_count_ ||
                                     too_long_count != this->reported_too_long_count_ ||
                                     recovery_timeout_count != this->reported_recovery_timeout_count_ ||
                                     below_threshold_count != this->reported_below_threshold_count_;
  const uint32_t now_ms = millis();
  if (filter_counts_changed && now_ms - this->last_filter_log_ms_ >= 1000) {
    const char *reason = "unknown";
    if (filter_reason == FilterReason::MASKED) {
      reason = "startup/relay/debounce mask";
    } else if (filter_reason == FilterReason::TOO_LONG) {
      reason = "above max_pulse";
    } else if (filter_reason == FilterReason::RECOVERY_TIMEOUT) {
      reason = "zero-cross recovery timeout";
    } else if (filter_reason == FilterReason::BELOW_THRESHOLD) {
      reason = "below active threshold";
    }
    ESP_LOGD(TAG,
             "Filtered pulse=%" PRIu32 "us (%s); totals: masked=%" PRIu32 ", too_long=%" PRIu32
             ", recovery_timeout=%" PRIu32 ", below_threshold=%" PRIu32,
             filtered_pulse_us, reason, masked_count, too_long_count, recovery_timeout_count,
             below_threshold_count);
    this->reported_masked_count_ = masked_count;
    this->reported_too_long_count_ = too_long_count;
    this->reported_recovery_timeout_count_ = recovery_timeout_count;
    this->reported_below_threshold_count_ = below_threshold_count;
    this->last_filter_log_ms_ = now_ms;
  }
}

void MagicSwitch::dump_config() {
  ESP_LOGCONFIG(TAG, "Magic Switch:");
  LOG_PIN("  Pin: ", this->pin_);
#ifdef USE_MAGIC_SWITCH_IRAM_SAFE_INTERRUPT
  ESP_LOGCONFIG(TAG, "  IRAM-safe GPIO interrupt service: YES");
#else
  ESP_LOGCONFIG(TAG, "  IRAM-safe GPIO interrupt service: NO");
#endif
  ESP_LOGCONFIG(TAG,
                "  Pulse range: %" PRIu32 "us to %" PRIu32 "us\n"
                "  Adaptive off-phase threshold: at least %" PRIu32 "us, normal + %" PRIu32 "us\n"
                "  Phase tolerance: %" PRIu32 "us\n"
                "  Recovery: %u pulses within %" PRIu32 "ms\n"
                "  Startup mask: %" PRIu32 "ms\n"
                "  Post-event debounce: %" PRIu32 "ms",
                this->min_pulse_us_, this->max_pulse_us_, this->adaptive_min_pulse_us_, this->adaptive_margin_us_,
                this->phase_tolerance_us_, this->recovery_pulses_, this->recovery_timeout_us_ / 1000UL,
                this->startup_mask_us_ / 1000UL, this->debounce_us_ / 1000UL);
}

void MagicSwitch::mask(uint32_t duration_ms) {
  const uint32_t now = micros();
  const uint32_t new_until = now + duration_ms * 1000UL;

  InterruptLock lock;
  if (this->mask_until_us_ != 0 && static_cast<int32_t>(now - this->mask_until_us_) >= 0) {
    this->mask_until_us_ = 0;
  }
  if (this->mask_until_us_ == 0 || static_cast<int32_t>(new_until - this->mask_until_us_) > 0) {
    this->mask_until_us_ = new_until;
  }
  this->candidate_pending_ = false;
  this->recovery_count_ = 0;
  this->event_ready_ = false;
  this->rise_valid_ = false;
}

void IRAM_ATTR HOT MagicSwitch::edge_intr(MagicSwitch *component) {
  const bool state = component->isr_pin_.digital_read();
  if (state == component->last_state_) {
    return;
  }

  component->last_state_ = state;
  const uint32_t now = micros();
  if (state) {
    component->handle_rising_(now);
  } else {
    component->handle_falling_(now);
  }
}

void IRAM_ATTR MagicSwitch::handle_rising_(uint32_t now) {
  this->rise_us_ = now;
  this->rise_valid_ = true;
  this->rise_on_phase_ = this->is_on_phase_(now);
}

void IRAM_ATTR MagicSwitch::handle_falling_(uint32_t now) {
  if (!this->rise_valid_) {
    return;
  }

  this->rise_valid_ = false;
  const uint32_t rise = this->rise_us_;
  const uint32_t width = now - rise;
  const bool on_phase = this->rise_on_phase_;
  const uint32_t adaptive_threshold = this->adaptive_threshold_();
  const uint32_t threshold = (this->calibrating_ || on_phase) ? this->min_pulse_us_ : adaptive_threshold;
  const bool masked = this->is_masked_(now);

  if (width > this->max_pulse_us_) {
    this->candidate_pending_ = false;
    this->recovery_count_ = 0;
    this->last_regular_rise_us_ = 0;
    this->too_long_count_++;
    this->record_filtered_(FilterReason::TOO_LONG, width);
    return;
  }

  const bool candidate = width >= threshold;
  if (candidate) {
    if (masked) {
      this->masked_count_++;
      this->record_filtered_(FilterReason::MASKED, width);
      return;
    }

    if (!this->candidate_pending_) {
      this->candidate_pulse_us_ = width;
      this->event_adaptive_ = !on_phase && threshold < this->min_pulse_us_;
    } else if (width > this->candidate_pulse_us_) {
      this->candidate_pulse_us_ = width;
    }
    this->candidate_pending_ = true;
    this->candidate_deadline_us_ = now + this->recovery_timeout_us_;
    this->recovery_count_ = 0;
    return;
  }

  // Surface unusual short pulses so missed quick wall-switch movements can be tuned from logs.
  if (!this->calibrating_ && !masked && (!on_phase || width >= adaptive_threshold)) {
    this->below_threshold_count_++;
    this->record_filtered_(FilterReason::BELOW_THRESHOLD, width);
  }

  if (this->calibrating_) {
    this->learn_normal_(rise, width, true);
  } else if (on_phase) {
    this->learn_normal_(rise, width, false);
  }

  if (!this->candidate_pending_) {
    return;
  }

  if (!on_phase) {
    this->recovery_count_ = 0;
    return;
  }

  this->recovery_count_++;
  if (this->recovery_count_ < this->recovery_pulses_) {
    return;
  }

  this->candidate_pending_ = false;
  this->recovery_count_ = 0;
  this->event_pulse_us_ = this->candidate_pulse_us_;
  this->event_ready_ = true;
  this->accepted_count_++;
  this->mask_until_us_ = now + this->debounce_us_;
}

bool IRAM_ATTR MagicSwitch::is_on_phase_(uint32_t rise) const {
  if (this->last_regular_rise_us_ == 0) {
    return true;
  }

  const uint32_t period = this->zero_cross_period_us_;
  const uint32_t elapsed = rise - this->last_regular_rise_us_;
  if (elapsed > period * 20UL) {
    return true;
  }

  const uint32_t cycles = (elapsed + period / 2UL) / period;
  if (cycles == 0) {
    return false;
  }

  const uint32_t expected = cycles * period;
  const uint32_t error = elapsed > expected ? elapsed - expected : expected - elapsed;
  return error <= this->phase_tolerance_us_;
}

bool IRAM_ATTR MagicSwitch::is_masked_(uint32_t now) const {
  return this->mask_until_us_ != 0 && static_cast<int32_t>(this->mask_until_us_ - now) > 0;
}

uint32_t IRAM_ATTR MagicSwitch::adaptive_threshold_() const {
  uint32_t threshold = this->baseline_pulse_us_ + this->adaptive_margin_us_;
  if (threshold < this->adaptive_min_pulse_us_) {
    threshold = this->adaptive_min_pulse_us_;
  }
  if (threshold > this->min_pulse_us_) {
    threshold = this->min_pulse_us_;
  }
  return threshold;
}

void IRAM_ATTR MagicSwitch::learn_normal_(uint32_t rise, uint32_t width, bool relaxed_phase) {
  const uint32_t normal_threshold = relaxed_phase ? this->min_pulse_us_ : this->adaptive_threshold_();
  if (width < normal_threshold) {
    const int32_t pulse_delta = static_cast<int32_t>(width) - static_cast<int32_t>(this->baseline_pulse_us_);
    this->baseline_pulse_us_ = static_cast<uint32_t>(static_cast<int32_t>(this->baseline_pulse_us_) + pulse_delta / 8);
  }

  if (this->last_regular_rise_us_ != 0) {
    const uint32_t elapsed = rise - this->last_regular_rise_us_;
    const uint32_t period = this->zero_cross_period_us_;
    uint32_t cycles = (elapsed + period / 2UL) / period;
    if (relaxed_phase && elapsed >= 5000UL && elapsed <= 15000UL) {
      cycles = 1;
    }
    if (cycles > 0 && cycles <= 20) {
      const uint32_t observed = elapsed / cycles;
      if (observed >= 5000UL && observed <= 15000UL) {
        const int32_t period_delta = static_cast<int32_t>(observed) - static_cast<int32_t>(period);
        this->zero_cross_period_us_ =
            static_cast<uint32_t>(static_cast<int32_t>(period) + period_delta / 8);
      }
    }
  }
  this->last_regular_rise_us_ = rise;
}

void IRAM_ATTR MagicSwitch::record_filtered_(FilterReason reason, uint32_t pulse_us) {
  this->last_filter_reason_ = reason;
  this->last_filtered_pulse_us_ = pulse_us;
}

}  // namespace esphome::magic_switch
