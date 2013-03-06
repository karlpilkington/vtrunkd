/*
 * protocol.c
 *
 *  Created on: 05.03.2013
 *      Author: Kuznetsov Andrey
 */

#include "protocol.h"
#include "vtun.h"

int protocol_dead_send(char *buf, int fd, uint16_t dead_channel) {
    memcpy(buf + sizeof(uint16_t), &(htons(FRAME_DEAD_CHANNEL)), sizeof(uint16_t));
    uint16_t dead_channel_n = htons(dead_channel);
    memcpy(buf + sizeof(uint16_t), &dead_channel_n, sizeof(uint16_t));
    int len = proto_write(fd, buf, ((2 * sizeof(uint16_t)) | VTUN_BAD_FRAME));
    return len;
}

int protocol_lws_send(char *buf, int fd, uint32_t lws) {
    memcpy(buf, &(htons(FRAME_TIME_LAG)), sizeof(uint16_t));
    uint16_t lws_n = htonl(lws);
    memcpy(buf + sizeof(uint16_t), &lws_n, sizeof(uint32_t));
    int len = proto_write(fd, buf, ((sizeof(uint32_t) + sizeof(uint16_t)) | VTUN_BAD_FRAME));
    return len;
}

int protocol_request_frame(char *buf, int fd, uint32_t seq_num) {
    memcpy(buf, &(htons(FRAME_MODE_RXMIT)), sizeof(uint16_t));
    uint16_t seq_num_n = htonl(seq_num);
    memcpy(buf + sizeof(uint16_t), &seq_num_n, sizeof(uint32_t));
    int len = proto_write(fd, buf, ((sizeof(uint32_t) + sizeof(uint32_t)) | VTUN_BAD_FRAME));
    return len;
}

int protocol_time_lag_send(char *buf, int fd, uint32_t seq_num) {
    memcpy(buf, &(htons(FRAME_MODE_RXMIT)), sizeof(uint16_t));
    uint16_t lws_n = htonl(seq_num);
    memcpy(buf + sizeof(uint16_t), &lws_n, sizeof(uint32_t));
    int len = proto_write(fd, buf, ((sizeof(uint32_t) + sizeof(uint32_t)) | VTUN_BAD_FRAME));
    return len;
}

int protocol_echo_request(char *buf, int fd) {
    int len = proto_write(fd, buf, VTUN_ECHO_REQ);
    return len;
}

int protocol_echo_reply(char *buf, int fd) {
    int len = proto_write(fd, buf, VTUN_ECHO_REP);
    return len;
}

int protocol_just_started(char *buf, int fd, uint32_t seq_num) {
    uint32_t seq_counter_n = htonl(seq_num);
    *((unsigned short *) (buf + sizeof(unsigned long))) = htons(FRAME_JUST_STARTED);
    int len = proto_write(fd, buf, ((sizeof(uint32_t) + sizeof(uint32_t)) | VTUN_BAD_FRAME));
    return len;
}

