// SPDX-License-Identifier: Apache-2.0
// Copyright ...

#include "sdkconfig.h"
#include <stdio.h>
#include <string.h>

// ESP-IDF / Logging
#include "esp_log.h"

// ESP-IDF / UART
#include "driver/uart.h"
#include "driver/gpio.h"

// FreeRTOS
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// BTstack / Bluepad32
#include <btstack_port_esp32.h>
#include <btstack_run_loop.h>
#include <btstack_stdio_esp32.h>
#include <arduino_platform.h>
#include <uni.h>

// Bluepad32 device / controller definitions
#include "uni_hid_device.h"
#include "controller/uni_balance_board.h"  // Correct include path

#define TAG "MAIN"

// UART Configuration
#define UART_NUM UART_NUM_2            // Use UART2 to avoid conflicts
#define UART_TX_GPIO GPIO_NUM_17        // GPIO17 for UART2 TX
#define UART_RX_GPIO UART_PIN_NO_CHANGE // Not using RX

#define POLL_INTERVAL_MS 100            // Poll every 100 milliseconds

// -----------------------------------------
// 1. UART Setup on UART_NUM TX
// -----------------------------------------
static void setup_uart(void) {
    ESP_LOGI(TAG, "Setting up UART%d...", UART_NUM);

    const uart_config_t uart_config = {
        .baud_rate = 115200,                        // Set baud rate to 115200
        .data_bits = UART_DATA_8_BITS,              // 8 data bits
        .parity    = UART_PARITY_DISABLE,           // No parity
        .stop_bits = UART_STOP_BITS_1,              // 1 stop bit
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,      // No flow control
    };
    
    esp_err_t err;

    // Configure UART parameters
    err = uart_param_config(UART_NUM, &uart_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "UART%d param config failed, error = %d", UART_NUM, err);
        return;
    }
    
    // Set UART pins (TX on GPIO17, no RX)
    err = uart_set_pin(UART_NUM, UART_TX_GPIO, UART_RX_GPIO, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "UART%d set pin failed, error = %d", UART_NUM, err);
        return;
    }
    
    // Install UART driver
    err = uart_driver_install(UART_NUM, 1024, 0, 0, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "UART%d driver install failed, error = %d", UART_NUM, err);
    } else {
        ESP_LOGI(TAG, "UART%d setup complete on GPIO%d (TX).", UART_NUM, UART_TX_GPIO);
    }
}

// -----------------------------------------
// 2. Function to Send Balance Board Data over UART
// -----------------------------------------
static void send_balance_board_data(const uni_balance_board_t* bb) {
    char buffer[128];
    int len = snprintf(buffer, sizeof(buffer),
             "WiiBB: tl=%d, tr=%d, bl=%d, br=%d, temp=%d\r\n",
             bb->tl, bb->tr, bb->bl, bb->br, bb->temperature);
    if (len > 0 && len < sizeof(buffer)) {
        uart_write_bytes(UART_NUM, buffer, len);
        ESP_LOGI(TAG, "Sent data over UART: %s", buffer);
    } else {
        ESP_LOGE(TAG, "Failed to format balance board data.");
    }
}

// -----------------------------------------
// 3. Polling Task to Read Balance Board Data
// -----------------------------------------
static void poll_balance_board_task(void *pvParameter) {
    ESP_LOGI(TAG, "Starting balance board polling task...");
    
    while (1) {
        // Get the first connected HID device
        uni_hid_device_t* device = uni_hid_device_get_instance_for_idx(0); // Only one device

        if (device && device->controller.klass == UNI_CONTROLLER_CLASS_BALANCE_BOARD) {
            const uni_balance_board_t* bb = &device->controller.balance_board;
            
            // Send the balance board data over UART
            send_balance_board_data(bb);
            
            // Log the data
            ESP_LOGI(TAG, "Balance Board Data - TL: %d, TR: %d, BL: %d, BR: %d, Temp: %d",
                     bb->tl, bb->tr, bb->bl, bb->br, bb->temperature);
        } else {
            ESP_LOGW(TAG, "No Balance Board connected.");
        }
        
        // Delay for the poll interval
        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
    }
}

// -----------------------------------------
//
// 4. Autostart: app_main
//
// -----------------------------------------
#if CONFIG_AUTOSTART_ARDUINO
void initBluepad32() {
#else
int app_main(void) {
#endif  // !CONFIG_AUTOSTART_ARDUINO
    // Optional: Enable HCI dump for debugging
    // hci_dump_open(NULL, HCI_DUMP_STDOUT);

    // Don't use BTstack buffered UART. It conflicts with the console.
#ifndef CONFIG_ESP_CONSOLE_UART_NONE
#ifndef CONFIG_BLUEPAD32_USB_CONSOLE_ENABLE
    btstack_stdio_init();
#endif  // CONFIG_BLUEPAD32_USB_CONSOLE_ENABLE
#endif  // CONFIG_ESP_CONSOLE_UART_NONE

    ESP_LOGI(TAG, "Initializing BTstack + Bluepad32...");

    // Initialize BTstack
    btstack_init();

    // Must be called before uni_init()
    uni_platform_set_custom(get_arduino_platform());

    // Initialize Bluepad32
    uni_init(0 /* argc */, NULL /* argv */);

    // Set up UART to forward data
    setup_uart();

    // Create the polling task
    xTaskCreate(poll_balance_board_task, "poll_balance_board_task", 4096, NULL, 5, NULL);

    // Start the BTstack run loop (never returns)
    btstack_run_loop_execute();

#if !CONFIG_AUTOSTART_ARDUINO
    return 0;
#endif  // !CONFIG_AUTOSTART_ARDUINO
}