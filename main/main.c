#include "i2c_driver.h"
#include "driver/gpio.h"
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Each entry: ASCII code + 5 column bytes (bit0 = top pixel).
// static const struct { char c; uint8_t px[5]; } font[] = {
//     {' ', {0x00,0x00,0x00,0x00,0x00}},
//     {'!', {0x00,0x00,0x5F,0x00,0x00}},
//     {'H', {0x7F,0x08,0x08,0x08,0x7F}},
//     {'W', {0x3F,0x40,0x38,0x40,0x3F}},
//     {'d', {0x38,0x44,0x44,0x48,0x7F}},
//     {'e', {0x38,0x54,0x54,0x54,0x18}},
//     {'l', {0x00,0x41,0x7F,0x40,0x00}},
//     {'o', {0x38,0x44,0x44,0x44,0x38}},
//     {'r', {0x7C,0x08,0x04,0x04,0x08}},
// };

// static const uint8_t *find_glyph(char c)
// {
//     for (size_t i = 0; i < sizeof(font) / sizeof(font[0]); i++)
//         if (font[i].c == c) return font[i].px;
//     return font[0].px;  // fall back to space
// }

// // Send up to 30 SSD1306 command bytes (control byte 0x00).
// static void ssd1306_cmd(i2c_port_t num, uint8_t addr, const uint8_t *cmds, size_t n)
// {
//     uint8_t buf[31];
//     buf[0] = 0x00;
//     memcpy(buf + 1, cmds, n);
//     i2c_driver_write(num, addr, buf, n + 1);
// }

// // Send up to 30 pixel bytes to GDDRAM (control byte 0x40).
// static void ssd1306_data(i2c_port_t num, uint8_t addr, const uint8_t *data, size_t n)
// {
//     uint8_t buf[31];
//     buf[0] = 0x40;
//     memcpy(buf + 1, data, n);
//     i2c_driver_write(num, addr, buf, n + 1);
// }

// // Fill all 128×64 GDDRAM bytes with value (0x00 = clear, 0xFF = all on).
// static void ssd1306_clear(i2c_port_t num, uint8_t addr)
// {
//     uint8_t cmds[] = {0x21, 0, 127, 0x22, 0, 7};
//     ssd1306_cmd(num, addr, cmds, sizeof(cmds));
//     uint8_t zeros[31] = {0x40};  // ctrl byte + 30 zero bytes
//     for (int i = 0; i < 1024; i += 30) {
//         size_t chunk = ((1024 - i) > 30) ? 30 : (size_t)(1024 - i);
//         i2c_driver_write(num, addr, zeros, chunk + 1);
//     }
// }

// // Write a string to page row (0–7). Font is 5px + 1px gap = 21 chars/row.
// static void ssd1306_puts(i2c_port_t num, uint8_t addr, const char *str, uint8_t page)
// {
//     uint8_t line[128] = {0};
//     for (uint8_t col = 0; *str && col + 5 <= 128; col += 6, str++)
//         memcpy(&line[col], find_glyph(*str), 5);
//     uint8_t cmds[] = {0x21, 0, 127, 0x22, page, page};
//     ssd1306_cmd(num, addr, cmds, sizeof(cmds));
//     for (int i = 0; i < 128; i += 30)
//         ssd1306_data(num, addr, &line[i], ((128 - i) > 30) ? 30 : (size_t)(128 - i));
// }


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


    i2c_driver_write(i2c_num, slave_addr, init, sizeof(init));

    // set column address 0 to 127
    uint8_t col_cmd[] = {0x00, 0x21, 0x00, 0x7F};
    i2c_driver_write(i2c_num, slave_addr, col_cmd, sizeof(col_cmd));

    // set page address 0 to 7
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


}
