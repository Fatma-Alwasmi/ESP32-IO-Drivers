#include "uart_driver.h"
#include <string.h>
#include <sys/param.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

void app_main(void){

    uart_port_t uart_num = UART_NUM_1;

    uart_driver_config_t uart_conf = {
        .baud_rate = 115200,
        .word_len = UART_DATA_8_BITS,
        .stop_bits = UART_STOP_BITS_1,
        .parity = UART_PARITY_DISABLE,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .tx_pin = GPIO_NUM_25,
        .rx_pin = GPIO_NUM_26,
        .rx_thrs =  0,
    };

    char *buf = "Hello World";
    uint8_t recv_buf[128];
    size_t recv_len = 128;
    TickType_t timeout = pdMS_TO_TICKS(1000);
    size_t len = strlen(buf);

    if(uart_driver_init(uart_num, &uart_conf, 1024) == ESP_OK){
        ESP_LOGI("main", "init DONE");
    }
    ESP_LOGI("main", "interrupt dump:");
    esp_intr_dump(stdout);
    if(uart_driver_transmit(uart_num, (uint8_t *)buf, len) == ESP_OK){
        ESP_LOGI("main", "transmit DONE");
    }

    int recv = uart_driver_receive(uart_num, (uint8_t *)recv_buf, recv_len, timeout);
    ESP_LOGI("main", "receive done, got %d bytes", recv);
    if(recv > 0 && memcmp(recv_buf, buf, len) == 0){
        ESP_LOGI("main", "received %d bytes", recv);
    }
    else{
    ESP_LOGE("main", "loopback FAILED, recv=%d", recv);
    ESP_LOG_BUFFER_HEXDUMP("main", recv_buf, recv, ESP_LOG_INFO);

    }
}