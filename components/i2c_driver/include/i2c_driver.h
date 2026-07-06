
#pragma once

#include <stdbool.h>
#include "hal/i2c_types.h"
#include "esp_err.h"
#include "hal/gpio_types.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif
typedef struct{

    uint32_t freq_hz;
    gpio_num_t sda_pin;
    gpio_num_t scl_pin;
    bool sda_pullup_en;
    bool scl_pullup_en;
    
} i2c_driver_config_t;

esp_err_t i2c_driver_init(i2c_port_t i2c_num, i2c_driver_config_t *config_i2c);

esp_err_t i2c_driver_delete(i2c_port_t i2c_num);

int i2c_driver_read(i2c_port_t i2c_num, uint8_t slave_addr, uint8_t *buf, size_t len, TickType_t timeout);

esp_err_t i2c_driver_write(i2c_port_t i2c_num, uint8_t slave_addr, uint8_t *buf, size_t len);

#ifdef __cplusplus
}
#endif