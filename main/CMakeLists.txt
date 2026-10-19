idf_component_register(
    SRCS "app_main.c"
         "packet.c"
         "crypto.c"
         "espnow_link.c"
         "protocol.c"
         "console_io.c"
         "telemetry.c"
         "bldc.c"
         "lcd.c"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi nvs_flash mbedtls driver esp_timer esp_netif esp_event
)
