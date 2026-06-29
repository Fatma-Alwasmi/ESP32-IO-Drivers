# esp-IO-drivers

A collection of peripheral drivers for the ESP32, written in C using ESP-IDF. Each driver is implemented at the hardware register level using low-level HAL APIs, without relying on Espressif's higher-level driver abstractions. The goal is to demonstrate deep understanding of embedded IO protocols, interrupt-driven design, and hardware-software interfaces. so far only UART driver has been implemented. planning on adding more I/O drivers.

---

## Drivers

### UART Driver

A fully interrupt-driven UART driver for the ESP32 (tested on the Adafruit ESP32 Feather V2).


#### Hardware setup

- Board: Adafruit ESP32 Feather V2
- TX pin: GPIO25
- RX pin: GPIO26
- For the loopback test: connect GPIO25 to GPIO26 with a jumper wire

#### Project structure

```
esp-IO-drivers/
    components/
        uart_driver/
            include/
                uart_driver.h       # Public API
            uart_driver.c           # Driver implementation and ISR
            CMakeLists.txt
    main/
        main.c                      # Loopback test
        CMakeLists.txt
    CMakeLists.txt
    sdkconfig
```

#### Public API

```c
// Initialize the UART driver with the given configuration
esp_err_t uart_driver_init(uart_port_t uart_num, uart_driver_config_t *config, size_t rx_buf_size);

// Transmit data over UART
esp_err_t uart_driver_transmit(uart_port_t uart_num, uint8_t *buf, size_t len);

// Receive data over UART, blocking until data arrives or timeout expires
// Returns number of bytes received, or -1 on timeout/error
int uart_driver_receive(uart_port_t uart_num, uint8_t *buf, size_t len, TickType_t timeout);

// Deinitialize the driver and release all resources
esp_err_t uart_driver_delete(uart_port_t uart_num);
```

#### Configuration

```c
uart_driver_config_t config = {
    .baud_rate  = 115200,
    .word_len   = UART_DATA_8_BITS,
    .stop_bits  = UART_STOP_BITS_1,
    .parity     = UART_PARITY_DISABLE,
    .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
    .tx_pin     = GPIO_NUM_25,
    .rx_pin     = GPIO_NUM_26,
    .rx_thrs    = 0,  // 0 uses default threshold of 120 bytes
};
```

#### Build and flash

Prerequisites: ESP-IDF installed and environment sourced (`get_idf`)

```bash
idf.py set-target esp32
idf.py build
idf.py -p /dev/cu.usbserial-* flash monitor
```


#### Key debugging insights

During development, several non-obvious issues required hardware-level debugging:

- The ISR was registered and interrupt bits were enabled, but the ISR never fired. Inspecting the raw interrupt status register (`uart_ll_get_intraw_mask`) confirmed the hardware was generating interrupts, but they were not being delivered. The fix was explicitly resetting the RX FIFO and clearing all pending interrupt status bits before enabling interrupts, which cleared stale state left from hardware initialization.
- A `0xfc` byte appeared at the start of every received message. This was caused by the UART line transitioning through undefined states during GPIO matrix configuration, which the RX FIFO captured as a valid byte. A second FIFO reset at the end of initialization resolved this.
- GPIO8 and GPIO7 (the pins labeled TX/RX on the Feather V2) are internally connected to the ESP32's flash interface and cannot be used as general purpose IO. GPIO25 and GPIO26 were used instead.

---

## Planned drivers

- I2C
- SPI

---

## Environment

- Board: Adafruit ESP32 Feather V2 (ESP32 dual-core 240MHz, 8MB flash)
- Framework: ESP-IDF v6.1
- Language: C