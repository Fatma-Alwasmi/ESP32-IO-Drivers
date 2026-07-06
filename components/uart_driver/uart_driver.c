#include <string.h>
#include <sys/param.h>
#include <sys/lock.h>
#include "sdkconfig.h"
#include "esp_types.h"
#include "esp_attr.h"
#include "esp_intr_alloc.h"
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/ringbuf.h"
#include "freertos/idf_additions.h"
#include "esp_private/critical_section.h"
#include "hal/uart_ll.h"
#include "hal/uart_periph.h"
#include "hal/gpio_types.h"
#include "soc/soc_caps.h"
#include "driver/gpio.h"
#include "esp_private/esp_clk_tree_common.h"
#include "esp_private/gpio.h"
#include "esp_private/esp_gpio_reserve.h"
#include "esp_private/periph_ctrl.h"
#include "esp_sleep.h"
#include "esp_private/sleep_retention.h"
#include "esp_clk_tree.h"
#include "esp_rom_gpio.h"
#include "esp_check.h"
#include "uart_driver.h"
#include "esp_rom_sys.h"


/* this macro does a bitwise OR with MALLOC_CAP_INTERNAL and MALLOC_CAP_8BIT, 
its to tell the mem allocation function to allocate memory that is both internal and 8-bit accessable.
MALLOC_CAP_8BIT means the memory can be accessed one byte at a time, some memeory regions on esp32 like
some IRAM can oly be accessed in 32-bit words and MALLOC_CAP_INTERNAL is so the allocation function
allocates memory in DRAM
*/
#define UART_MALLOC_CAPS  (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)
#define UART_FULL_THRESH_DEFAULT (120)
static const char *UART_TAG = "uart_driver";

typedef struct {

    uart_port_t port_num;  // uart port number to specify which port e.g: 0,1,2
    RingbufHandle_t rx_ring_buf; // recieve ring buffer, written from fifo hardware to software ring buffer
    SemaphoreHandle_t rx_mux; // mutex for accessing recieve buffer to avoid race conditions
    SemaphoreHandle_t tx_mux; // mutex for accessing Tx fifo to avoid race conditions
    intr_handle_t intr_handle; // handle to register interrupt, used to free on deinit
    SemaphoreHandle_t rx_ready_sem; // binary semaphore signaled by isr when data has been written to rx_ring_buf, wakes blocked recv function
    SemaphoreHandle_t tx_done_sem;
    gpio_num_t tx_pin; // transfer gpio pin
    gpio_num_t rx_pin; // rcv gpio pin
} uart_driver_obj_t;

/* allocate memory for uart_driver_obj_t and nullify all feilds */
static uart_driver_obj_t *p_uart_driver_obj[UART_NUM_MAX] = {0};

/* Function to free the memory of the uart obj */
static void uart_free_driver_obj(uart_driver_obj_t *uart_obj){

    if(uart_obj->rx_ring_buf){
        vRingbufferDeleteWithCaps(uart_obj->rx_ring_buf);
    }
    if(uart_obj->rx_mux){
        vSemaphoreDeleteWithCaps(uart_obj->rx_mux);
    }
    if(uart_obj->tx_mux){
        vSemaphoreDeleteWithCaps(uart_obj->tx_mux);
    }
    if(uart_obj->rx_ready_sem){
        vSemaphoreDeleteWithCaps(uart_obj->rx_ready_sem);
    }    
    if(uart_obj->tx_done_sem){
        vSemaphoreDeleteWithCaps(uart_obj->tx_done_sem);
    }  
    heap_caps_free(uart_obj);
}

/* Function to delete uart driver obj */
esp_err_t uart_driver_dlt(uart_port_t uart_num){

    ESP_RETURN_ON_FALSE((uart_num < UART_NUM_MAX), ESP_FAIL, UART_TAG, "uart_num error"); 
    // if p_uart_driver_obj[uart_num] is NULL, it means its already been freed and nullified, so no need to do anything
    if(!p_uart_driver_obj[uart_num]){
        ESP_LOGI(UART_TAG, "ALREADY NULL");
        return ESP_OK;
    }

    // resets GPIO pin to default state
    gpio_reset_pin(p_uart_driver_obj[uart_num]->tx_pin);
    gpio_reset_pin(p_uart_driver_obj[uart_num]->rx_pin);

    
    esp_intr_free(p_uart_driver_obj[uart_num]->intr_handle);
    uart_free_driver_obj(p_uart_driver_obj[uart_num]);
    p_uart_driver_obj[uart_num] = NULL;
    return ESP_OK;
    
    
}

/* Function that runs during isr and writes data from hardware fifo to rx_ring_buf */
static void IRAM_ATTR uart_driver_isr(void *arg){

    uint8_t buf[UART_LL_FIFO_DEF_LEN];

    uart_driver_obj_t *uart_obj = (uart_driver_obj_t *)arg;

    uart_dev_t *hw = UART_LL_GET_HW(uart_obj->port_num);
    BaseType_t HPTaskAwoken = 0;

    uint32_t uart_status = uart_ll_get_intraw_mask(hw);


    if(uart_status & UART_INTR_TXFIFO_EMPTY){
        uart_ll_disable_intr_mask(hw, UART_INTR_TXFIFO_EMPTY);
        xSemaphoreGiveFromISR(uart_obj->tx_done_sem, &HPTaskAwoken);
    }
    if(uart_status & UART_INTR_RXFIFO_FULL || uart_status & UART_INTR_RXFIFO_TOUT){
        uint32_t len = uart_ll_get_rxfifo_len(hw);
        uart_ll_read_rxfifo(hw, buf, len);
        xRingbufferSendFromISR(uart_obj->rx_ring_buf, buf, len, &HPTaskAwoken);
        xSemaphoreGiveFromISR(uart_obj->rx_ready_sem, &HPTaskAwoken);
    }
    else if(uart_status & UART_INTR_RXFIFO_OVF){
        uart_ll_rxfifo_rst(hw);
        ESP_EARLY_LOGE(UART_TAG, "rx fifo overflow %d", uart_obj->port_num);
    }

    
    uart_ll_clr_intsts_mask(hw, uart_status);
    portYIELD_FROM_ISR(HPTaskAwoken);
}

esp_err_t uart_driver_init(uart_port_t uart_num, uart_driver_config_t *config_uart, size_t buf_len){

    esp_err_t ret;

    ESP_RETURN_ON_FALSE((uart_num < UART_NUM_MAX), ESP_FAIL, UART_TAG, "uart_num error"); 
    ESP_RETURN_ON_FALSE((config_uart != NULL), ESP_FAIL, UART_TAG, "config_uart is NULL");
    ESP_RETURN_ON_FALSE((p_uart_driver_obj[uart_num] == NULL), ESP_FAIL, UART_TAG, "uart driver already installed");
    

    p_uart_driver_obj[uart_num] = heap_caps_calloc(1, sizeof(uart_driver_obj_t), UART_MALLOC_CAPS);
    if(!p_uart_driver_obj[uart_num]){
        ret = ESP_ERR_NO_MEM;
        goto err;
    }
    p_uart_driver_obj[uart_num]->port_num = uart_num;

    gpio_num_t tx_pin = config_uart->tx_pin;
    gpio_num_t rx_pin = config_uart->rx_pin;

    p_uart_driver_obj[uart_num]->tx_pin = tx_pin;
    p_uart_driver_obj[uart_num]->rx_pin = rx_pin;

    p_uart_driver_obj[uart_num]->rx_ring_buf = xRingbufferCreateWithCaps(buf_len, RINGBUF_TYPE_BYTEBUF, UART_MALLOC_CAPS);
    if(!p_uart_driver_obj[uart_num]->rx_ring_buf){
        ret = ESP_ERR_NO_MEM;
        goto err;       
    }
    p_uart_driver_obj[uart_num]->rx_mux = xSemaphoreCreateMutexWithCaps(UART_MALLOC_CAPS);
    if(!p_uart_driver_obj[uart_num]->rx_mux){
        ret = ESP_ERR_NO_MEM;
        goto err;         
    }
    p_uart_driver_obj[uart_num]->tx_mux = xSemaphoreCreateMutexWithCaps(UART_MALLOC_CAPS);
    if(!p_uart_driver_obj[uart_num]->tx_mux){
        ret = ESP_ERR_NO_MEM;
        goto err;        
    }
    p_uart_driver_obj[uart_num]->rx_ready_sem = xSemaphoreCreateBinaryWithCaps(UART_MALLOC_CAPS);
    if(!p_uart_driver_obj[uart_num]->rx_ready_sem){
        ret = ESP_ERR_NO_MEM;
        goto err;          
    }

    p_uart_driver_obj[uart_num]->tx_done_sem = xSemaphoreCreateBinaryWithCaps(UART_MALLOC_CAPS);
    if(!p_uart_driver_obj[uart_num]->tx_done_sem){
        ret = ESP_ERR_NO_MEM;
        goto err;          
    }

    uart_dev_t *hw = UART_LL_GET_HW(uart_num);

    PERIPH_RCC_ATOMIC(){
        uart_ll_enable_bus_clock(uart_num, true);
    }
    PERIPH_RCC_ATOMIC(){
        uart_ll_reset_register(uart_num);
    }
    uart_ll_rxfifo_rst(hw);  
    uart_ll_txfifo_rst(hw);

    
    uint32_t baud = config_uart->baud_rate;
    uart_hw_flowcontrol_t flow_ctrl = config_uart->flow_ctrl;
    uart_word_length_t word_len = config_uart->word_len;
    uart_stop_bits_t stop_bits = config_uart->stop_bits;
    uart_parity_t parity = config_uart->parity;

    uint32_t rx_thrs = (config_uart->rx_thrs)? config_uart->rx_thrs: UART_FULL_THRESH_DEFAULT;
    uint32_t clk_freq = 0;


    ret = esp_clk_tree_src_get_freq_hz(UART_SCLK_DEFAULT, ESP_CLK_TREE_SRC_FREQ_PRECISION_CACHED, &clk_freq);
    if(ret != ESP_OK) goto err;

    if(!uart_ll_set_baudrate(hw, baud, clk_freq)){
        ret = ESP_FAIL;
        goto err;
    }
    uart_ll_set_data_bit_num(hw, word_len);
    uart_ll_set_stop_bits(hw, stop_bits);
    uart_ll_set_parity(hw, parity);
    uart_ll_set_hw_flow_ctrl(hw, flow_ctrl, rx_thrs);
    uart_ll_set_rxfifo_full_thr(hw, rx_thrs);
    uart_ll_set_rx_tout(hw, 10);  // 10 symbol periods; enables RXFIFO_TOUT interrupt

    gpio_reset_pin(tx_pin);
    gpio_reset_pin(rx_pin);
    gpio_set_direction(tx_pin, GPIO_MODE_OUTPUT);
    gpio_set_direction(rx_pin, GPIO_MODE_INPUT);
    gpio_pullup_en(rx_pin);

    gpio_matrix_input(rx_pin, uart_periph_signal[uart_num].pins[SOC_UART_PERIPH_SIGNAL_RX].signal, false);
    gpio_matrix_output(tx_pin, uart_periph_signal[uart_num].pins[SOC_UART_PERIPH_SIGNAL_TX].signal, false, false);

    ret = esp_intr_alloc(uart_periph_signal[uart_num].irq, ESP_INTR_FLAG_IRAM | ESP_INTR_FLAG_LOWMED, uart_driver_isr, p_uart_driver_obj[uart_num], &p_uart_driver_obj[uart_num]->intr_handle);
    ESP_LOGI(UART_TAG, "intr_alloc ret: %d", ret);

    if(ret != ESP_OK) goto err;
    uart_ll_clr_intsts_mask(hw, UART_LL_INTR_MASK);
    uart_ll_rxfifo_rst(hw);
    uart_ll_ena_intr_mask(hw, UART_INTR_RXFIFO_FULL | UART_INTR_RXFIFO_TOUT | UART_INTR_RXFIFO_OVF);
    ESP_LOGI(UART_TAG, "interrupts enabled");
    ESP_LOGI(UART_TAG, "intr ena status: 0x%lx", (unsigned long)uart_ll_get_intr_ena_status(hw));


    return ESP_OK;

err:
    uart_driver_dlt(uart_num);
    return ret;
    
}


esp_err_t uart_driver_transmit(uart_port_t uart_num, uint8_t *buf, size_t len){
    

    ESP_RETURN_ON_FALSE((uart_num < UART_NUM_MAX), ESP_FAIL, UART_TAG, "uart_num error"); 
    ESP_RETURN_ON_FALSE((p_uart_driver_obj[uart_num] != NULL), ESP_FAIL, UART_TAG, "driver not installed");
    ESP_RETURN_ON_FALSE((buf != NULL), ESP_FAIL, UART_TAG, "buf is empty"); 
    uart_driver_obj_t *uart_obj = p_uart_driver_obj[uart_num];

    uart_dev_t *hw = UART_LL_GET_HW(uart_num);

    uint32_t remaining_buf = (uint32_t)len;
    uint32_t available_fifo = 0;
    uint32_t to_write = 0;

    xSemaphoreTake(uart_obj->tx_mux, portMAX_DELAY);
    while(true){
        available_fifo = uart_ll_get_txfifo_len(hw);
        to_write = MIN(available_fifo, remaining_buf);
        if(to_write == 0){
            if(remaining_buf > 0){
                uart_ll_ena_intr_mask(hw, UART_INTR_TXFIFO_EMPTY);
                xSemaphoreTake(uart_obj->tx_done_sem, portMAX_DELAY);
            }
            else{
                break;
            }
        }
        ESP_LOGI(UART_TAG, "writing %lu bytes to FIFO", (unsigned long)to_write);
        uart_ll_write_txfifo(hw, buf, to_write);
        vTaskDelay(pdMS_TO_TICKS(10));  // wait for data to transmit
        buf += to_write;
        remaining_buf -= to_write;
        
    }
    xSemaphoreGive(uart_obj->tx_mux);
    return ESP_OK;
}

int uart_driver_receive(uart_port_t uart_num, uint8_t *buf, size_t len, TickType_t timeout){

    ESP_RETURN_ON_FALSE((uart_num < UART_NUM_MAX), (int)ESP_FAIL, UART_TAG, "uart_num error"); 
    ESP_RETURN_ON_FALSE((p_uart_driver_obj[uart_num] != NULL), (int)ESP_FAIL, UART_TAG, "driver not installed");
    ESP_RETURN_ON_FALSE((buf != NULL), -1, UART_TAG, "buf is NULL");
    uart_driver_obj_t *uart_obj = p_uart_driver_obj[uart_num];

    if(xSemaphoreTake(uart_obj->rx_ready_sem, timeout) != pdTRUE){
        ESP_LOGE("uart_driver", "sem take failed");
        return -1;
    }
    xSemaphoreTake(uart_obj->rx_mux, portMAX_DELAY);
    size_t written_len = 0;
    uint8_t *data = (uint8_t *)xRingbufferReceiveUpTo(uart_obj->rx_ring_buf, &written_len, timeout, len);
    if(!data){
        xSemaphoreGive(uart_obj->rx_mux);
        ESP_LOGE("uart_driver", "data is NULL");
        return -1;
    }
    memcpy(buf, data, written_len);
    vRingbufferReturnItem(uart_obj->rx_ring_buf, data);
    xSemaphoreGive(uart_obj->rx_mux);

    return (int)written_len;

}
