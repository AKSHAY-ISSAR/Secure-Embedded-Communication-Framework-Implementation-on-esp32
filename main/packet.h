#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Wire format (little differs from the UART version: no start/stop byte and
 * no resync scan, because ESP-NOW already delivers whole, CRC-checked frames)
 *
 *   off  size  field
 *    0    1    magic
 *    1    1    version
 *    2    1    type
 *    3    1    sender_id
 *    4    1    receiver_id
 *    5    1    flags        bit0 = frame is plaintext (handshake only)
 *    6    4    seq          big endian, AEAD nonce counter
 *   10    2    msg_id       big endian, application-level message id
 *   12    2    length       big endian, ciphertext == plaintext length
 *   14    n    payload
 *   14+n  16   GCM tag      (all zero when flags bit0 set)
 *
 * The whole 14-byte header is the AEAD associated data, so type, ids, seq
 * and msg_id are all authenticated even though they travel in the clear.
 */

#define PKT_MAGIC        0xAB
#define PKT_VERSION      0x02

#define PKT_HDR_LEN      14
#define PKT_TAG_LEN      16
#define PKT_PAYLOAD_MAX  192                 /* ESP_NOW_MAX_DATA_LEN is 250 */
#define PKT_FRAME_MAX    (PKT_HDR_LEN + PKT_PAYLOAD_MAX + PKT_TAG_LEN)

#define PKT_FLAG_PLAINTEXT 0x01

#define PKT_SYN          0x01
#define PKT_SYN_ACK      0x02
#define PKT_ACK          0x03
#define PKT_DATA         0x04
#define PKT_DATA_ACK     0x05
#define PKT_HELLO        0x06   /* carries a random session nonce, PSK-only handshake */
#define PKT_KEY_CONFIRM  0x07

#define PKT_OK            0
#define PKT_BAD_FRAME    -2
#define PKT_WRONG_ID     -3
#define PKT_REPLAY       -4
#define PKT_AUTH_FAIL    -5

typedef struct {
    uint8_t  magic;
    uint8_t  version;
    uint8_t  type;
    uint8_t  sender_id;
    uint8_t  receiver_id;
    uint8_t  flags;
    uint32_t seq;
    uint16_t msg_id;
    uint16_t length;
    uint8_t  payload[PKT_PAYLOAD_MAX];
    uint8_t  tag[PKT_TAG_LEN];
} Packet;

void   packet_init(Packet *p, uint8_t type, uint8_t sender, uint8_t receiver);
void   packet_write_header(const Packet *p, uint8_t hdr[PKT_HDR_LEN]);
size_t packet_serialize(const Packet *p, uint8_t *out, size_t cap);
int    packet_parse(const uint8_t *buf, size_t len, Packet *p);