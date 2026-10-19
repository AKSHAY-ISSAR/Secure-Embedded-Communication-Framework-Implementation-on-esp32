#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_wifi.h"
#include "protocol.h"
#include "packet.h"
#include "link_config.h"

#if CONFIG_LINK_ENABLE_TELEMETRY

/* Autonomous sender for the node that has no laptop attached.
 * vTaskDelayUntil keeps the period stable regardless of how long the
 * sampling takes. */
static void telemetry_task(void *arg)
{
    (void)arg;
    TickType_t last = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(CONFIG_LINK_TELEMETRY_PERIOD_MS);
    uint32_t counter = 0;
    char line[PKT_PAYLOAD_MAX];

    for (;;) {
        vTaskDelayUntil(&last, period);

        if (!protocol_is_connected())
            continue;   /* nothing to do until the session is up */

        int64_t uptime_s = esp_timer_get_time() / 1000000;
        size_t  heap     = heap_caps_get_free_size(MALLOC_CAP_8BIT);

        int n = snprintf(line, sizeof(line),
                         "TLM n=%lu up=%llds heap=%u",
                         (unsigned long)(++counter),
                         (long long)uptime_s,
                         (unsigned)heap);
        if (n > 0)
            protocol_post_text((const uint8_t *)line,
                               (size_t)(n > (int)sizeof(line) ? (int)sizeof(line) : n));
    }
}

void telemetry_start(void)
{
    xTaskCreatePinnedToCore(telemetry_task, "telemetry", 3072, NULL,
                            TELEMETRY_TASK_PRIO, NULL, CORE_APP);
}

#else
void telemetry_start(void) { }
#endif
