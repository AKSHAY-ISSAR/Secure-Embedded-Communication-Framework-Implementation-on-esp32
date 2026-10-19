#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Creates the event queue and starts the protocol/crypto task on core 1. */
void protocol_start(void);

/* Called from the ESP-NOW receive callback (core 0). Non-blocking. */
void protocol_post_frame(const uint8_t mac[6], const uint8_t *data, size_t len);

/* Called from the console task and the telemetry task. Returns false if the
 * event queue is full. */
bool protocol_post_text(const uint8_t *text, size_t len);

bool protocol_is_connected(void);
