#include "uart_driver.h"
#include "i2c_driver.h"
#include "driver/gpio.h"
#include <string.h>
#include <sys/param.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

void app_main(void){

    /*---------------------test uart-------------------*/
    // uart_port_t uart_num = UART_NUM_1;

    // uart_driver_config_t uart_conf = {
    //     .baud_rate = 115200,
    //     .word_len = UART_DATA_8_BITS,
    //     .stop_bits = UART_STOP_BITS_1,
    //     .parity = UART_PARITY_DISABLE,
    //     .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    //     .tx_pin = GPIO_NUM_25,
    //     .rx_pin = GPIO_NUM_26,
    //     .rx_thrs =  0,
    // };

    //char *buf = "Hello World";
    //uint8_t recv_buf[128];
    //size_t recv_len = 128;
    TickType_t timeout = pdMS_TO_TICKS(1000);
    //size_t len = strlen(buf);

    // if(uart_driver_init(uart_num, &uart_conf, 1024) == ESP_OK){
    //     ESP_LOGI("main", "init DONE");
    // }
    // ESP_LOGI("main", "interrupt dump:");
    // esp_intr_dump(stdout);
    // if(uart_driver_transmit(uart_num, (uint8_t *)buf, len) == ESP_OK){
    //     ESP_LOGI("main", "transmit DONE");
    // }

    // int recv = uart_driver_receive(uart_num, (uint8_t *)recv_buf, recv_len, timeout);
    // ESP_LOGI("main", "receive done, got %d bytes", recv);
    // if(recv > 0 && memcmp(recv_buf, buf, len) == 0){
    //     ESP_LOGI("main", "received %d bytes", recv);
    // }
    // else{
    // ESP_LOGE("main", "loopback FAILED, recv=%d", recv);
    // ESP_LOG_BUFFER_HEXDUMP("main", recv_buf, recv, ESP_LOG_INFO);

    //}
    /*-----------------------test i2c-----------------------------*/

    i2c_port_t i2c_num = I2C_NUM_0;
    uint8_t slave_addr = 0x3C;
    char *i2c_buf = "Hello World";

    uint8_t i2c_recv_buf[32]; 


    i2c_driver_config_t i2c_config = {
        .freq_hz = 10000, 
        .sda_pin = GPIO_NUM_22,
        .scl_pin = GPIO_NUM_20,
        .sda_pullup_en = true,
        .scl_pullup_en = true,
    };

    if(i2c_driver_init(i2c_num, &i2c_config) == ESP_OK){
        ESP_LOGI("main", "i2c init done");
    }
    else{
        ESP_LOGE("main", "i2c init failed");
    }

    ESP_LOGI("main", "SDA=%d SCL=%d", gpio_get_level(GPIO_NUM_22), gpio_get_level(GPIO_NUM_20));

    esp_err_t ret = i2c_driver_write(i2c_num, slave_addr, (uint8_t *)i2c_buf, strlen(i2c_buf));
    ESP_LOGI("main", "After write: SDA=%d SCL=%d", gpio_get_level(GPIO_NUM_22), gpio_get_level(GPIO_NUM_20));
    if( ret == ESP_OK){
        ESP_LOGI("main", "i2c write done");
    }
    else{
        ESP_LOGE("main", "i2c write failed on %d", ret);
    }

    ret = i2c_driver_read(i2c_num, slave_addr, (uint8_t *)i2c_recv_buf, 32, timeout);
    if( ret != -1){
        ESP_LOGI("main", "i2c read done");
    }
    else{
        ESP_LOGE("main", "i2c read failed on %d", ret);
    }

}