idf_component_register(SRCS "i2c_driver.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_hw_support esp_driver_gpio freertos esp_rom soc hal esp_hal_i2c esp_ringbuf)