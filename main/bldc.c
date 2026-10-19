#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "bldc.h"

static const char *TAG = "bldc";

#define LEDC_TIMER              LEDC_TIMER_0
#define LEDC_MODE               LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL_M1         LEDC_CHANNEL_0
#define LEDC_CHANNEL_M2         LEDC_CHANNEL_1
#define LEDC_DUTY_RES           LEDC_TIMER_14_BIT   /* 16384 levels */
#define LEDC_FREQUENCY          50                  /* 50 Hz = 20ms period */

/* At 50Hz and 14-bit resolution:
 * 1 ms (1000us) pulse = (1 / 20) * 16384 = ~819
 * 2 ms (2000us) pulse = (2 / 20) * 16384 = ~1638 */
#define ESC_MIN_DUTY            819  /* */
#define ESC_MAX_DUTY            1638 /*[cite: 1] */

/* --- NEW SAFETY MACROS --- */
#define MAX_SAFE_SPEED_PCT      50.0f   /* Hard cap for manual speed inputs to prevent runaways */
#define DEFAULT_SAFE_SPEED_PCT  15.0f   /* Default running speed if an invalid command is sent */

static motor_pattern_t s_current_pattern = MOTOR_MODE_MANUAL;
static float s_target_m1 = 0.0f;
static float s_target_m2 = 0.0f;

static void write_esc_pulse(ledc_channel_t ch, float pct)
{
    if (pct < 0.0f) pct = 0.0f;
    if (pct > 100.0f) pct = 100.0f;

    uint32_t duty = ESC_MIN_DUTY + (uint32_t)((ESC_MAX_DUTY - ESC_MIN_DUTY) * (pct / 100.0f));
    ledc_set_duty(LEDC_MODE, ch, duty);
    ledc_update_duty(LEDC_MODE, ch);
}

void bldc_set_speed(uint8_t motor_id, float speed_percent)
{
    /* Enforce Safety Mechanism */
    if (speed_percent > MAX_SAFE_SPEED_PCT) {
        ESP_LOGW(TAG, "Requested speed (%.1f%%) exceeds safety cap! Constraining to %.1f%%", 
                 speed_percent, MAX_SAFE_SPEED_PCT);
        speed_percent = MAX_SAFE_SPEED_PCT;
    } else if (speed_percent < 0.0f) {
        ESP_LOGW(TAG, "Requested speed is negative. Defaulting to safe running speed.");
        speed_percent = DEFAULT_SAFE_SPEED_PCT; 
    }

    s_current_pattern = MOTOR_MODE_MANUAL; /*[cite: 1] */
    
    if (motor_id == 1 || motor_id == 0) s_target_m1 = speed_percent; /*[cite: 1] */
    if (motor_id == 2 || motor_id == 0) s_target_m2 = speed_percent; /*[cite: 1] */

    if (motor_id == 1 || motor_id == 0) write_esc_pulse(LEDC_CHANNEL_M1, speed_percent); /*[cite: 1] */
    if (motor_id == 2 || motor_id == 0) write_esc_pulse(LEDC_CHANNEL_M2, speed_percent); /*[cite: 1] */
}
void bldc_set_pattern(motor_pattern_t pattern)
{
    s_current_pattern = pattern;
}

void bldc_emergency_stop(void)
{
    s_current_pattern = MOTOR_MODE_MANUAL;
    s_target_m1 = 0.0f;
    s_target_m2 = 0.0f;
    write_esc_pulse(LEDC_CHANNEL_M1, 0.0f);
    write_esc_pulse(LEDC_CHANNEL_M2, 0.0f);
}

/* Background animation/ramp task to keep smooth visual aesthetics */
static void bldc_animation_task(void *pvParameters)
{
    (void)pvParameters;
    float phase = 0.0f;
    bool direction = true;

    for (;;) {
        switch (s_current_pattern) {
        case MOTOR_MODE_RAMP_DEMO:
            /* Smooth ramp between 10% and 35% throttle */
            if (direction) {
                phase += 0.5f;
                if (phase >= 35.0f) direction = false;
            } else {
                phase -= 0.5f;
                if (phase <= 10.0f) direction = true;
            }
            write_esc_pulse(LEDC_CHANNEL_M1, phase);
            write_esc_pulse(LEDC_CHANNEL_M2, phase);
            vTaskDelay(pdMS_TO_TICKS(25));
            break;

        case MOTOR_MODE_ALTERNATING_DEMO:
            /* Alternate spinning motor 1 and motor 2 */
            write_esc_pulse(LEDC_CHANNEL_M1, 25.0f);
            write_esc_pulse(LEDC_CHANNEL_M2, 0.0f);
            vTaskDelay(pdMS_TO_TICKS(1800));
            write_esc_pulse(LEDC_CHANNEL_M1, 0.0f);
            write_esc_pulse(LEDC_CHANNEL_M2, 25.0f);
            vTaskDelay(pdMS_TO_TICKS(1200));
            break;

        case MOTOR_MODE_PULSE_DEMO:
            /* Rev up pulses */
            for (float s = 10.0f; s <= 30.0f; s += 2.0f) {
                write_esc_pulse(LEDC_CHANNEL_M1, s);
                write_esc_pulse(LEDC_CHANNEL_M2, s);
                vTaskDelay(pdMS_TO_TICKS(20));
            }
            vTaskDelay(pdMS_TO_TICKS(250));
            for (float s = 30.0f; s >= 10.0f; s -= 2.0f) {
                write_esc_pulse(LEDC_CHANNEL_M1, s);
                write_esc_pulse(LEDC_CHANNEL_M2, s);
                vTaskDelay(pdMS_TO_TICKS(20));
            }
            vTaskDelay(pdMS_TO_TICKS(500));
            break;

        case MOTOR_MODE_MANUAL:
        default:
            vTaskDelay(pdMS_TO_TICKS(100));
            break;
        }
    }
}

void bldc_init(void)
{
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_MODE,
        .timer_num        = LEDC_TIMER,
        .duty_resolution  = LEDC_DUTY_RES,
        .freq_hz          = LEDC_FREQUENCY,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    ledc_channel_config_t ch_m1 = {
        .speed_mode     = LEDC_MODE,
        .channel        = LEDC_CHANNEL_M1,
        .timer_sel      = LEDC_TIMER,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = MOTOR_PIN_1,
        .duty           = ESC_MIN_DUTY,
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch_m1));

    ledc_channel_config_t ch_m2 = {
        .speed_mode     = LEDC_MODE,
        .channel        = LEDC_CHANNEL_M2,
        .timer_sel      = LEDC_TIMER,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = MOTOR_PIN_2,
        .duty           = ESC_MIN_DUTY,
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch_m2));

    ESP_LOGI(TAG, "Arming ESCs with 1000us signal...");
    write_esc_pulse(LEDC_CHANNEL_M1, 0.0f);
    write_esc_pulse(LEDC_CHANNEL_M2, 0.0f);

    /* Dedicated motor pattern executor task */
    xTaskCreate(bldc_animation_task, "bldc_anim", 2048, NULL, 3, NULL);
}