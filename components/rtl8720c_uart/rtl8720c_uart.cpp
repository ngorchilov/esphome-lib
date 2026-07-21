#ifdef USE_LIBRETINY

#include "rtl8720c_uart.h"

#include "esphome/core/log.h"

namespace esphome::rtl8720c_uart {

static const char *const TAG = "rtl8720c_uart";

void RTL8720CUARTComponent::setup() { this->start_(); }

void RTL8720CUARTComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "RTL8720C UART0:");
  LOG_PIN("  TX Pin: ", this->tx_pin_);
  LOG_PIN("  RX Pin: ", this->rx_pin_);
  ESP_LOGCONFIG(TAG,
                "  Baud Rate: %u baud\n"
                "  Data Bits: %u\n"
                "  Parity: %s\n"
                "  Stop Bits: %u\n"
                "  Started: %s",
                this->baud_rate_, this->data_bits_, LOG_STR_ARG(uart::parity_to_str(this->parity_)), this->stop_bits_,
                YESNO(this->started_));
}

void RTL8720CUARTComponent::on_shutdown() { this->stop_(); }

void RTL8720CUARTComponent::start_() {
  const uint8_t rx_pin = this->rx_pin_->get_pin();
  const uint8_t tx_pin = this->tx_pin_->get_pin();

  if (this->hal_ == nullptr) {
    this->hal_ = rtl8720c_uart_hal_create();
  }
  if (this->hal_ == nullptr) {
    ESP_LOGE(TAG, "Could not allocate UART0 state");
    this->status_set_error();
    return;
  }

  uint8_t parity = RTL8720C_UART_PARITY_NONE;
  if (this->parity_ == uart::UART_CONFIG_PARITY_EVEN) {
    parity = RTL8720C_UART_PARITY_EVEN;
  } else if (this->parity_ == uart::UART_CONFIG_PARITY_ODD) {
    parity = RTL8720C_UART_PARITY_ODD;
  }

  this->started_ = rtl8720c_uart_hal_start(this->hal_, 0, tx_pin, rx_pin, this->baud_rate_, this->data_bits_, parity,
                                           this->stop_bits_);
  if (!this->started_) {
    ESP_LOGE(TAG, "UART0 failed to start");
    this->status_set_error();
    return;
  }

  this->status_clear_error();
  ESP_LOGD(TAG, "UART0 started with TX=%u, RX=%u", tx_pin, rx_pin);
}

void RTL8720CUARTComponent::stop_() {
  if (!this->started_) {
    return;
  }

  rtl8720c_uart_hal_stop(this->hal_);
  this->started_ = false;
}

void RTL8720CUARTComponent::write_array(const uint8_t *data, size_t len) {
  if (!this->started_) {
    return;
  }

  rtl8720c_uart_hal_write(this->hal_, data, len);
#ifdef USE_UART_DEBUGGER
  for (size_t i = 0; i < len; i++) {
    this->debug_callback_.call(uart::UART_DIRECTION_TX, data[i]);
  }
#endif
}

bool RTL8720CUARTComponent::peek_byte(uint8_t *data) {
  if (!this->started_ || !this->check_read_timeout_()) {
    return false;
  }

  return rtl8720c_uart_hal_peek(this->hal_, data);
}

bool RTL8720CUARTComponent::read_array(uint8_t *data, size_t len) {
  if (!this->started_ || !this->check_read_timeout_(len)) {
    return false;
  }

  for (size_t i = 0; i < len; i++) {
    if (!rtl8720c_uart_hal_read(this->hal_, &data[i])) {
      return false;
    }
  }
#ifdef USE_UART_DEBUGGER
  for (size_t i = 0; i < len; i++) {
    this->debug_callback_.call(uart::UART_DIRECTION_RX, data[i]);
  }
#endif
  return true;
}

size_t RTL8720CUARTComponent::available() {
  if (!this->started_) {
    return 0;
  }
  return rtl8720c_uart_hal_available(this->hal_);
}

uart::UARTFlushResult RTL8720CUARTComponent::flush() {
  return uart::UARTFlushResult::UART_FLUSH_RESULT_ASSUMED_SUCCESS;
}

}  // namespace esphome::rtl8720c_uart

#endif  // USE_LIBRETINY
