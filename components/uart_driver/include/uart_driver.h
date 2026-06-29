/*
UART Driver Header File for ESP-IDF
*/

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "hal/uart_types.h"
#include "freertos/FreeRTOS.h"
#include "hal/gpio_types.h"

#ifdef __cplusplus
extern "C" {
#endif


typedef struct {

uint32_t baud_rate;
uart_word_length_t word_len;
uart_stop_bits_t stop_bits;
uart_parity_t parity;
uart_hw_flowcontrol_t flow_ctrl;
gpio_num_t tx_pin;
gpio_num_t rx_pin;
uint32_t rx_thrs;
} uart_driver_config_t;



esp_err_t uart_driver_init(uart_port_t uart_num, uart_driver_config_t *config_uart, size_t buf_len);

esp_err_t uart_driver_transmit(uart_port_t uart_num, uint8_t *buf, size_t len);

int uart_driver_receive(uart_port_t uart_num, uint8_t *buf, size_t len, TickType_t timeout);

esp_err_t uart_driver_dlt(uart_port_t uart_num);
#ifdef __cplusplus
}
#endif

