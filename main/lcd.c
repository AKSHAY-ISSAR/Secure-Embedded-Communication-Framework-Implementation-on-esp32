#include "lcd.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static void lcd_pulse_enable(void) {
    gpio_set_level(LCD_PIN_EN, 0);
    esp_rom_delay_us(1);
    gpio_set_level(LCD_PIN_EN, 1);
    esp_rom_delay_us(1);
    gpio_set_level(LCD_PIN_EN, 0);
    esp_rom_delay_us(50);
}

static void lcd_send_nibble(uint8_t data) {
    gpio_set_level(LCD_PIN_D4, (data >> 0) & 0x01);
    gpio_set_level(LCD_PIN_D5, (data >> 1) & 0x01);
    gpio_set_level(LCD_PIN_D6, (data >> 2) & 0x01);
    gpio_set_level(LCD_PIN_D7, (data >> 3) & 0x01);
    lcd_pulse_enable();
}

static void lcd_send_byte(uint8_t rs, uint8_t data) {
    gpio_set_level(LCD_PIN_RS, rs);
    lcd_send_nibble(data >> 4);
    lcd_send_nibble(data & 0x0F);
    if (rs == 0) vTaskDelay(pdMS_TO_TICKS(2)); // Commands need more time
}

void lcd_clear(void) {
    lcd_send_byte(0, 0x01);
}

void lcd_set_cursor(uint8_t col, uint8_t row) {
    uint8_t row_offsets[] = { 0x00, 0x40 };
    if (row > 1) row = 1;
    lcd_send_byte(0, 0x80 | (col + row_offsets[row]));
}

void lcd_write_str(const char *str) {
    int count = 0;
    while (*str) {
        if (count == 16) lcd_set_cursor(0, 1); // Auto-wrap to line 2
        if (count >= 32) break;                // Stop if screen is full
        
        lcd_send_byte(1, *str++);
        count++;
    }
}

void lcd_init(void) {
    uint8_t pins[] = {LCD_PIN_RS, LCD_PIN_EN, LCD_PIN_D4, LCD_PIN_D5, LCD_PIN_D6, LCD_PIN_D7};
    for (int i = 0; i < 6; i++) {
        gpio_reset_pin(pins[i]);
        gpio_set_direction(pins[i], GPIO_MODE_OUTPUT);
        gpio_set_level(pins[i], 0);
    }
    vTaskDelay(pdMS_TO_TICKS(50)); // Wait for LCD to power up

    /* 4-Bit Initialization Sequence */
    lcd_send_nibble(0x03); vTaskDelay(pdMS_TO_TICKS(5));
    lcd_send_nibble(0x03); esp_rom_delay_us(150);
    lcd_send_nibble(0x03);
    lcd_send_nibble(0x02); // Switch to 4-bit mode

    lcd_send_byte(0, 0x28); // 4-bit, 2 lines, 5x8 font
    lcd_send_byte(0, 0x0C); // Display ON, Cursor OFF
    lcd_clear();
    lcd_send_byte(0, 0x06); // Entry mode: increment cursor
}