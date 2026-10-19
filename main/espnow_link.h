#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

/* Brings up NVS, Wi-Fi (STA, fixed channel, power save off) and ESP-NOW,
 * and registers the broadcast peer used for discovery. */
esp_err_t espnow_link_init(void);

/* Transmit a raw frame. Unicast once the peer MAC is known, broadcast
 * before that. */
esp_err_t espnow_link_send(const uint8_t *buf, size_t len);

/* Called by the protocol layer the first time a valid frame from the peer
 * arrives; registers the unicast peer and stops broadcasting. */
void espnow_link_learn_peer(const uint8_t mac[6]);
bool espnow_link_peer_known(void);
void espnow_link_own_mac_str(char *out, size_t cap);
