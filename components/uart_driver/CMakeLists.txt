idf_component_register(SRCS "uart_driver.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_hw_support esp_driver_gpio freertos esp_rom soc hal esp_ringbuf esp_hal_uart)