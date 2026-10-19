#include <string.h>
#include "packet.h"

void packet_init(Packet *p, uint8_t type, uint8_t sender, uint8_t receiver)
{
    memset(p, 0, sizeof(*p));
    p->magic       = PKT_MAGIC;
    p->version     = PKT_VERSION;
    p->type        = type;
    p->sender_id   = sender;
    p->receiver_id = receiver;
}

void packet_write_header(const Packet *p, uint8_t hdr[PKT_HDR_LEN])
{
    hdr[0]  = p->magic;
    hdr[1]  = p->version;
    hdr[2]  = p->type;
    hdr[3]  = p->sender_id;
    hdr[4]  = p->receiver_id;
    hdr[5]  = p->flags;
    hdr[6]  = (uint8_t)(p->seq >> 24);
    hdr[7]  = (uint8_t)(p->seq >> 16);
    hdr[8]  = (uint8_t)(p->seq >> 8);
    hdr[9]  = (uint8_t)(p->seq);
    hdr[10] = (uint8_t)(p->msg_id >> 8);
    hdr[11] = (uint8_t)(p->msg_id);
    hdr[12] = (uint8_t)(p->length >> 8);
    hdr[13] = (uint8_t)(p->length);
}

size_t packet_serialize(const Packet *p, uint8_t *out, size_t cap)
{
    size_t need = PKT_HDR_LEN + p->length + PKT_TAG_LEN;
    if (p->length > PKT_PAYLOAD_MAX || cap < need)
        return 0;

    packet_write_header(p, out);
    memcpy(out + PKT_HDR_LEN, p->payload, p->length);
    memcpy(out + PKT_HDR_LEN + p->length, p->tag, PKT_TAG_LEN);
    return need;
}

int packet_parse(const uint8_t *buf, size_t len, Packet *p)
{
    if (len < PKT_HDR_LEN + PKT_TAG_LEN)
        return PKT_BAD_FRAME;

    memset(p, 0, sizeof(*p));
    p->magic       = buf[0];
    p->version     = buf[1];
    p->type        = buf[2];
    p->sender_id   = buf[3];
    p->receiver_id = buf[4];
    p->flags       = buf[5];
    p->seq         = ((uint32_t)buf[6] << 24) | ((uint32_t)buf[7] << 16) |
                     ((uint32_t)buf[8] << 8)  |  (uint32_t)buf[9];
    p->msg_id      = (uint16_t)((buf[10] << 8) | buf[11]);
    p->length      = (uint16_t)((buf[12] << 8) | buf[13]);

    if (p->magic != PKT_MAGIC || p->version != PKT_VERSION)
        return PKT_BAD_FRAME;
    if (p->length > PKT_PAYLOAD_MAX)
        return PKT_BAD_FRAME;
    /* the declared length must account for exactly what arrived */
    if (len != (size_t)PKT_HDR_LEN + p->length + PKT_TAG_LEN)
        return PKT_BAD_FRAME;

    memcpy(p->payload, buf + PKT_HDR_LEN, p->length);
    memcpy(p->tag, buf + PKT_HDR_LEN + p->length, PKT_TAG_LEN);
    return PKT_OK;
}
