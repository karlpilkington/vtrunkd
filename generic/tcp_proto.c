/*  
   vtrunkd - Virtual Tunnel Trunking over TCP/IP network. 

   Copyright (C) 2011  Andrew Gryaznov <realgrandrew@gmail.com>,
   Andrey Kuznetsov <andreykyz@gmail.com>

   Vtrunkd has been derived from VTUN package by Maxim Krasnyansky. 
   vtun Copyright (C) 1998-2000  Maxim Krasnyansky <max_mk@yahoo.com>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
 */

/*
 * tcp_proto.c,v 1.4.2.3.2.1 2006/11/16 04:04:35 mtbishop Exp
 */ 

#include "config.h"

#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <errno.h>

#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif

#ifdef HAVE_NETINET_IN_SYSTM_H
#include <netinet/in_systm.h>
#endif

#ifdef HAVE_NETINET_IP_H
#include <netinet/ip.h>
#endif

#ifdef HAVE_NETINET_TCP_H
#include <netinet/tcp.h>
#endif

#include "vtun.h"
#include "lib.h"

void ggg()
{
    int d = 1 + 1;
}
int tcp_write(int fd, char *buf, int len)
{
     char *ptr;

     if (VTUN_BAD_FRAME == len & ~VTUN_FSIZE_MASK) {
         unsigned short flag_var = ntohs(*((unsigned short *) (buf + (sizeof(unsigned long)))));
         if (flag_var == 16390) {
             ggg();
         }
     }

     ptr = buf - sizeof(short);

     *((unsigned short *)ptr) = htons(len); 
     len  = (len & VTUN_FSIZE_MASK) + sizeof(short);

    if (VTUN_BAD_FRAME == len & ~VTUN_FSIZE_MASK) {
        unsigned short flag_var = ntohs(*((unsigned short *) (buf + (sizeof(unsigned long)))));
        if (flag_var == 16390) {
            ggg();
        }
    }

     return write_n(fd, ptr, len);
}

int tcp_read(int fd, char *buf)
{
     unsigned short len, flen;
     int rlen;

     /* Rad frame size */
     if( (rlen = read_n(fd, (char *)&len, sizeof(short)) ) <= 0) {
#ifdef DEBUGG
        vtun_syslog(LOG_ERR, "Null-size or -1 frame length received len %d", rlen); // TODO: remove! OK on client connect error
#endif
          return rlen;
     }

     len = ntohs(len);
     flen = len & VTUN_FSIZE_MASK;

     if( flen > VTUN_FRAME_SIZE + VTUN_FRAME_OVERHEAD ){
     	/* Oversized frame, drop it. */ 
        while( flen ){
	   len = min(flen, VTUN_FRAME_SIZE);
           if( (rlen = read_n(fd, buf, len)) <= 0 )
	      break;
           flen -= rlen;
        }
        vtun_syslog(LOG_ERR, "Oversized frame received %hd", flen); // TODO: remove!
	return VTUN_BAD_FRAME;
     }	

     if( len & ~VTUN_FSIZE_MASK ){
	/* Return flags */
        read_n(fd, buf, flen);
	return len;
     }

     /* Read frame */
     return read_n(fd, buf, flen);
}
