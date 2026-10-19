#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "console_io.h"
#include "protocol.h"
#include "packet.h"
#include "link_config.h"

#define CONSOLE_UART  UART_NUM_0
#define LINE_MAX      PKT_PAYLOAD_MAX

#if CONFIG_LINK_ENABLE_CONSOLE

static uint8_t s_line[LINE_MAX];
static size_t  s_fill;

static void console_task(void *arg)
{
    (void)arg;
    uint8_t chunk[64];

    for (;;) {
        int n = uart_read_bytes(CONSOLE_UART, chunk, sizeof(chunk), pdMS_TO_TICKS(100));
        for (int i = 0; i < n; i++) {
            uint8_t c = chunk[i];

            if (c == '\r') continue;
            if (c == '\n') {
                if (s_fill) {
                    protocol_post_text(s_line, s_fill);
                    s_fill = 0;
                }
                continue;
            }
            if (s_fill < LINE_MAX) {
                s_line[s_fill++] = c;
            } else {
                /* line too long: flush what we have and keep going */
                protocol_post_text(s_line, s_fill);
                s_fill = 0;
                s_line[s_fill++] = c;
            }
        }
    }
}

void console_start(void)
{
    uart_config_t cfg = {
        .baud_rate  = 115200,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(CONSOLE_UART, 2048, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(CONSOLE_UART, &cfg));

    xTaskCreatePinnedToCore(console_task, "console", 4096, NULL,
                            CONSOLE_TASK_PRIO, NULL, CORE_APP);
}

#else
void console_start(void) { }
#endif

void console_write_line(const uint8_t *data, size_t len)
{
    fwrite(data, 1, len, stdout);
    fputc('\n', stdout);
    fflush(stdout);
}
