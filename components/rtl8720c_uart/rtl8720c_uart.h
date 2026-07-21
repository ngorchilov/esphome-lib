#pragma once

#ifdef USE_LIBRETINY

#include "esphome/components/uart/uart_component.h"
#include "esphome/core/component.h"
#include "rtl8720c_uart_hal.h"

namespace esphome::rtl8720c_uart {

class RTL8720CUARTComponent final : public uart::UARTComponent, public Component {
 public:
  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::BUS; }
  void on_shutdown() override;

  void write_array(const uint8_t *data, size_t len) override;
  bool peek_byte(uint8_t *data) override;
  bool read_array(uint8_t *data, size_t len) override;
  size_t available() override;
  uart::UARTFlushResult flush() override;

 protected:
  void check_logger_conflict() override {}
  void start_();
  void stop_();

  rtl8720c_uart_hal_t *hal_{nullptr};
  bool started_{false};
};

}  // namespace esphome::rtl8720c_uart

#endif  // USE_LIBRETINY
