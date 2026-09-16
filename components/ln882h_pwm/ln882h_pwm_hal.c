#include "ln882h_pwm_hal.h"
#include "hal/hal_adv_timer.h"
#include "hal/hal_clock.h"
#include "hal/hal_gpio.h"
#include <math.h>

static void ln882h_pwm_static(const ln882h_pwm_state_t *state, bool high) {
  gpio_pin_t mask = (gpio_pin_t) state->pin_mask;
  if (high)
    hal_gpio_pin_set(state->gpio_base, mask);
  else
    hal_gpio_pin_reset(state->gpio_base, mask);
  hal_gpio_pin_direction_set(state->gpio_base, mask, GPIO_OUTPUT);
  hal_gpio_pin_afio_en(state->gpio_base, mask, HAL_DISABLE);
}

bool ln882h_pwm_init(ln882h_pwm_state_t *state, uint8_t pin, uint8_t timer, float frequency, bool off_high) {
  static const uint32_t timer_bases[] = {
      ADV_TIMER_0_BASE, ADV_TIMER_1_BASE, ADV_TIMER_2_BASE,
      ADV_TIMER_3_BASE, ADV_TIMER_4_BASE, ADV_TIMER_5_BASE,
  };
  // LibreTiny encodes PA00..PA12 as 0..12 and PB03..PB09 as 19..25.
  if (timer > 5 || !(pin <= 12 || (pin >= 19 && pin <= 25)) ||
      !isfinite(frequency) || frequency < 100.0f || frequency > 20000.0f)
    return false;

  float clock = (float) hal_clock_get_apb0_clk();
  // Use the smallest prescaler that fits, retaining precision for low-brightness fades.
  uint32_t divider = (uint32_t) ceilf(clock / (frequency * 65535.0f));
  if (divider < 1)
    divider = 1;
  uint32_t period = (uint32_t) lroundf(clock / (divider * frequency));
  if (divider > 64 || period < 2 || period > 65535)
    return false;

  state->gpio_base = pin < 16 ? GPIOA_BASE : GPIOB_BASE;
  state->pin_mask = (uint16_t) (1U << (pin % 16));
  state->timer_base = timer_bases[timer];
  state->period = (uint16_t) period;

  gpio_pin_t mask = (gpio_pin_t) state->pin_mask;
  ln882h_pwm_static(state, off_high);
  hal_gpio_pin_mode_set(state->gpio_base, mask, GPIO_MODE_DIGITAL);
  hal_gpio_pin_pull_set(state->gpio_base, mask, GPIO_PULL_NONE);

  adv_tim_init_t_def config = {0};
  config.adv_tim_clk_div = divider - 1;
  config.adv_tim_load_value = period - 2;
  config.adv_tim_cnt_mode = ADV_TIMER_CNT_MODE_INC;
  // The SDK uses this timer polarity for positive-duty, active-high PWM.
  config.adv_tim_cha_inv_en = ADV_TIMER_CHA_INV_EN;
  hal_adv_tim_init(state->timer_base, &config);
  // Reserve one whole timer per output; use its A channel only.
  hal_gpio_pin_afio_select(state->gpio_base, mask, (afio_function_t) (ADV_TIMER_PWM0 + timer * 2));
  hal_adv_tim_a_en(state->timer_base, HAL_ENABLE);
  return true;
}

void ln882h_pwm_write(const ln882h_pwm_state_t *state, float duty) {
  if (!isfinite(duty))
    return;
  uint32_t compare = duty <= 0.0f ? 0 : duty >= 1.0f ? state->period : (uint32_t) lroundf(state->period * duty);
  // The SDK's PWM sample uses GPIO at the endpoints to avoid residual pulses.
  if (compare == 0 || compare == state->period) {
    ln882h_pwm_static(state, compare != 0);
    return;
  }
  hal_adv_tim_set_comp_a(state->timer_base, (uint16_t) compare);
  gpio_pin_t mask = (gpio_pin_t) state->pin_mask;
  hal_gpio_pin_direction_set(state->gpio_base, mask, GPIO_INPUT);
  hal_gpio_pin_afio_en(state->gpio_base, mask, HAL_ENABLE);
}
