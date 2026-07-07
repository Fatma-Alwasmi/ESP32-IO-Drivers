#include "i2c_driver.h"
#include "driver/gpio.h"
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define INDEX(char) (((char) - 'A' + 1) * 8)

// reference: https://github.com/raspberrypi/pico-examples/blob/master/i2c/ssd1306_i2c/ssd1306_font.h 
static uint8_t font[] = {
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Nothing
0x78, 0x14, 0x12, 0x11, 0x12, 0x14, 0x78, 0x00, //A
0x7f, 0x49, 0x49, 0x49, 0x49, 0x49, 0x7f, 0x00, //B
0x7e, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x00, //C
0x7f, 0x41, 0x41, 0x41, 0x41, 0x41, 0x7e, 0x00, //D
0x7f, 0x49, 0x49, 0x49, 0x49, 0x49, 0x49, 0x00, //E
0x7f, 0x09, 0x09, 0x09, 0x09, 0x01, 0x01, 0x00, //F
0x7f, 0x41, 0x41, 0x41, 0x51, 0x51, 0x73, 0x00, //G
0x7f, 0x08, 0x08, 0x08, 0x08, 0x08, 0x7f, 0x00, //H
0x00, 0x00, 0x00, 0x7f, 0x00, 0x00, 0x00, 0x00, //I
0x21, 0x41, 0x41, 0x3f, 0x01, 0x01, 0x01, 0x00, //J
0x00, 0x7f, 0x08, 0x08, 0x14, 0x22, 0x41, 0x00, //K
0x7f, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x00, //L
0x7f, 0x02, 0x04, 0x08, 0x04, 0x02, 0x7f, 0x00, //M
0x7f, 0x02, 0x04, 0x08, 0x10, 0x20, 0x7f, 0x00, //N
0x3e, 0x41, 0x41, 0x41, 0x41, 0x41, 0x3e, 0x00, //O
0x7f, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e, 0x00, //P
0x3e, 0x41, 0x41, 0x49, 0x51, 0x61, 0x7e, 0x00, //Q
0x7f, 0x11, 0x11, 0x11, 0x31, 0x51, 0x0e, 0x00, //R
0x46, 0x49, 0x49, 0x49, 0x49, 0x30, 0x00, 0x00, //S
0x01, 0x01, 0x01, 0x7f, 0x01, 0x01, 0x01, 0x00, //T
0x3f, 0x40, 0x40, 0x40, 0x40, 0x40, 0x3f, 0x00, //U
0x0f, 0x10, 0x20, 0x40, 0x20, 0x10, 0x0f, 0x00, //V
0x7f, 0x20, 0x10, 0x08, 0x10, 0x20, 0x7f, 0x00, //W
0x00, 0x41, 0x22, 0x14, 0x14, 0x22, 0x41, 0x00, //X
0x01, 0x02, 0x04, 0x78, 0x04, 0x02, 0x01, 0x00, //Y
0x41, 0x61, 0x59, 0x45, 0x43, 0x41, 0x00, 0x00, //Z
};


void app_main(void)
{
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
    // TickType_t timeout = pdMS_TO_TICKS(1000);
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

    i2c_driver_config_t i2c_config = {
        .freq_hz       = 400000,
        .sda_pin       = GPIO_NUM_22,
        .scl_pin       = GPIO_NUM_20,
        .sda_pullup_en = true,
        .scl_pullup_en = true,
    };

    vTaskDelay(pdMS_TO_TICKS(200));

    if (i2c_driver_init(i2c_num, &i2c_config) == ESP_OK)
        ESP_LOGI("main", "i2c init done");

    // reference: https://github.com/raspberrypi/pico-examples/blob/master/i2c/ssd1306_i2c/ssd1306_i2c.c
    uint8_t init[] = {
    0x00,        // control byte — everything after is commands
    0xAE,        // display off
    0x20, 0x00,  // horizontal memory mode
    0x40,        // start line 0
    0xA1,        // segment remap
    0xA8, 0x3F,  // multiplex ratio 64-1
    0xC8,        // COM scan direction
    0xD3, 0x00,  // display offset 0
    0xDA, 0x12,  // COM pins config for 128x64
    0xD5, 0x80,  // clock divide ratio
    0xD9, 0xF1,  // precharge period
    0xDB, 0x40,  // VCOMH deselect
    0x81, 0xFF,  // contrast
    0xA4,        // display from RAM
    0xA6,        // normal display
    0x8D, 0x14,  // charge pump on
    0x2E,        // scroll off
    0xAF,        // display on
    };
/*
           | COL0 | COL1 | COL2 | COL3 |  ...  | COL126 | COL127 |
    PAGE 0 |      |      |      |      |       |        |        |
    PAGE 1 |      |      |      |      |       |        |        |
    PAGE 2 |      |      |      |      |       |        |        |
    PAGE 3 |      |      |      |      |       |        |        |
    --------------------------------------------------------------

        within each page:

          | COL0 | COL1 | COL2 | COL3 |  ...  | COL126 | COL127 |
    COM 0 |      |      |      |      |       |        |        |
    COM 1 |      |      |      |      |       |        |        |
       :  |      |      |      |      |       |        |        |
    COM 7 |      |      |      |      |       |        |        |
    -------------------------------------------------------------
*/

    i2c_driver_write(i2c_num, slave_addr, init, sizeof(init));

    // when sending pixel data in horizontal addressing mode, the display has an internal pointer
    // that starts at a position and automatically adcvances, we need to tell it where to start
    // and where to end

    // im doing Horizontal addressing mode, speified by isntruction 0x00, described in the ssd1306 datasheet section 10.1.3 & 10.1.4
    // so the command below states: do horizontal addressing (0x00), set the column address (0x21), start at col 0x00 and end at
    // column 0x7F (col0 - col127)
    uint8_t col_cmd[] = {0x00, 0x21, 0x00, 0x7F};
    i2c_driver_write(i2c_num, slave_addr, col_cmd, sizeof(col_cmd));

    // this command specifies horizontal addressing as well, page addressing, start at page 0x00 and end at 0x07
    uint8_t page_cmd[] = {0x00, 0x22, 0x00, 0x07};
    i2c_driver_write(i2c_num, slave_addr, page_cmd, sizeof(page_cmd));

    // first clear display, the display is 128 pixels wide and 64 pixels tall,
    // 128x64 = 8192 total pixels, each byte stors 8 pixels, 8192/ 8 = 1024 bytes

    uint8_t clear[32] = {0x40}; // 0x40 = data mode control byte, rest is 0x00
    
    int remaining = 1024;
    while(remaining > 0){
        int to_send = remaining > 31 ? 31 : remaining; // clearing 31 pixels at a time
        i2c_driver_write(i2c_num, slave_addr, clear, to_send + 1);
        remaining -= to_send;
    }

    // set the column and page address of where the character should apprear:
    uint8_t col_char[] = {0x00, 0x21, 0x00, 0x7F};
    i2c_driver_write(i2c_num, slave_addr, col_char, sizeof(col_char));

    uint8_t page_char[] = {0x00, 0x22, 0x00, 0x00};
    i2c_driver_write(i2c_num, slave_addr, page_char, sizeof(page_char));


    const char *char_to_send = "HELLOWORLD";
    uint8_t char_buf[9];
    for(int i = 0; i < strlen(char_to_send); i++){
        char_buf[0] = 0x40; // data mode control byte
        memcpy(char_buf + 1, &font[INDEX(char_to_send[i])], 8);
        i2c_driver_write(i2c_num, slave_addr, char_buf, 9);
    }
}
