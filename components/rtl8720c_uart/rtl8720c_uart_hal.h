#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct rtl8720c_uart_hal rtl8720c_uart_hal_t;

enum {
  RTL8720C_UART_PARITY_NONE = 0,
  RTL8720C_UART_PARITY_EVEN = 1,
  RTL8720C_UART_PARITY_ODD = 2,
};

rtl8720c_uart_hal_t *rtl8720c_uart_hal_create(void);
void rtl8720c_uart_hal_destroy(rtl8720c_uart_hal_t *hal);

bool rtl8720c_uart_hal_start(rtl8720c_uart_hal_t *hal, uint8_t port, uint8_t tx_pin, uint8_t rx_pin,
                             uint32_t baud_rate, uint8_t data_bits, uint8_t parity, uint8_t stop_bits);
void rtl8720c_uart_hal_stop(rtl8720c_uart_hal_t *hal);

size_t rtl8720c_uart_hal_available(const rtl8720c_uart_hal_t *hal);
bool rtl8720c_uart_hal_peek(const rtl8720c_uart_hal_t *hal, uint8_t *data);
bool rtl8720c_uart_hal_read(rtl8720c_uart_hal_t *hal, uint8_t *data);
void rtl8720c_uart_hal_write(rtl8720c_uart_hal_t *hal, const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif
