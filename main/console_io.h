#pragma once
#include <stdint.h>
#include <stddef.h>

void console_start(void);
/* Emit one received message to the laptop, newline-terminated. */
void console_write_line(const uint8_t *data, size_t len);
