/*
 * protocol.h
 *
 *  Created on: 05.03.2013
 *      Author: andrey
 */

#ifndef PROTOCOL_H_
#define PROTOCOL_H_

#define VTUN_CONN_CLOSE 0x1000
#define VTUN_ECHO_REQ   0x2000
#define VTUN_ECHO_REP   0x4000
#define VTUN_BAD_FRAME  0x8000

#define FRAME_MODE_NORM 0
#define FRAME_MODE_RXMIT 1
#define FRAME_JUST_STARTED 2
#define FRAME_PRIO_PORT_NOTIFY 3
#define FRAME_LAST_WRITTEN_SEQ 4
#define FRAME_TIME_LAG 5 // time lag from favorite CONN - Issue #11
#define FRAME_DEAD_CHANNEL 6

int protocol_dead_send(char *buf, int fd, uint16_t dead_channel);
int protocol_lws_send(char *buf, int fd, uint16_t lws);

#endif /* PROTOCOL_H_ */
