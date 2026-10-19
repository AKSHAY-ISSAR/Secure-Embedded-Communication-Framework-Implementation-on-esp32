#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"    // <-- Added for GPIO control

#include "link_config.h"
#include "espnow_link.h"
#include "protocol.h"
#include "console_io.h"
#include "bldc.h"
#include "lcd.h"
#define ACTION_PIN 2        // Default DevKit onboard LED

void telemetry_start(void);

void app_main(void)
{
    printf("\n[Node " NODE_NAME "] boot ok\n");

#if !CONFIG_LINK_ROLE_A
    // Initialize the hardware action pin only on the battery-powered responder
    gpio_reset_pin(ACTION_PIN);
    gpio_set_direction(ACTION_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(ACTION_PIN, 0);
    bldc_init();  
    lcd_init();
    lcd_write_str("Node B Ready!");                          /* Setup PWM & arm ESCs */
        /* Wi-Fi + ESP-NOW */
#endif
    ESP_ERROR_CHECK(espnow_link_init());
    console_start();                       /* core 1: laptop UART bridge        */
    protocol_start();                      /* core 1: crypto + state machine    */
    telemetry_start();                     /* core 1: periodic autonomous data  */
}