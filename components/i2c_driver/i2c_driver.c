#include <string.h>
#include <sys/param.h>
#include <sys/lock.h>
#include "sdkconfig.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "esp_intr_alloc.h"
#include "esp_rom_sys.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/idf_additions.h"
#include "driver/gpio.h"
#include "soc/soc_caps.h"
#include "hal/i2c_ll.h"
#include "hal/i2c_periph.h"
#include "hal/gpio_types.h"
#include "esp_private/periph_ctrl.h"
#include "esp_private/esp_clk_tree_common.h"
#include "esp_clk_tree.h"
#include "i2c_driver.h"
#include "esp_private/gpio.h"
#include "esp_private/esp_gpio_reserve.h"
#include "esp_rom_gpio.h"


/*
How communication starts in I2C protocol:
In the master controller, there is a queue of commands (cmd0-cmd15), this queue will be filled with interactions that the master will execute.  The driver has to fill the commands with the correct sequence of commands. The first command will be START.

The serial clock SCL will be high while the serial data SDA will go from high to low

START:

    The time the line must be high called SDA_START_HOLD_LINE
     |
     V
————————————
SCL _____
		 \_____ 

SDA ___
		\__
————————————

SDA_HOLD_TIME is the minimum time that must pass after SCL falls before the master is allowed to change SDA to give the slave time to recognize that the clock cycle ended before the master starts changing the sea to prepare the next bit

STOP
————————————
SCL        _____
	    ___/	        

SDA          _____
	      ____/   
————————————

RSTART lets you issue a new START condition while still holding the bus, keeping the transaction atomic
*/
#define I2C_MALLOC_CAPS  (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)

static const char *I2C_TAG = "i2c_driver";
typedef struct {
    i2c_port_t i2c_num; // port num of i2c
    SemaphoreHandle_t done_sem; // signal transaction complete
    SemaphoreHandle_t mutex; // prevent conccurent transactions
    intr_handle_t intr_handle; // to register interrupt
    gpio_num_t sda_pin;
    gpio_num_t scl_pin;
    i2c_intr_event_t last_event; //stores isr result
} i2c_obj_driver_t;

/* allocate memory for i2c_obj_driver_t and nullify all feilds */
static i2c_obj_driver_t *p_i2c_driver_obj[SOC_I2C_NUM] = {0};

static void i2c_free_driver_obj(i2c_obj_driver_t *i2c_obj){
    if(i2c_obj->done_sem){
        vSemaphoreDeleteWithCaps(i2c_obj->done_sem);
    }
    if(i2c_obj->mutex){
        vSemaphoreDeleteWithCaps(i2c_obj->mutex);
    }
    heap_caps_free(i2c_obj);
}

esp_err_t i2c_driver_delete(i2c_port_t i2c_num){
    ESP_RETURN_ON_FALSE((i2c_num < SOC_I2C_NUM), ESP_FAIL, I2C_TAG, "i2c_num error");
    if(!p_i2c_driver_obj[i2c_num]){
        ESP_LOGI(I2C_TAG, "ALREADY NULL");
        return ESP_OK;
    }

    // reset gpio pins to default state
    gpio_reset_pin(p_i2c_driver_obj[i2c_num]->sda_pin);
    gpio_reset_pin(p_i2c_driver_obj[i2c_num]->scl_pin);

    esp_intr_free(p_i2c_driver_obj[i2c_num]->intr_handle);
    i2c_free_driver_obj(p_i2c_driver_obj[i2c_num]);
    p_i2c_driver_obj[i2c_num] = NULL;
    return ESP_OK;

}

// reason this function takes pointer to void rather than pointer to obj struct is because
// esp_intr_alloc requires specific function signature for the isr.
static void IRAM_ATTR i2c_driver_isr(void *arg){

    i2c_obj_driver_t *i2c_obj = (i2c_obj_driver_t *)arg;

    i2c_dev_t *hw = I2C_LL_GET_HW(i2c_obj->i2c_num);
    BaseType_t HPTaskAwoken = 0;
    i2c_intr_event_t event;

    i2c_ll_master_get_event(hw, &event);
    i2c_obj->last_event = event;

    i2c_ll_clear_intr_mask(hw, I2C_LL_INTR_MASK);
    i2c_ll_disable_intr_mask(hw, I2C_LL_INTR_MASK);
    xSemaphoreGiveFromISR(i2c_obj->done_sem, &HPTaskAwoken);
    portYIELD_FROM_ISR(HPTaskAwoken);
}

esp_err_t i2c_driver_init(i2c_port_t i2c_num, i2c_driver_config_t *config_i2c){

    esp_err_t ret;
    // validations to make sure parameters are correct before using them
    ESP_RETURN_ON_FALSE((i2c_num < SOC_I2C_NUM), ESP_FAIL, I2C_TAG, "i2c_num error");
    ESP_RETURN_ON_FALSE((config_i2c != NULL), ESP_FAIL, I2C_TAG, "config_i2c is NULL");
    ESP_RETURN_ON_FALSE((p_i2c_driver_obj[i2c_num] == NULL), ESP_FAIL, I2C_TAG, "i2c driver already installed");

    // allocate memory for the struct 
    p_i2c_driver_obj[i2c_num] = heap_caps_calloc(1, sizeof(i2c_obj_driver_t), I2C_MALLOC_CAPS);

    // make sure allocation is successful, if not return mem error
    if(p_i2c_driver_obj[i2c_num] == NULL){
        ret = ESP_ERR_NO_MEM;
        goto err;
    }

    // set the pins in the struct to the pins the user passed
    gpio_num_t sda_pin = config_i2c->sda_pin;
    gpio_num_t scl_pin = config_i2c->scl_pin;
    p_i2c_driver_obj[i2c_num]->i2c_num = i2c_num;
    p_i2c_driver_obj[i2c_num]->sda_pin = sda_pin;
    p_i2c_driver_obj[i2c_num]->scl_pin = scl_pin;

    // create done_sem
    p_i2c_driver_obj[i2c_num]->done_sem = xSemaphoreCreateBinaryWithCaps(I2C_MALLOC_CAPS);
    if(!p_i2c_driver_obj[i2c_num]->done_sem){
        ret = ESP_FAIL;
        goto err;
    }
    // create mutex
    p_i2c_driver_obj[i2c_num]->mutex = xSemaphoreCreateMutexWithCaps(I2C_MALLOC_CAPS);
    if(!p_i2c_driver_obj[i2c_num]->mutex){
        ret = ESP_FAIL;
        goto err;
    }

    // initialize hardware

    /* this enables the bus clock for the i2c hardware clock, this is needed becuase the esp preserves power
    for hardware blocks that are not used and they need to be "turned on" before usage and allows register access
     */
    PERIPH_RCC_ATOMIC(){
        i2c_ll_enable_bus_clock(i2c_num, true);
    }

    // this function resets the i2c registers, if the registers have been used before, they will have stale data
    PERIPH_RCC_ATOMIC(){
        i2c_ll_reset_register(i2c_num);
    }    

    /* 
    these function calls configure the direction of the data transmited on the pins
    GPIO_MODE_OUTPUT_OD means that the pin will be an output pin, OD means open drain.
    we want the scl line to be an output because the master is the one that controls the clock,
    it doesnt read a clock from a slave. GPIO_MODE_INPUT_OUTPUT_OD means the line is both input and output.
    in i2c, the sda is bidirectional. OD means open drain, this means only the pullup resistor can drive the line
    high and the sda can only drive it low.
    */

    // first, reset pins
    gpio_reset_pin(sda_pin);
    gpio_reset_pin(scl_pin);

    gpio_set_direction(scl_pin, GPIO_MODE_INPUT_OUTPUT_OD);
    gpio_set_direction(sda_pin, GPIO_MODE_INPUT_OUTPUT_OD);

    // this connects the pins to the i2c perihperal signals.
    gpio_matrix_input(sda_pin, i2c_periph_signal[i2c_num].sda_in_sig, false);
    gpio_matrix_output(sda_pin, i2c_periph_signal[i2c_num].sda_out_sig, false, false);
    gpio_matrix_input(scl_pin, i2c_periph_signal[i2c_num].scl_in_sig, false);
    gpio_matrix_output(scl_pin, i2c_periph_signal[i2c_num].scl_out_sig, false, false);

    // this function returns pointer to the i2c peripheral's hardware register struct (i2c_dev_t) for the given
    // port number
    i2c_dev_t *hw = I2C_LL_GET_HW(i2c_num);

    // this sets the mode to master since the esp is the master
    i2c_ll_set_mode(hw, I2C_BUS_MODE_MASTER);
 
    i2c_ll_enable_pins_open_drain(hw, true);

    if(config_i2c->sda_pullup_en) gpio_pullup_en(sda_pin);
    if(config_i2c->scl_pullup_en) gpio_pullup_en(scl_pin);

    uint32_t apb_clk;
    ret = esp_clk_tree_src_get_freq_hz(SOC_MOD_CLK_APB, ESP_CLK_TREE_SRC_FREQ_PRECISION_CACHED, &apb_clk);
    if(ret != ESP_OK) goto err;

    i2c_hal_clk_config_t clk_cfg;
    i2c_ll_master_cal_bus_clk(apb_clk, config_i2c->freq_hz, &clk_cfg);
    i2c_ll_master_set_bus_timing(hw, &clk_cfg); // Configure the I2C bus timing related register

    // FIFO mode: TX/RX data goes through APB path (0x6001301c), which works correctly on ESP32.
    // Non-FIFO (ram_data) writes go through DPORT (0x3FF53100) which does not reliably update the I2C RAM.
    i2c_ll_enable_fifo_mode(hw, true);

    // clear fifos from stale data
    i2c_ll_txfifo_rst(hw);
    i2c_ll_rxfifo_rst(hw);

    // the filter is to ignore noise on the line and noise is considered any pulse smaller than filter_num
    i2c_ll_master_set_filter(hw, 7);

    // reset leaves timeout at 0x10 (~200ns at 80MHz APB), far too short for any real transaction
    i2c_ll_set_tout(hw, 0xFFFFF);

    ret = esp_intr_alloc(i2c_periph_signal[i2c_num].irq, ESP_INTR_FLAG_IRAM | ESP_INTR_FLAG_LOWMED, i2c_driver_isr, p_i2c_driver_obj[i2c_num], &p_i2c_driver_obj[i2c_num]->intr_handle);
    if(ret != ESP_OK) goto err;

    i2c_ll_clear_intr_mask(hw, I2C_LL_INTR_MASK);    // clear any stale pending status
    i2c_ll_disable_intr_mask(hw, I2C_LL_INTR_MASK);  // disable all so nothing fires unexpectedly


    return ESP_OK;
err:
    i2c_driver_delete(i2c_num);
    return ret;
}

esp_err_t i2c_driver_write(i2c_port_t i2c_num, uint8_t slave_addr, uint8_t *buf, size_t len){

    ESP_RETURN_ON_FALSE((i2c_num < SOC_I2C_NUM), ESP_FAIL, I2C_TAG, "i2c_num error");
    ESP_RETURN_ON_FALSE((slave_addr <= 0x7F), ESP_FAIL, I2C_TAG, "invalid slave_addr");
    ESP_RETURN_ON_FALSE((buf != NULL), ESP_FAIL, I2C_TAG, "buf is empty");
    ESP_RETURN_ON_FALSE((p_i2c_driver_obj[i2c_num] != NULL), ESP_FAIL, I2C_TAG, "Driver not initialized");
    i2c_obj_driver_t *i2c_obj =  p_i2c_driver_obj[i2c_num];

    xSemaphoreTake(i2c_obj->mutex, portMAX_DELAY); // take mutex, block until bus is free
    i2c_ll_hw_cmd_t cmd = {0};

    i2c_dev_t *hw = I2C_LL_GET_HW(i2c_num);

    i2c_ll_txfifo_rst(hw);
    i2c_ll_rxfifo_rst(hw);

    uint8_t addr_byte = (slave_addr << 1) | 0;
    i2c_ll_write_txfifo(hw, &addr_byte, 1);
    i2c_ll_write_txfifo(hw, buf, len);

    cmd.op_code = I2C_LL_CMD_RESTART; // first set cmd to 0 to start
    i2c_ll_master_write_cmd_reg(hw, cmd, 0); // write cmd to cmd queue at index 0

    cmd.val = 0; // reset byte_num, ack_en and ack_exp
    cmd.op_code = I2C_LL_CMD_WRITE; // opcode 1
    cmd.byte_num = len + 1; // 1 address byte + data bytes
    cmd.ack_en = 1; // enable ack checking
    cmd.ack_exp = 0; // expect ack (ack = 0, nack = 1)
    i2c_ll_master_write_cmd_reg(hw, cmd, 1); // now write the commands to the cmd queue at index 1

    cmd.val = 0; // reset byte_num, ack_en and ack_exp
    cmd.op_code = I2C_LL_CMD_STOP;
    i2c_ll_master_write_cmd_reg(hw, cmd, 2);


    i2c_ll_clear_intr_mask(hw, I2C_LL_INTR_MASK); // clear any stale intr
    i2c_ll_enable_intr_mask(hw, I2C_LL_MASTER_TX_INT); // enable master tx interrupt
    i2c_ll_start_trans(hw); // start i2c transfer, this triggers hardware to begin executing cmd queue
   
    if(xSemaphoreTake(i2c_obj->done_sem, pdMS_TO_TICKS(1000)) != pdTRUE){
        xSemaphoreGive(i2c_obj->mutex);
        ESP_LOGE("driver", "sem timeout");
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t result = ESP_FAIL;
    if(i2c_obj->last_event == I2C_INTR_EVENT_NACK){
        result = ESP_ERR_NOT_FOUND;
    }
    else if(i2c_obj->last_event == I2C_INTR_EVENT_TOUT){
        ESP_LOGE("driver", "last event timeout");
        result = ESP_ERR_TIMEOUT;
    }
    else if(i2c_obj->last_event == I2C_INTR_EVENT_ARBIT_LOST){
        result = ESP_FAIL;
    }
    else if(i2c_obj->last_event == I2C_INTR_EVENT_TRANS_DONE){
        result = ESP_OK;
    }
    xSemaphoreGive(i2c_obj->mutex); // release mutex
    return result;
}

int i2c_driver_read(i2c_port_t i2c_num, uint8_t slave_addr, uint8_t *buf, size_t len, TickType_t timeout){

    ESP_RETURN_ON_FALSE((i2c_num < SOC_I2C_NUM), -1, I2C_TAG, "i2c_num error");
    ESP_RETURN_ON_FALSE((slave_addr <= 0x7F), -1, I2C_TAG, "invalid slave_addr");
    ESP_RETURN_ON_FALSE((buf != NULL), -1, I2C_TAG, "buf not empty");
    ESP_RETURN_ON_FALSE((p_i2c_driver_obj[i2c_num] != NULL), -1, I2C_TAG, "Driver not initialized");
    i2c_obj_driver_t *i2c_obj =  p_i2c_driver_obj[i2c_num];

    xSemaphoreTake(i2c_obj->mutex, portMAX_DELAY); // take mutex, block until bus is free
    i2c_ll_hw_cmd_t cmd = {0};

    i2c_dev_t *hw = I2C_LL_GET_HW(i2c_num);

    i2c_ll_txfifo_rst(hw);
    i2c_ll_rxfifo_rst(hw);

    uint8_t addr_byte = (slave_addr << 1) | 1;
    i2c_ll_write_txfifo(hw, &addr_byte, 1);

    cmd.op_code = I2C_LL_CMD_RESTART; // first set cmd to 0 to start
    i2c_ll_master_write_cmd_reg(hw, cmd, 0); // write cmd to cmd queue at index 0

    cmd.val = 0; // reset byte_num, ack_en and ack_exp
    cmd.op_code = I2C_LL_CMD_WRITE; // opcode 1
    cmd.byte_num = 1; // 1 address byte 
    cmd.ack_en = 1; // enable ack checking
    cmd.ack_exp = 0; // expect ack (ack = 0, nack = 1)
    i2c_ll_master_write_cmd_reg(hw, cmd, 1); // now write the commands to the cmd queue at index 1

    cmd.val = 0;
    cmd.op_code = I2C_LL_CMD_READ; // opcode 2
    cmd.byte_num = len; // len data to read
    cmd.ack_val = 0; // ack each byte
    i2c_ll_master_write_cmd_reg(hw, cmd, 2);

    cmd.val = 0; // reset byte_num, ack_en and ack_exp
    cmd.op_code = I2C_LL_CMD_STOP;
    i2c_ll_master_write_cmd_reg(hw, cmd, 3);


    i2c_ll_clear_intr_mask(hw, I2C_LL_INTR_MASK); // clear any stale intr
    i2c_ll_enable_intr_mask(hw, I2C_LL_MASTER_RX_INT); // enable master rx interrupt
    i2c_ll_start_trans(hw); // start i2c transfer, this triggers hardware to begin executing cmd queue
   
    if(xSemaphoreTake(i2c_obj->done_sem, timeout) != pdTRUE){
        xSemaphoreGive(i2c_obj->mutex);
        return -1;
    }

    int result = -1;
    if(i2c_obj->last_event == I2C_INTR_EVENT_NACK){
        result = -1;
    }
    else if(i2c_obj->last_event == I2C_INTR_EVENT_TOUT){
        result = -1;
    }
    else if(i2c_obj->last_event == I2C_INTR_EVENT_ARBIT_LOST){
        result = -1;
    }
    else if(i2c_obj->last_event == I2C_INTR_EVENT_TRANS_DONE){
        i2c_ll_read_rxfifo(hw, buf, len);
        result = len;
    }

    xSemaphoreGive(i2c_obj->mutex); // release mutex
    return result;
}