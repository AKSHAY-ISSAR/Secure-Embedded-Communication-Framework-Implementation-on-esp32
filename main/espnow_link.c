#include <string.h>
#include <stdio.h>
#include "espnow_link.h"
#include "link_config.h"
#include "protocol.h"

#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_mac.h"
#include "esp_log.h"

static const char *TAG = "link";

static const uint8_t BROADCAST_MAC[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
static uint8_t s_peer_mac[6];
static bool    s_peer_known;

/* Runs in the Wi-Fi task context on core 0. Must stay short: copy the
 * bytes into the protocol queue and return. No crypto here. */
static void recv_cb(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    if (len <= 0) return;
    protocol_post_frame(info->src_addr, data, (size_t)len);
}

static void send_cb(const uint8_t *mac, esp_now_send_status_t status)
{
    if (status != ESP_NOW_SEND_SUCCESS)
        ESP_LOGD(TAG, "tx to %02x:%02x:%02x:%02x:%02x:%02x failed",
                 mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
}

static esp_err_t add_peer(const uint8_t mac[6])
{
    if (esp_now_is_peer_exist(mac)) return ESP_OK;

    esp_now_peer_info_t peer = {0};
    memcpy(peer.peer_addr, mac, 6);
    peer.channel = CONFIG_LINK_WIFI_CHANNEL;
    peer.ifidx   = WIFI_IF_STA;
    peer.encrypt = false;          /* our AES-GCM layer does the work */
    return esp_now_add_peer(&peer);
}

esp_err_t espnow_link_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(esp_netif_init());
    err = esp_event_loop_create_default();
if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    ESP_ERROR_CHECK(err);
}

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    /* Power save must be off or the radio sleeps between ESP-NOW frames. */
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

#if CONFIG_LINK_ENABLE_LONG_RANGE
    ESP_ERROR_CHECK(esp_wifi_set_protocol(WIFI_IF_STA,
        WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N | WIFI_PROTOCOL_LR));
#endif

    ESP_ERROR_CHECK(esp_wifi_set_channel(CONFIG_LINK_WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE));

    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_recv_cb(recv_cb));
    ESP_ERROR_CHECK(esp_now_register_send_cb(send_cb));
    ESP_ERROR_CHECK(add_peer(BROADCAST_MAC));

    char mac[18];
    espnow_link_own_mac_str(mac, sizeof(mac));
    ESP_LOGW(TAG, "node " NODE_NAME " up, mac %s, channel %d",
             mac, CONFIG_LINK_WIFI_CHANNEL);
    return ESP_OK;
}

esp_err_t espnow_link_send(const uint8_t *buf, size_t len)
{
    if (len > ESP_NOW_MAX_DATA_LEN) return ESP_ERR_INVALID_SIZE;
    return esp_now_send(s_peer_known ? s_peer_mac : BROADCAST_MAC, buf, len);
}

void espnow_link_learn_peer(const uint8_t mac[6])
{
    if (s_peer_known && memcmp(s_peer_mac, mac, 6) == 0) return;

    memcpy(s_peer_mac, mac, 6);
    if (add_peer(mac) == ESP_OK) {
        s_peer_known = true;
        ESP_LOGW(TAG, "peer locked: %02x:%02x:%02x:%02x:%02x:%02x",
                 mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    }
}

bool espnow_link_peer_known(void) { return s_peer_known; }

void espnow_link_own_mac_str(char *out, size_t cap)
{
    uint8_t m[6] = {0};
    esp_wifi_get_mac(WIFI_IF_STA, m);
    snprintf(out, cap, "%02x:%02x:%02x:%02x:%02x:%02x",
             m[0], m[1], m[2], m[3], m[4], m[5]);
}