#include <string.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "driver/gpio.h"      // <-- Added for GPIO control

#include "protocol.h"
#include "packet.h"
#include "crypto.h"
#include "espnow_link.h"
#include "console_io.h"
#include "link_config.h"
#include "bldc.h"
#include "lcd.h"
#define ACTION_PIN 2          // Must match the pin in app_main.c


static const char *TAG = "proto";

/* ------------------------------------------------------------------ */
/* events                                                             */
/* ------------------------------------------------------------------ */
enum { EV_FRAME = 1, EV_TEXT = 2 };

typedef struct {
    uint8_t  kind;
    uint8_t  mac[6];
    uint16_t len;
    uint8_t  data[PKT_FRAME_MAX];
} proto_ev_t;

static QueueHandle_t s_q;

/* ------------------------------------------------------------------ */
/* state                                                              */
/* ------------------------------------------------------------------ */
typedef enum {
    ST_NONCE_WAIT,     /* broadcasting our session nonce                */
    ST_KEY_CONFIRM,    /* key derived from PSK, exchanging confirms     */
    ST_DISCONNECTED,   /* have a session key, no logical connection yet */
    ST_WAIT_SYNACK,    /* initiator sent SYN                            */
    ST_WAIT_ACK,       /* responder sent SYN-ACK                        */
    ST_CONNECTED,
    ST_WAIT_DATA_ACK
} link_state_t;

static link_state_t s_state;
static int64_t      s_deadline_ms;

static uint32_t s_tx_seq;
static uint32_t s_win_top;          /* replay window: highest accepted seq */
static uint64_t s_win_mask;         /* bit0 == s_win_top                   */
static uint16_t s_msg_id;
static bool     s_have_delivered;
static uint16_t s_last_delivered;

static uint8_t  s_my_nonce[NONCE_LEN];
static uint8_t  s_confirm_retries;
static uint8_t  s_ack_retries;
static int64_t  s_last_good_rx_ms;

/* one message in flight (stop-and-wait) */
static uint8_t  s_pending[PKT_PAYLOAD_MAX];
static uint16_t s_pending_len;
static uint16_t s_pending_msg;

/* small outbox so console/telemetry never block on the radio */
typedef struct { uint16_t len; uint8_t data[PKT_PAYLOAD_MAX]; } msg_t;
static msg_t   s_outbox[OUTBOX_DEPTH];
static uint8_t s_ob_head, s_ob_tail, s_ob_count;

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

static void set_state(link_state_t st, uint32_t timeout_ms)
{
    s_state       = st;
    s_deadline_ms = now_ms() + timeout_ms;
}

/* ------------------------------------------------------------------ */
/* transmit helpers                                                    */
/* ------------------------------------------------------------------ */
static void send_plain(uint8_t type, const uint8_t *payload, uint16_t len)
{
    Packet p;
    uint8_t frame[PKT_FRAME_MAX];

    packet_init(&p, type, NODE_ID, PEER_ID);
    p.flags  = PKT_FLAG_PLAINTEXT;
    p.seq    = 0;
    p.length = len;
    if (len) memcpy(p.payload, payload, len);

    size_t n = packet_serialize(&p, frame, sizeof(frame));
    if (n) espnow_link_send(frame, n);
}

static bool send_secure(uint8_t type, uint16_t msg_id,
                        const uint8_t *payload, uint16_t len)
{
    Packet p;
    uint8_t frame[PKT_FRAME_MAX];

    if (!crypto_ready() || len > PKT_PAYLOAD_MAX) return false;

    packet_init(&p, type, NODE_ID, PEER_ID);
    p.seq    = ++s_tx_seq;          /* fresh nonce for every transmission */
    p.msg_id = msg_id;
    p.length = len;
    if (len) memcpy(p.payload, payload, len);

    if (crypto_seal(&p) != 0) return false;

    size_t n = packet_serialize(&p, frame, sizeof(frame));
    if (!n) return false;
    return espnow_link_send(frame, n) == ESP_OK;
}

static void send_ctrl(uint8_t type, uint16_t msg_id)
{
    send_secure(type, msg_id, NULL, 0);
}

/* ------------------------------------------------------------------ */
/* handshake                                                           */
/* ------------------------------------------------------------------ */
static void start_handshake(const char *why)
{
    ESP_LOGW(TAG, "handshake restart (%s)", why);

    crypto_reset();
    s_tx_seq      = 0;
    s_win_top     = 0;
    s_win_mask    = 0;
    s_msg_id      = 0;
    s_have_delivered = false;
    s_pending_len = 0;
    s_ack_retries = 0;
    s_confirm_retries = 0;

    crypto_session_start(s_my_nonce);   /* always succeeds: just fills random bytes */
    send_plain(PKT_HELLO, s_my_nonce, NONCE_LEN);
    set_state(ST_NONCE_WAIT, DH_RETRY_MS);
}

static void finish_handshake(void)
{
    uint8_t cfm[CONFIRM_LEN];
    crypto_make_confirm(cfm, NODE_ID);
    send_plain(PKT_KEY_CONFIRM, cfm, CONFIRM_LEN);
    set_state(ST_KEY_CONFIRM, CONFIRM_TIMEOUT_MS);
}

/* ------------------------------------------------------------------ */
/* replay window (64 packets wide, tolerates ESP-NOW reordering)       */
/* ------------------------------------------------------------------ */
static bool replay_ok(uint32_t seq)
{
    if (seq == 0) return false;

    if (seq > s_win_top) {
        uint32_t shift = seq - s_win_top;
        s_win_mask = (shift >= 64) ? 0 : (s_win_mask << shift);
        s_win_mask |= 1ULL;
        s_win_top   = seq;
        return true;
    }

    uint32_t d = s_win_top - seq;
    if (d >= 64) return false;              /* too old */
    uint64_t bit = 1ULL << d;
    if (s_win_mask & bit) return false;     /* already seen */
    s_win_mask |= bit;
    return true;
}

/* ------------------------------------------------------------------ */
/* outbox                                                              */
/* ------------------------------------------------------------------ */
static bool outbox_push(const uint8_t *d, uint16_t len)
{
    if (s_ob_count >= OUTBOX_DEPTH) return false;
    msg_t *m = &s_outbox[s_ob_head];
    m->len = len > PKT_PAYLOAD_MAX ? PKT_PAYLOAD_MAX : len;
    memcpy(m->data, d, m->len);
    s_ob_head = (uint8_t)((s_ob_head + 1) % OUTBOX_DEPTH);
    s_ob_count++;
    return true;
}

static void pump_outbox(void)
{
    if (s_state != ST_CONNECTED || s_ob_count == 0) return;

    msg_t *m = &s_outbox[s_ob_tail];
    s_ob_tail = (uint8_t)((s_ob_tail + 1) % OUTBOX_DEPTH);
    s_ob_count--;

    memcpy(s_pending, m->data, m->len);
    s_pending_len = m->len;
    s_pending_msg = ++s_msg_id;
    s_ack_retries = 0;

    if (send_secure(PKT_DATA, s_pending_msg, s_pending, s_pending_len))
        set_state(ST_WAIT_DATA_ACK, ACK_TIMEOUT_MS);
}

/* ------------------------------------------------------------------ */
/* frame handling                                                      */
/* ------------------------------------------------------------------ */
static void on_plain_frame(Packet *p, const uint8_t mac[6])
{
    if (p->type == PKT_HELLO) {
        if (p->length != NONCE_LEN) return;
        espnow_link_learn_peer(mac);

        if (s_state == ST_NONCE_WAIT) {
            if (crypto_session_finish(p->payload) != 0) {
                ESP_LOGE(TAG, "session key derivation failed");
                return;
            }
            finish_handshake();
        } else if (s_state == ST_KEY_CONFIRM) {
            /* peer missed our hello, echo both */
            send_plain(PKT_HELLO, s_my_nonce, NONCE_LEN);
            uint8_t cfm[CONFIRM_LEN];
            crypto_make_confirm(cfm, NODE_ID);
            send_plain(PKT_KEY_CONFIRM, cfm, CONFIRM_LEN);
        } else if (now_ms() - s_last_good_rx_ms > 3000) {
            /* Peer rebooted with a fresh nonce. Only honour this when the
             * link has already gone quiet, otherwise anyone could force a
             * rekey by spraying HELLO frames. */
            start_handshake("peer restarted");
        }
        return;
    }

    if (p->type == PKT_KEY_CONFIRM && s_state == ST_KEY_CONFIRM) {
        if (p->length != CONFIRM_LEN) return;
        if (!crypto_check_confirm(p->payload, PEER_ID)) {
            ESP_LOGE(TAG, "confirm mismatch - wrong PSK or MITM");
            start_handshake("bad confirm");
            return;
        }
        espnow_link_learn_peer(mac);
        ESP_LOGW(TAG, "session established");
        s_last_good_rx_ms = now_ms();
#if NODE_INITIATOR
        send_ctrl(PKT_SYN, 0);
        set_state(ST_WAIT_SYNACK, HANDSHAKE_TIMEOUT_MS);
#else
        set_state(ST_DISCONNECTED, LISTEN_TIMEOUT_MS);
#endif
    }
}

static void deliver(Packet *p)
{
    /* Duplicate suppression by msg_id */
    if (s_have_delivered && p->msg_id == s_last_delivered)
        return;
    s_have_delivered = true;
    s_last_delivered = p->msg_id;

#if NODE_INITIATOR
    /* Node A (Laptop): Pass the decrypted payload to the Python console */
    console_write_line(p->payload, p->length);
#else
    /* Node B (Autonomous Responder): Parse commands and trigger hardware */
    
    // 1. Safely extract the payload into a null-terminated string
    char cmd[PKT_PAYLOAD_MAX + 1];
    uint16_t len = p->length < PKT_PAYLOAD_MAX ? p->length : PKT_PAYLOAD_MAX;
    memcpy(cmd, p->payload, len);
    cmd[len] = '\0';

    // 2. Strip trailing newlines/carriage returns sent from the Python frontend
    for (int i = len - 1; i >= 0; i--) {
        if (cmd[i] == '\r' || cmd[i] == '\n') cmd[i] = '\0';
        else break;
    }

    char reply[64];

    // 3. Execute custom functions based on the ASCII command
    if (strcmp(cmd, "LED_ON") == 0) {
        gpio_set_level(ACTION_PIN, 1);
        snprintf(reply, sizeof(reply), "ACK: Action 'LED_ON' executed. Feature is ON.");
    }
    else if (strcmp(cmd, "LED_OFF") == 0) {
        gpio_set_level(ACTION_PIN, 0);
        snprintf(reply, sizeof(reply), "ACK: Action 'LED_OFF' executed. Feature is OFF.");
    }
    else if (strcmp(cmd, "MOTOR:STOP") == 0 || strcmp(cmd, "MOTOR:OFF") == 0) {
        bldc_emergency_stop();
        snprintf(reply, sizeof(reply), "ACK: Motors stopped");
    }
    else if (strcmp(cmd, "MOTOR:SPIN") == 0 || strcmp(cmd, "MOTOR:ON") == 0) {
        /* Default aesthetic cruise speed (20%) */
        bldc_set_speed(0, 20.0f);
        snprintf(reply, sizeof(reply), "ACK: Motors running @ 20%%");
    }
    else if (strncmp(cmd, "MOTOR:SPEED ", 12) == 0) {
        float speed = (float)atof(cmd + 12);
        bldc_set_speed(0, speed);
        snprintf(reply, sizeof(reply), "ACK: Motors set to speed %.1f%%", speed);
    }
    else if (strcmp(cmd, "MOTOR:RAMP") == 0) {
        bldc_set_pattern(MOTOR_MODE_RAMP_DEMO);
        snprintf(reply, sizeof(reply), "ACK: Pattern started: RAMP");
    }
    else if (strcmp(cmd, "MOTOR:ALT") == 0) {
        bldc_set_pattern(MOTOR_MODE_ALTERNATING_DEMO);
        snprintf(reply, sizeof(reply), "ACK: Pattern started: ALTERNATING");
    }
    else if (strcmp(cmd, "MOTOR:PULSE") == 0) {
        bldc_set_pattern(MOTOR_MODE_PULSE_DEMO);
        snprintf(reply, sizeof(reply), "ACK: Pattern started: PULSE");
    }
    else if (strncmp(cmd, "LCD ", 4) == 0 || strncmp(cmd, "LCD:", 4) == 0) {
        char *msg = cmd + 4;
        
        /* Skip leading spaces if the user typed "LCD:  Hello" */
        while (*msg == ' ') msg++; 
        
        lcd_clear();
        lcd_write_str(msg);
        
        ESP_LOGI(TAG, "LCD Updated: %s", msg);
        snprintf(reply, sizeof(reply), "ACK: Display updated");
    }
    else {
        // Handle unknown commands
        snprintf(reply, sizeof(reply), "ERR: Unknown command '%.30s'", cmd);
    }


    // 4. Inject the status reply directly into the outbound event queue
    protocol_post_text((const uint8_t *)reply, strlen(reply));
#endif
}

static void on_secure_frame(Packet *p)
{
    switch (p->type) {

    case PKT_SYN:
        send_ctrl(PKT_SYN_ACK, 0);
#if NODE_INITIATOR
        set_state(ST_WAIT_SYNACK, HANDSHAKE_TIMEOUT_MS);
#else
        set_state(ST_WAIT_ACK, HANDSHAKE_TIMEOUT_MS);
#endif
        break;

    case PKT_SYN_ACK:
#if NODE_INITIATOR
        send_ctrl(PKT_ACK, 0);
        ESP_LOGW(TAG, "connected");
        set_state(ST_CONNECTED, IDLE_POLL_MS);
#endif
        break;

    case PKT_ACK:
#if !NODE_INITIATOR
        ESP_LOGW(TAG, "connected");
        set_state(ST_CONNECTED, IDLE_POLL_MS);
#endif
        break;

    case PKT_DATA:
        if (s_state == ST_CONNECTED || s_state == ST_WAIT_DATA_ACK) {
            deliver(p);
            send_ctrl(PKT_DATA_ACK, p->msg_id);
        }
        break;

    case PKT_DATA_ACK:
        if (s_state == ST_WAIT_DATA_ACK && p->msg_id == s_pending_msg) {
            s_pending_len = 0;
            s_ack_retries = 0;
            set_state(ST_CONNECTED, IDLE_POLL_MS);
        }
        break;

    default:
        break;
    }
}

static void on_frame(proto_ev_t *ev)
{
    Packet p;
    if (packet_parse(ev->data, ev->len, &p) != PKT_OK) return;
    if (p.sender_id != PEER_ID || p.receiver_id != NODE_ID) return;

    if (p.flags & PKT_FLAG_PLAINTEXT) {
        on_plain_frame(&p, ev->mac);
        return;
    }

    if (!crypto_ready()) return;
    if (crypto_open(&p) != 0) {
        ESP_LOGD(TAG, "auth fail (type %02x seq %lu)", p.type, (unsigned long)p.seq);
        return;
    }
    if (!replay_ok(p.seq)) {
        ESP_LOGD(TAG, "replay/stale seq %lu", (unsigned long)p.seq);
        return;
    }

    espnow_link_learn_peer(ev->mac);
    s_last_good_rx_ms = now_ms();
    on_secure_frame(&p);
}

/* ------------------------------------------------------------------ */
/* timeouts                                                            */
/* ------------------------------------------------------------------ */
static void on_timeout(void)
{
    switch (s_state) {

    case ST_NONCE_WAIT:
        send_plain(PKT_HELLO, s_my_nonce, NONCE_LEN);
        set_state(ST_NONCE_WAIT, DH_RETRY_MS);
        break;

    case ST_KEY_CONFIRM:
        if (++s_confirm_retries > 6) { start_handshake("confirm timeout"); break; }
        {
            uint8_t cfm[CONFIRM_LEN];
            crypto_make_confirm(cfm, NODE_ID);
            send_plain(PKT_KEY_CONFIRM, cfm, CONFIRM_LEN);
        }
        set_state(ST_KEY_CONFIRM, CONFIRM_TIMEOUT_MS);
        break;

    case ST_DISCONNECTED:
#if NODE_INITIATOR
        send_ctrl(PKT_SYN, 0);
        set_state(ST_WAIT_SYNACK, HANDSHAKE_TIMEOUT_MS);
#else
        set_state(ST_DISCONNECTED, LISTEN_TIMEOUT_MS);
#endif
        break;

    case ST_WAIT_SYNACK:
    case ST_WAIT_ACK:
        set_state(ST_DISCONNECTED, 1);
        break;

    case ST_CONNECTED:
        set_state(ST_CONNECTED, IDLE_POLL_MS);
        break;

    case ST_WAIT_DATA_ACK:
        if (++s_ack_retries >= MAX_ACK_RETRIES) {
            ESP_LOGW(TAG, "message %u abandoned", s_pending_msg);
            s_pending_len = 0;
            set_state(ST_DISCONNECTED, 1);
        } else {
            /* Same msg_id, NEW seq. Re-sending the original frame verbatim
             * would be dropped by the peer's replay window and the ACK would
             * never come - that deadlock exists in the UART version. */
            send_secure(PKT_DATA, s_pending_msg, s_pending, s_pending_len);
            set_state(ST_WAIT_DATA_ACK, ACK_TIMEOUT_MS);
        }
        break;
    }

    /* AES-GCM nonce is (sender_id || seq); it may never repeat under one
     * key, so rekey long before the counter could wrap. */
    if (s_tx_seq > 0xF0000000UL)
        start_handshake("nonce budget exhausted");
}

/* ------------------------------------------------------------------ */
/* task                                                                */
/* ------------------------------------------------------------------ */
static void proto_task(void *arg)
{
    (void)arg;
    proto_ev_t ev;

    s_last_good_rx_ms = now_ms();
    start_handshake("boot");

    for (;;) {
        int64_t remain = s_deadline_ms - now_ms();
        if (remain < 1) remain = 1;

        if (xQueueReceive(s_q, &ev, pdMS_TO_TICKS((uint32_t)remain)) == pdTRUE) {
            if (ev.kind == EV_FRAME)      on_frame(&ev);
            else if (ev.kind == EV_TEXT)  outbox_push(ev.data, ev.len);
        } else {
            on_timeout();
        }
        pump_outbox();
    }
}

void protocol_start(void)
{
    s_q = xQueueCreate(EVENT_QUEUE_DEPTH, sizeof(proto_ev_t));
    configASSERT(s_q);
    xTaskCreatePinnedToCore(proto_task, "proto", 8192, NULL,
                            PROTO_TASK_PRIO, NULL, CORE_APP);
}

void protocol_post_frame(const uint8_t mac[6], const uint8_t *data, size_t len)
{
    if (!s_q || len == 0 || len > PKT_FRAME_MAX) return;

    proto_ev_t ev;
    ev.kind = EV_FRAME;
    memcpy(ev.mac, mac, 6);
    ev.len = (uint16_t)len;
    memcpy(ev.data, data, len);

    /* Never block the Wi-Fi task: drop instead, the retry logic copes. */
    (void)xQueueSend(s_q, &ev, 0);
}

bool protocol_post_text(const uint8_t *text, size_t len)
{
    if (!s_q || len == 0) return false;
    if (len > PKT_PAYLOAD_MAX) len = PKT_PAYLOAD_MAX;

    proto_ev_t ev;
    ev.kind = EV_TEXT;
    memset(ev.mac, 0, 6);
    ev.len = (uint16_t)len;
    memcpy(ev.data, text, len);
    return xQueueSend(s_q, &ev, pdMS_TO_TICKS(50)) == pdTRUE;
}

bool protocol_is_connected(void)
{
    return s_state == ST_CONNECTED || s_state == ST_WAIT_DATA_ACK;
}