#include "rtl8720c_uart_hal.h"

#include <stdlib.h>
#include <string.h>

// Arduino and the Realtek SDK both declare PinMode; isolate the SDK name in this C bridge.
#define PinMode RealtekPinMode
#include <hal_uart.h>
#include <serial_api.h>
#include <serial_ex_api.h>
#undef PinMode

#define RTL8720C_UART_RX_BUFFER_SIZE 256U

struct rtl8720c_uart_hal {
  serial_t serial;
  volatile uint16_t rx_head;
  volatile uint16_t rx_tail;
  uint8_t rx_buffer[RTL8720C_UART_RX_BUFFER_SIZE];
  uint8_t port;
  bool started;
};

static void rtl8720c_uart_irq(uint32_t id, SerialIrq event) {
  rtl8720c_uart_hal_t *hal = (rtl8720c_uart_hal_t *) (uintptr_t) id;
  if (hal == NULL || event != RxIrq) {
    return;
  }

  uint8_t data = (uint8_t) serial_getc(&hal->serial);
  uint16_t next = (hal->rx_head + 1U) % RTL8720C_UART_RX_BUFFER_SIZE;
  if (next == hal->rx_tail) {
    return;
  }

  hal->rx_buffer[hal->rx_head] = data;
  hal->rx_head = next;
}

rtl8720c_uart_hal_t *rtl8720c_uart_hal_create(void) {
  return calloc(1, sizeof(rtl8720c_uart_hal_t));
}

void rtl8720c_uart_hal_destroy(rtl8720c_uart_hal_t *hal) {
  if (hal == NULL) {
    return;
  }
  rtl8720c_uart_hal_stop(hal);
  free(hal);
}

bool rtl8720c_uart_hal_start(rtl8720c_uart_hal_t *hal, uint8_t port, uint8_t tx_pin, uint8_t rx_pin,
                             uint32_t baud_rate, uint8_t data_bits, uint8_t parity, uint8_t stop_bits) {
  if (hal == NULL || hal->started) {
    return hal != NULL && hal->started;
  }

  memset(&hal->serial, 0, sizeof(hal->serial));
  hal->rx_head = 0;
  hal->rx_tail = 0;
  hal->port = port;

  hal_uart_en_ctrl(port, 1);
  serial_init(&hal->serial, (PinName) tx_pin, (PinName) rx_pin);
  if (!hal->serial.uart_adp.is_inited || hal->serial.uart_adp.uart_idx != port) {
    if (hal->serial.uart_adp.is_inited) {
      serial_free(&hal->serial);
    }
    hal_uart_en_ctrl(port, 0);
    return false;
  }

  SerialParity sdk_parity = ParityNone;
  if (parity == RTL8720C_UART_PARITY_EVEN) {
    sdk_parity = ParityEven;
  } else if (parity == RTL8720C_UART_PARITY_ODD) {
    sdk_parity = ParityOdd;
  }

  serial_baud(&hal->serial, (int) baud_rate);
  serial_format(&hal->serial, data_bits, sdk_parity, stop_bits);
  serial_irq_handler(&hal->serial, rtl8720c_uart_irq, (uint32_t) (uintptr_t) hal);
  serial_irq_set(&hal->serial, RxIrq, 1);
  hal->started = true;
  return true;
}

void rtl8720c_uart_hal_stop(rtl8720c_uart_hal_t *hal) {
  if (hal == NULL || !hal->started) {
    return;
  }

  serial_irq_set(&hal->serial, RxIrq, 0);
  serial_irq_handler(&hal->serial, NULL, 0);
  serial_clear_tx(&hal->serial);
  serial_clear_rx(&hal->serial);
  serial_free(&hal->serial);
  hal_uart_en_ctrl(hal->port, 0);
  hal->started = false;
  hal->rx_head = 0;
  hal->rx_tail = 0;
}

size_t rtl8720c_uart_hal_available(const rtl8720c_uart_hal_t *hal) {
  if (hal == NULL || !hal->started) {
    return 0;
  }
  return (hal->rx_head + RTL8720C_UART_RX_BUFFER_SIZE - hal->rx_tail) % RTL8720C_UART_RX_BUFFER_SIZE;
}

bool rtl8720c_uart_hal_peek(const rtl8720c_uart_hal_t *hal, uint8_t *data) {
  if (hal == NULL || data == NULL || !hal->started || hal->rx_head == hal->rx_tail) {
    return false;
  }
  *data = hal->rx_buffer[hal->rx_tail];
  return true;
}

bool rtl8720c_uart_hal_read(rtl8720c_uart_hal_t *hal, uint8_t *data) {
  if (!rtl8720c_uart_hal_peek(hal, data)) {
    return false;
  }
  hal->rx_tail = (hal->rx_tail + 1U) % RTL8720C_UART_RX_BUFFER_SIZE;
  return true;
}

void rtl8720c_uart_hal_write(rtl8720c_uart_hal_t *hal, const uint8_t *data, size_t len) {
  if (hal == NULL || data == NULL || !hal->started) {
    return;
  }
  for (size_t i = 0; i < len; i++) {
    serial_putc(&hal->serial, data[i]);
  }
}
