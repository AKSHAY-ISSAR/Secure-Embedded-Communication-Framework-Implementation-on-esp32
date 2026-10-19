#pragma once
#include <stdint.h>
#include <stdbool.h>

/* Configure your ESC GPIO pins */
#define MOTOR_PIN_1        18
#define MOTOR_PIN_2        19

typedef enum {
    MOTOR_MODE_MANUAL = 0,
    MOTOR_MODE_RAMP_DEMO,
    MOTOR_MODE_PULSE_DEMO,
    MOTOR_MODE_ALTERNATING_DEMO
} motor_pattern_t;

void bldc_init(void);
void bldc_set_speed(uint8_t motor_id, float pct); /* 0.0% to 100.0% */
void bldc_set_pattern(motor_pattern_t pattern);
void bldc_emergency_stop(void);
