#pragma once
#include <stdint.h>

/* LCD Pin Configuration (Match your wiring) */
#define LCD_PIN_RS 22
#define LCD_PIN_EN 23
#define LCD_PIN_D4 21
#define LCD_PIN_D5 17
#define LCD_PIN_D6 16
#define LCD_PIN_D7 4

void lcd_init(void);
void lcd_clear(void);
void lcd_set_cursor(uint8_t col, uint8_t row);
void lcd_write_str(const char *str);