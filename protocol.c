/*
 * protocol.c
 *
 *  Created on: 05.03.2013
 *      Author: Kuznetsov Andrey
 */

#include "protocol.h"
#include "vtun.h"

int protocol_dead_send(char *buf, int fd, uint16_t dead_channel){
    memcpy(buf+sizeof(uint16_t), &(htons(FRAME_DEAD_CHANNEL)), sizeof(uint16_t));
    uint16_t dead_channel_h = htons(dead_channel);
    memcpy(buf + sizeof(uint16_t), &dead_channel_h, sizeof(uint16_t));
    int len = proto_write(fd, buf, ((2 * sizeof(uint16_t)) | VTUN_BAD_FRAME));
    return len;
}

int protocol_lws_send(char *buf, int fd, uint32_t lws) {
    memcpy(buf, &(htons(FRAME_TIME_LAG)), sizeof(uint16_t));
    uint16_t lws_h = htonl(lws);
    memcpy(buf + sizeof(uint16_t), &lws_h, sizeof(uint32_t));
    int len = proto_write(fd, buf, ((sizeof(uint32_t) + sizeof(uint16_t)) | VTUN_BAD_FRAME));
    return len;
}

