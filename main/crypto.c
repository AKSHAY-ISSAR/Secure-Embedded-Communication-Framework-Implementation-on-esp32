#include <string.h>
#include "crypto.h"
#include "link_config.h"
#include "esp_random.h"
#include "esp_log.h"

#include "mbedtls/gcm.h"
#include "mbedtls/hkdf.h"
#include "mbedtls/md.h"
#include "mbedtls/platform_util.h"

static const char *TAG = "crypto";

static mbedtls_gcm_context s_gcm;

static uint8_t s_my_nonce[NONCE_LEN];
static uint8_t s_nonce_low[NONCE_LEN];   /* lexicographically smaller nonce */
static uint8_t s_nonce_high[NONCE_LEN];
static uint8_t s_confirm_key[32];
static bool    s_ready;
static bool    s_inited;

static void build_iv(uint8_t iv[12], uint8_t sender_id, uint32_t seq)
{
    memset(iv, 0, 7);
    iv[7]  = sender_id;
    iv[8]  = (uint8_t)(seq >> 24);
    iv[9]  = (uint8_t)(seq >> 16);
    iv[10] = (uint8_t)(seq >> 8);
    iv[11] = (uint8_t)(seq);
}

static void ensure_init(void)
{
    if (s_inited) return;
    mbedtls_gcm_init(&s_gcm);
    s_inited = true;
}

int crypto_session_start(uint8_t out_nonce[NONCE_LEN])
{
    ensure_init();
    s_ready = false;

    esp_fill_random(s_my_nonce, NONCE_LEN);
    memcpy(out_nonce, s_my_nonce, NONCE_LEN);
    return 0;
}

int crypto_session_finish(const uint8_t peer_nonce[NONCE_LEN])
{
    ensure_init();

    uint8_t okm[SESSION_KEY_LEN + 32];
    int rc = -1;

    /* A peer that echoes our own nonce back would force a session context
     * we already know in advance; refuse it rather than derive from it. */
    if (memcmp(s_my_nonce, peer_nonce, NONCE_LEN) == 0) {
        ESP_LOGE(TAG, "peer echoed our nonce");
        return -1;
    }

    /* Order the two nonces so both sides derive an identical context
     * regardless of who is A/B or who sent first. */
    if (memcmp(s_my_nonce, peer_nonce, NONCE_LEN) < 0) {
        memcpy(s_nonce_low,  s_my_nonce,  NONCE_LEN);
        memcpy(s_nonce_high, peer_nonce,  NONCE_LEN);
    } else {
        memcpy(s_nonce_low,  peer_nonce,  NONCE_LEN);
        memcpy(s_nonce_high, s_my_nonce,  NONCE_LEN);
    }

    {
        /* salt = nonce_low || nonce_high (public, gives per-session
         *        freshness only)
         * IKM  = PSK (the one secret input - this is what the whole
         *        session key's security rests on now that there's no
         *        ECDH shared secret)
         * info = fixed label, for domain separation from other uses
         *        of the same PSK */
        uint8_t salt[2 * NONCE_LEN];
        memcpy(salt, s_nonce_low, NONCE_LEN);
        memcpy(salt + NONCE_LEN, s_nonce_high, NONCE_LEN);

        const mbedtls_md_info_t *md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
        const char *psk   = CONFIG_LINK_PSK;
        const char *label = "esplink-psk-v1";

        if (mbedtls_hkdf(md,
                         salt, sizeof(salt),
                         (const unsigned char *)psk, strlen(psk),
                         (const unsigned char *)label, strlen(label),
                         okm, sizeof(okm)) != 0)
            goto done;
    }

    mbedtls_gcm_free(&s_gcm);
    mbedtls_gcm_init(&s_gcm);
    if (mbedtls_gcm_setkey(&s_gcm, MBEDTLS_CIPHER_ID_AES, okm, SESSION_KEY_LEN * 8) != 0)
        goto done;

    memcpy(s_confirm_key, okm + SESSION_KEY_LEN, 32);
    s_ready = true;
    rc = 0;

done:
    mbedtls_platform_zeroize(okm, sizeof(okm));
    return rc;
}

static void confirm_for(uint8_t node_id, uint8_t out[CONFIRM_LEN])
{
    uint8_t msg[4 + 2 * NONCE_LEN];
    memcpy(msg, "cfm", 3);
    msg[3] = node_id;
    memcpy(msg + 4, s_nonce_low, NONCE_LEN);
    memcpy(msg + 4 + NONCE_LEN, s_nonce_high, NONCE_LEN);

    const mbedtls_md_info_t *md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    mbedtls_md_hmac(md, s_confirm_key, sizeof(s_confirm_key),
                    msg, sizeof(msg), out);
}

void crypto_make_confirm(uint8_t out[CONFIRM_LEN], uint8_t node_id)
{
    confirm_for(node_id, out);
}

bool crypto_check_confirm(const uint8_t got[CONFIRM_LEN], uint8_t peer_id)
{
    uint8_t expect[CONFIRM_LEN];
    confirm_for(peer_id, expect);

    uint8_t diff = 0;
    for (int i = 0; i < CONFIRM_LEN; i++)
        diff |= (uint8_t)(expect[i] ^ got[i]);
    return diff == 0;
}

bool crypto_ready(void) { return s_ready; }

void crypto_reset(void)
{
    s_ready = false;
    if (s_inited) {
        mbedtls_gcm_free(&s_gcm);
        mbedtls_gcm_init(&s_gcm);
    }
    mbedtls_platform_zeroize(s_confirm_key, sizeof(s_confirm_key));
}

int crypto_seal(Packet *p)
{
    if (!s_ready) return -1;
    if (p->length > PKT_PAYLOAD_MAX) return -1;

    uint8_t hdr[PKT_HDR_LEN];
    uint8_t iv[12];
    uint8_t ct[PKT_PAYLOAD_MAX];

    packet_write_header(p, hdr);
    build_iv(iv, p->sender_id, p->seq);

    int rc = mbedtls_gcm_crypt_and_tag(&s_gcm, MBEDTLS_GCM_ENCRYPT,
                                       p->length, iv, sizeof(iv),
                                       hdr, PKT_HDR_LEN,
                                       p->payload, ct,
                                       PKT_TAG_LEN, p->tag);
    if (rc) return rc;

    memcpy(p->payload, ct, p->length);
    return 0;
}

int crypto_open(Packet *p)
{
    if (!s_ready) return PKT_AUTH_FAIL;
    if (p->length > PKT_PAYLOAD_MAX) return PKT_BAD_FRAME;

    uint8_t hdr[PKT_HDR_LEN];
    uint8_t iv[12];
    uint8_t pt[PKT_PAYLOAD_MAX];

    packet_write_header(p, hdr);
    build_iv(iv, p->sender_id, p->seq);

    int rc = mbedtls_gcm_auth_decrypt(&s_gcm, p->length,
                                      iv, sizeof(iv),
                                      hdr, PKT_HDR_LEN,
                                      p->tag, PKT_TAG_LEN,
                                      p->payload, pt);
    if (rc) return PKT_AUTH_FAIL;

    memcpy(p->payload, pt, p->length);
    return 0;
}