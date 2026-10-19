#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "packet.h"

#define NONCE_LEN        16          /* per-session freshness nonce */
#define SESSION_KEY_LEN  16          /* AES-128-GCM */
#define CONFIRM_LEN      32          /* HMAC-SHA256 */

/* Generate a random per-session nonce. Call once per handshake attempt.
 * The nonce carries no secrecy of its own - it only stops two sessions
 * between the same pair of nodes from ever deriving the same key. */
int  crypto_session_start(uint8_t out_nonce[NONCE_LEN]);

/* Combine our nonce with the peer's, run HKDF-SHA256 with the PSK as the
 * only secret input, and install the resulting session key into the GCM
 * context. NOTE: unlike the ECDH version, this key is fully determined by
 * the PSK (plus the public nonces), so it does NOT provide forward
 * secrecy - a leaked PSK compromises every session, past and future. */
int  crypto_session_finish(const uint8_t peer_nonce[NONCE_LEN]);

/* PSK-bound handshake confirmation. */
void crypto_make_confirm(uint8_t out[CONFIRM_LEN], uint8_t node_id);
bool crypto_check_confirm(const uint8_t got[CONFIRM_LEN], uint8_t peer_id);

bool crypto_ready(void);
void crypto_reset(void);

/* AEAD. Both operate in place on p->payload; the 14-byte header is AAD. */
int  crypto_seal(Packet *p);   /* 0 on success */
int  crypto_open(Packet *p);   /* 0 on success, PKT_AUTH_FAIL on bad tag */