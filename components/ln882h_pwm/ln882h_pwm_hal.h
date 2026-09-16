#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint32_t gpio_base;
  uint32_t timer_base;
  uint16_t pin_mask;
  uint16_t period;
} ln882h_pwm_state_t;

bool ln882h_pwm_init(ln882h_pwm_state_t *state, uint8_t pin, uint8_t timer, float frequency, bool off_high);
void ln882h_pwm_write(const ln882h_pwm_state_t *state, float duty);

#ifdef __cplusplus
}
#endif
