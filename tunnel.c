/*  
   vtrunkd - Virtual Tunnel Trunking over TCP/IP network. 

   Copyright (C) 2011  Andrew Gryaznov <realgrandrew@gmail.com>

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
 * tunnel.c,v 1.5.2.8.2.2 2006/11/16 04:03:56 mtbishop Exp
 */ 

#include "config.h"

#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <syslog.h>
#include <signal.h>


#include        <sys/types.h>   /* basic system data types */
#include        <sys/socket.h>  /* basic socket definitions */
#include        <sys/time.h>    /* timeval{} for select() */
#include        <time.h>                /* timespec{} for pselect() */
#include        <netinet/in.h>  /* sockaddr_in{} and other Internet defns */
#include        <arpa/inet.h>   /* inet(3) functions */
#include        <errno.h>
#include        <fcntl.h>               /* for nonblocking */
#include        <netdb.h>
#include        <signal.h>
#include        <stdio.h>
#include        <stdlib.h>
#include        <string.h>
#include        <sys/stat.h>    /* for S_xxx file mode constants */
#include        <sys/uio.h>             /* for iovec{} and readv/writev */
#include        <unistd.h>
#include        <sys/wait.h>
#include        <sys/un.h>              /* for Unix domain sockets */

#include <sys/ioctl.h>
#include <net/if.h>

#include <semaphore.h>

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
#include "linkfd.h"
#include "lib.h"
#include "netlib.h"
#include "driver.h"

int (*dev_write)(int fd, char *buf, int len);
int (*dev_read)(int fd, char *buf, int len);

int (*proto_write)(int fd, char *buf, int len);
int (*proto_read)(int fd, char *buf);

int fdserver_term = 0;

ssize_t
read_fd(int fd, void *ptr, size_t nbytes, int *recvfd)
{
    struct msghdr   msg;
    struct iovec    iov[1];
    ssize_t         n;
    int             newfd;

#ifdef  HAVE_MSGHDR_MSG_CONTROL
    union {
      struct cmsghdr    cm;
      char              control[CMSG_SPACE(sizeof(int))];
    } control_un;
    struct cmsghdr  *cmptr;

    msg.msg_control = control_un.control;
    msg.msg_controllen = sizeof(control_un.control);
#else
    msg.msg_accrights = (caddr_t) &newfd;
    msg.msg_accrightslen = sizeof(int);
#endif

    msg.msg_name = NULL;
    msg.msg_namelen = 0;

    iov[0].iov_base = ptr;
    iov[0].iov_len = nbytes;
    msg.msg_iov = iov;
    msg.msg_iovlen = 1;

    if ( (n = recvmsg(fd, &msg, 0)) <= 0)
        return(n);

#ifdef  HAVE_MSGHDR_MSG_CONTROL
    if ( (cmptr = CMSG_FIRSTHDR(&msg)) != NULL &&
        cmptr->cmsg_len == CMSG_LEN(sizeof(int))) {
        if (cmptr->cmsg_level != SOL_SOCKET) {
            vtun_syslog(LOG_ERR, "control level != SOL_SOCKET");
            return -1;
        }
        if (cmptr->cmsg_type != SCM_RIGHTS) {
            vtun_syslog(LOG_ERR, "control type != SCM_RIGHTS");
            return -1;
        }
        *recvfd = *((int *) CMSG_DATA(cmptr));
    } else
        *recvfd = -1;       /* descriptor was not passed */
#else
/* *INDENT-OFF* */
    if (msg.msg_accrightslen == sizeof(int))
        *recvfd = newfd;
    else
        *recvfd = -1;       /* descriptor was not passed */
/* *INDENT-ON* */
#endif

    return(n);
}
/* end read_fd */

ssize_t
write_fd(int fd, void *ptr, size_t nbytes, int sendfd)
{
    struct msghdr   msg;
    struct iovec    iov[1];

#ifdef  HAVE_MSGHDR_MSG_CONTROL
    union {
      struct cmsghdr    cm;
      char              control[CMSG_SPACE(sizeof(int))];
    } control_un;
    struct cmsghdr  *cmptr;

    msg.msg_control = control_un.control;
    msg.msg_controllen = sizeof(control_un.control);

    cmptr = CMSG_FIRSTHDR(&msg);
    cmptr->cmsg_len = CMSG_LEN(sizeof(int));
    cmptr->cmsg_level = SOL_SOCKET;
    cmptr->cmsg_type = SCM_RIGHTS;
    *((int *) CMSG_DATA(cmptr)) = sendfd;
#else
    msg.msg_accrights = (caddr_t) &sendfd;
    msg.msg_accrightslen = sizeof(int);
#endif

    msg.msg_name = NULL;
    msg.msg_namelen = 0;

    iov[0].iov_base = ptr;
    iov[0].iov_len = nbytes;
    msg.msg_iov = iov;
    msg.msg_iovlen = 1;

    return(sendmsg(fd, &msg, 0));
}
/* end write_fd */

static void fd_server_term(int sig){
     fdserver_term = 1;
}

int run_fd_server(int fd, char * dev, struct conn_info *shm_conn_info, int srv) {
                              int s, s2, t, len;
     struct sockaddr_un local, remote;
     char ptr;
     int ptr_len = 1;
     char str[100];
     fd_set fdset;
     struct timeval tv, cur_time;
     
     struct sigaction sa;
     
     memset(&sa, 0, sizeof(sa));
     sa.sa_handler=fd_server_term;
     sigaction(SIGTERM,&sa,NULL);

     sa.sa_handler=SIG_DFL;
     sa.sa_flags=SA_NOCLDWAIT;
     sigaction(SIGINT,&sa,NULL);
     sigaction(SIGQUIT,&sa,NULL);
     sigaction(SIGKILL,&sa,NULL);
     sigaction(SIGCHLD,&sa,NULL);
     
     sigaction(SIGUSR1,&sa,NULL);
     sigaction(SIGUSR2,&sa,NULL);
     
     // SIGPIPE is OK
     sa.sa_handler=SIG_IGN;
     sigaction(SIGPIPE,&sa,NULL);

     if ((s = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
         vtun_syslog(LOG_ERR,"srv socket");
         return -1;
     }
 
     local.sun_family = AF_UNIX;
     strcpy(local.sun_path, dev);
     unlink(local.sun_path);
     len = strlen(local.sun_path) + sizeof(local.sun_family);
     if (bind(s, (struct sockaddr *)&local, len) == -1) {
         vtun_syslog(LOG_ERR,"srv bind");
         return -1;
     }
 
     if (listen(s, 5) == -1) {
         vtun_syslog(LOG_ERR, "listen");
         return -1;
     }
     
     gettimeofday(&cur_time, NULL);
     
     vtun_syslog(LOG_INFO, "fd_server waiting for a connection...\n");
     while(!fdserver_term) {
         int done, n=0, i;
         
         FD_ZERO(&fdset);
         FD_SET(s, &fdset);
         tv.tv_sec  = 10;
         tv.tv_usec = 0;
         
         
         shm_conn_info->rdy = 1;
         if( (len = select(s+1, &fdset, NULL, NULL, &tv)) < 0 ){ // selecting from multiple processes does actually work...
             // errors are OK if signal is received... TODO: do we have any signals left???
             if( errno != EAGAIN && errno != EINTR ) {
                vtun_syslog(LOG_INFO, "fd_server eagain select err; exit");
                break;
             } else {
               //vtun_syslog(LOG_INFO, "fd_server else select err; continue norm");
                continue;
             }
         }
         
         if(FD_ISSET(s, &fdset)) {
               
               t = sizeof(remote);
               
               if ((s2 = accept(s, (struct sockaddr *)&remote, &t)) == -1) {
                   vtun_syslog(LOG_ERR,"srv accept");
                   return -1;
               }
       
               vtun_syslog(LOG_INFO, "fd_server connected.\n");
       
               done = 0;
               do {
       
                   if (!done) 
                       if (write_fd(s2, &ptr, ptr_len, fd) < 0) {
                           vtun_syslog(LOG_ERR,"fd_srv send error");
                           done = 1;
                       }
                   done = 1;
               } while (!done);
       
               close(s2);
         } else {
               gettimeofday(&cur_time, NULL);
               if( srv && ( (cur_time.tv_sec - shm_conn_info->alive) > PROCESS_FD_SHM_TIMEOUT )) {
                         break;
               }
         }
         
         
     
     }
     
     // clean up
     memset(shm_conn_info, 0, sizeof(struct conn_info));
     vtun_syslog(LOG_INFO, "fd_server zeroing shm & exiting.\n");
     exit(0);
 
     return 0;
 
}

int read_fd_full(int *fd, char *dev) {
     int s, len;
     char ptr;
     size_t ptr_len = 1;
     struct sockaddr_un local, remote;
     
     if ((s = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
          vtun_syslog(LOG_ERR, "cannot create socket for fd_server connection");
          return -1;
     }
     remote.sun_family = AF_UNIX;
     strcpy(remote.sun_path, dev);
     len = strlen(remote.sun_path) + sizeof(remote.sun_family);
     if (connect(s, (struct sockaddr *)&remote, len) == -1) {
          vtun_syslog(LOG_ERR, "can not connect to fd_server");
          return -1;
     }
     read_fd(s, &ptr, ptr_len, fd);
     close(s);  
}

/* Initialize and start the tunnel.
   Returns:
      -1 - critical error
      0  - normal close or noncritical error 
*/
   
int tunnel(struct vtun_host *host, int srv)
{
     int null_fd, pid, opt, pid2;
     int fd[2]={-1, -1};
     char dev[VTUN_DEV_LEN]="";
     int interface_already_open = 0;
     int shmid, i;
     struct sockaddr_un remote;
     int connid = -1;
     struct conn_info *shm_conn_info;
     int my_conn_num = -1;
     int s, s2, t, len;
     
     char str[100];
     
     
     if(host->persist == VTUN_PERSIST_KEEPIF) {
          vtun_syslog(LOG_INFO,"PERSIST enabled");
     } else {
          vtun_syslog(LOG_INFO,"PERSIST DIS-abled");
     }


     if ( (host->persist == VTUN_PERSIST_KEEPIF) &&
	  (host->loc_fd >= 0) ) {
        interface_already_open = 0; // always try to reopen interface since shm is always required to get!!
        vtun_syslog(LOG_INFO,"Reusing open fd(NOT:-)!");
     } else {
          vtun_syslog(LOG_INFO,"New fd required (%d) ", host->loc_fd);
     }

     /* Initialize device. */
     if( host->dev ){
        strncpy(dev, host->dev, VTUN_DEV_LEN);
	dev[VTUN_DEV_LEN-1]='\0';
     }
     if( ! interface_already_open ){
        switch( host->flags & VTUN_TYPE_MASK ){
           case VTUN_TTY:
	      if( (fd[0]=pty_open(dev)) < 0 ){
		 vtun_syslog(LOG_ERR,"Can't allocate pseudo tty. %s(%d)", strerror(errno), errno);
		 return -1;
	      }
	      break;

           case VTUN_PIPE:
	      if( pipe_open(fd) < 0 ){
		 vtun_syslog(LOG_ERR,"Can't create pipe. %s(%d)", strerror(errno), errno);
		 return -1;
	      }
	      break;

           case VTUN_ETHER:
	      if( (fd[0]=tap_open(dev)) < 0 ){
		 vtun_syslog(LOG_ERR,"Can't allocate tap device %s. %s(%d)", dev, strerror(errno), errno);
		 return -1;
	      }
	      break;

	   case VTUN_TUN:
	      if( (fd[0]=tun_open(dev)) < 0 ){ // 
                int while_end = 1;
                for (int i = 0; while_end; i++) {
                    if (srv) {
                        vtun_syslog(LOG_INFO, "vtrunkd SERVER: trying to attach to running server");
                        if ((shmid = shmget(SHM_TUN_KEY, sizeof(struct conn_info) * vtun.MAX_TUNNELS_NUM, 0666)) < 0) {
                            vtun_syslog(LOG_ERR, "shmget 3");
                            return -1;
                        }
                        if ((shm_conn_info = shmat(shmid, NULL, 0)) == ((struct conn_info *) -1)) {
                            vtun_syslog(LOG_ERR, "shmat 3");
                            return -1;
                        }
                        // now scan for names, only fail if no found
                        for (i = 0; i < vtun.MAX_TUNNELS_NUM; i++) {
                            if (strcmp(shm_conn_info[i].devname, dev) == 0) {
                                connid = i;
                                break;
                            }
                        }
                        if (connid < 0) {
                            vtun_syslog(LOG_ERR, "Can't allocate tun device %s. %s(%d) - did not find master server shm", dev, strerror(errno),
                                    errno);
                            return 0;
                        }
                        if (!shm_conn_info[connid].rdy) {
                            if (i == 20) {
                                vtun_syslog(LOG_ERR, "SHM not ready EXIT");
                                return 0;
                            }
                            vtun_syslog(LOG_WARNING, "SHM not ready yet; I'll try again");
                            usleep(200000);
                            continue;
                        }
                    } else {
                        vtun_syslog(LOG_INFO, "vtrunkd CLIENT: trying to attach to running buddy");
                        // match only first conn...
                        // here comes the play. !. detect whether we are server or client??
                        if ((shmid = shmget(SHM_TUN_KEY, sizeof(struct conn_info), 0666)) < 0) {
                            vtun_syslog(LOG_ERR, "shmget 4");
                            return -1;
                        }
                        if ((shm_conn_info = shmat(shmid, NULL, 0)) == (struct conn_info *) -1) {
                            vtun_syslog(LOG_ERR, "shmat 4"); // FAULT HERE
                            return -1;
                        }

                        // now see if it is available.. only exit if not available
                        // never reaches here if not available...
                        if (strcmp(shm_conn_info[0].devname, dev) == 0) {
                            // ok
                            connid = 0;
                        } else {
                            vtun_syslog(LOG_ERR, "Can't allocate tun device %s. %s(%d) - did not find running buddy shm %s != %s", dev,
                                    strerror(errno), errno, shm_conn_info[0].devname, dev);
                            return 0;
                        }
                        if (!shm_conn_info[connid].rdy) {
                            if (i == 20) {
                                vtun_syslog(LOG_ERR, "SHM not ready EXIT");
                                return 0;
                            }
                            vtun_syslog(LOG_WARNING, "SHM not ready yet; I'll try again");
                            usleep(200000);
                            continue;
                        }

                    }
                    break;
                }

                    
                    // in either case, try to read

                    if(read_fd_full(&fd[0], dev) < 0) {
                         //vtun_syslog(LOG_ERR,"Cannot read fd tun device %s. %s(%d); failed to get from server", dev, strerror(errno), errno);
                         // wtf is going on here?? always <0! ?!?
                         //return -1;
                    }
                    
                    
                             
                    
                    if(fd[0] < 0) { // still...
                         vtun_syslog(LOG_ERR,"Can't allocate tun device %s. %s(%d); failed to get from server", dev, strerror(errno), errno);
                         return 0;     
                    } else {
                         // now we are forked, and have everything set up (?)
                    }
                    
	      } else {
                    // We were able to open device => we are new connection. connect shm, set devname in conn_info
                    if(srv) {
                         vtun_syslog(LOG_INFO,"vtrunkd SERVER: setting up fresh connection");
                         if ((shmid = shmget(SHM_TUN_KEY, sizeof(struct conn_info) * vtun.MAX_TUNNELS_NUM, 0666)) < 0) {
                              vtun_syslog(LOG_ERR,"shmget 5");
                              return -1;
                         }
                         if ((shm_conn_info = shmat(shmid, NULL, 0)) == (struct conn_info *) -1) {
                              vtun_syslog(LOG_ERR,"shmat 5");
                              return -1;
                         }
                         // now scan for free names
                         for(i=0; i<vtun.MAX_TUNNELS_NUM;i++) {
                              if(shm_conn_info[i].devname[0] == 0) {
                                   strcpy(shm_conn_info[i].devname, dev);
                                   connid = i;
                                   break;
                              }
                         }
                         if(i >= vtun.MAX_TUNNELS_NUM-1) {
                              vtun_syslog(LOG_ERR,"Can't allocate tun device %s. %s(%d) - did not find free slots master server shm", dev, strerror(errno), errno);
                              return -1;
                         }
                    } else {
                         vtun_syslog(LOG_INFO,"vtrunkd CLIENT: setting up fresh connection");

                         if ((shmid = shmget(SHM_TUN_KEY, sizeof(struct conn_info), 0666)) < 0) {
                              vtun_syslog(LOG_ERR,"shmget 6");
                              return -1;
                         }
                         if ((shm_conn_info = shmat(shmid, NULL, 0)) == (struct conn_info *) -1) {
                              vtun_syslog(LOG_ERR,"shmat 6");
                              return -1;
                         }

                         // TODO: make sure this check is nesessary! we already suppose that we're the only client on this host
                         //   so that in case we have an initialized shm but fd_server not running we will fail here (e.g. race?)
                         if(shm_conn_info[0].devname[0] == 0) {
                              // ok
                              strcpy(shm_conn_info[0].devname, dev);
                              vtun_syslog(LOG_INFO,"copied tun name to shm_conn_info: %s", shm_conn_info[0].devname);
                              connid = 0;
                         } else {
                              vtun_syslog(LOG_ERR,"Can't allocate tun device %s. %s(%d) - did not find freeslot at running buddy shm", dev, strerror(errno), errno);
                              return -1;
                         }
                    }
                    
                    // okay, now init if tx queue size! we dont want it too big on slow links! (what we're likely to have here)
                    struct ifreq ifr_queue;
                    int ctl_sock;
            
                    if ((ctl_sock = socket(AF_INET, SOCK_DGRAM, 0)) >= 0) {
                        memset(&ifr_queue, 0, sizeof(ifr_queue));
                        strcpy(ifr_queue.ifr_name, dev);
                        ifr_queue.ifr_qlen = host->TUN_TXQUEUE_LEN;
                        if (ioctl(ctl_sock, SIOCSIFTXQLEN, (void *) &ifr_queue) < 0) {
                            vtun_syslog(LOG_ERR, "ioctl SIOCGIFTXQLEN");
                            return -1;
                        }
                        close(ctl_sock);
                    }
                    else {
                        vtun_syslog(LOG_ERR, "error open control socket");
                        return -1;
                    }
                    
                    // init all semaphores, etc...
                    vtun_syslog(LOG_INFO, "init vars sem %s", shm_conn_info[connid].devname);

                    sem_init(&shm_conn_info[connid].tun_device_sem, 1, 1);
                    sem_init(&shm_conn_info[connid].write_buf_sem, 1, 1);
                    sem_init(&shm_conn_info[connid].resend_buf_sem, 1, 1);
                    sem_init(&shm_conn_info[connid].stats_sem, 1, 1);
                    sem_init(&shm_conn_info[connid].AG_flags_sem, 1, 1);
                    sem_init(&shm_conn_info[connid].common_sem, 1, 1);

                    for(i=0; i<MAX_TCP_LOGICAL_CHANNELS;i++) {
                         shm_conn_info[connid].seq_counter[i] = SEQ_START_VAL; // start with 10!! 0-9 are reserved as flags
                         shm_conn_info[connid].write_buf[i].last_written_seq = SEQ_START_VAL;
                         shm_conn_info[connid].resend_buf[i].last_written_seq = SEQ_START_VAL; // not needed...
                         frame_llist_init(&shm_conn_info[connid].write_buf[i].frames);
                         frame_llist_init(&shm_conn_info[connid].resend_buf[i].frames);
                    }
                    
                    frame_llist_init(&shm_conn_info[connid].wb_free_frames);
                    frame_llist_fill(&shm_conn_info[connid].wb_free_frames, shm_conn_info[connid].frames_buf, FRAME_BUF_SIZE);
                    
                    
                    frame_llist_init(&shm_conn_info[connid].rb_free_frames);
                    frame_llist_fill(&shm_conn_info[connid].rb_free_frames, shm_conn_info[connid].resend_frames_buf, FRAME_BUF_SIZE);
                    
                    vtun_syslog(LOG_INFO, "Starting fd_server");
                    if ((s = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
                         vtun_syslog(LOG_ERR, "socket 4");
                         return -1;
                    }
                    remote.sun_family = AF_UNIX;
                    strcpy(remote.sun_path, dev);
                    len = strlen(remote.sun_path) + sizeof(remote.sun_family);
                    if (connect(s, (struct sockaddr *)&remote, len) == -1) {
                         switch(pid2 = fork()) {
                              case -1:
                                   vtun_syslog(LOG_ERR,"Couldn't fork() on fd server");
                                   return 0;
                              case 0:
                                   // now run server
                                   // TODO: how to stop server??
                                   //chdir("/var"); // chdir to protect gprof
                                   signal(SIGCHLD, SIG_IGN);
                                   set_title("fd server running");
                                   run_fd_server(fd[0], dev, &shm_conn_info[connid], srv);
                        }     
                    }
                    close(s);
               
              }
	      break;
	}
        vtun_syslog(LOG_INFO,"Setting loc_fd to %d", fd[0]);
	host->loc_fd = fd[0];
     }
     host->sopt.dev = strdup(dev);

     /* Initialize protocol. */
     switch( host->flags & VTUN_PROT_MASK ){
        case VTUN_TCP:
	   opt=1;
	   setsockopt(host->rmt_fd,SOL_SOCKET,SO_KEEPALIVE,&opt,sizeof(opt) );

	   opt=1;
	   setsockopt(host->rmt_fd,IPPROTO_TCP,TCP_NODELAY,&opt,sizeof(opt) );

	   proto_write = tcp_write;
	   proto_read  = tcp_read;

	   break;

        case VTUN_UDP:
	   if( (opt = udp_session(host)) == -1){
	      vtun_syslog(LOG_ERR,"Can't establish UDP session");
	      close(fd[1]);
	      if( ! ( host->persist == VTUN_PERSIST_KEEPIF ) )
		 close(fd[0]);
	      return 0;
	   } 	

 	   proto_write = udp_write;
	   proto_read = udp_read;

	   break;
     }

        switch( (pid=fork()) ){
	   case -1:
	      vtun_syslog(LOG_ERR,"Couldn't fork()");
	      if( ! ( host->persist == VTUN_PERSIST_KEEPIF ) )
		 close(fd[0]);
	      close(fd[1]);
	      return 0;
 	   case 0:
           /* do this only the first time when in persist = keep mode */
           //chdir("/var"); // gprof
           if( ! interface_already_open ){
	      switch( host->flags & VTUN_TYPE_MASK ){
	         case VTUN_TTY:
		    /* Open pty slave (becomes controlling terminal) */
		    if( (fd[1] = open(dev, O_RDWR)) < 0){
		       vtun_syslog(LOG_ERR,"Couldn't open slave pty");
		       exit(0);
		    }
		    /* Fall through */
	         case VTUN_PIPE:
		    null_fd = open("/dev/null", O_RDWR);
		    close(fd[0]);
		    close(0); dup(fd[1]);
		    close(1); dup(fd[1]);
		    close(fd[1]);

		    /* Route stderr to /dev/null */
		    close(2); dup(null_fd);
		    close(null_fd);
		    break;
	         case VTUN_ETHER:
	         case VTUN_TUN:
		    break;
	      }
           }
	   /* Run list of up commands */
	   set_title("%s running up commands", host->host);
	   llist_trav(&host->up, run_cmd, &host->sopt);

	   exit(0);           
	}

     switch( host->flags & VTUN_TYPE_MASK ){
        case VTUN_TTY:
	   set_title("%s tty", host->host);

	   dev_read  = pty_read;
	   dev_write = pty_write; 
	   break;

        case VTUN_PIPE:
	   /* Close second end of the pipe */
	   close(fd[1]);
	   set_title("%s pipe", host->host);

	   dev_read  = pipe_read;
	   dev_write = pipe_write; 
	   break;

        case VTUN_ETHER:
	   set_title("%s ether %s", host->host, dev);

	   dev_read  = tap_read;
	   dev_write = tap_write; 
	   break;

        case VTUN_TUN:
	   set_title("%s tun %s", host->host, dev);

	   dev_read  = tun_read;
	   dev_write = tun_write; 
	   break;
     }
     if(srv) shm_conn_info[connid].usecount++;
     
     // finally, choose my conn number in stats block
     for(i=0; i<MAX_TCP_PHYSICAL_CHANNELS; i++) {
          if(shm_conn_info[connid].stats[i].pid == 0) {
               if(my_conn_num == -1) my_conn_num = i;
          } else {
               if( kill(shm_conn_info[connid].stats[i].pid, 0) < 0 ) {
                    // okay no proc found, use it
                    if(my_conn_num == -1) my_conn_num = i;
                    shm_conn_info[connid].stats[i].pid = 0;
                    shm_conn_info[connid].stats[i].weight = 0;
               }
          }
     }
     shm_conn_info[connid].stats[my_conn_num].pid = getpid();
     if(my_conn_num == -1) {
          vtun_syslog(LOG_ERR, "could not find free conn_num for stats, exit.");
          return -1;
     }

     opt = linkfd(host, &(shm_conn_info[connid]), srv, my_conn_num);
     set_title("%s running down commands", host->host);

     if(srv) {
          shm_conn_info[connid].usecount--;
          if(shm_conn_info[connid].usecount <= 0) { // AND we're going to quit the process...?
               // if we do "free" here, we need to kill fd server, free the local fd tun device - otherwise it will try shm_find+fd_read and fail
               // now check the lock.. if left locked - release!
               if(sem_trywait(&(shm_conn_info[connid].tun_device_sem)) == -1) {
                    vtun_syslog(LOG_ERR,"ASSERT: usecount == 0 && sem left unclosed!");
               } else {
                    sem_post(&(shm_conn_info[connid].tun_device_sem));
               }
          }
          if (shm_conn_info[connid].usecount < 0) {
               vtun_syslog(LOG_ERR,"ASSERT: usecount < 0 !!!");
          }
     } else {
          // noop
     }
     
     if(! ( host->persist == VTUN_PERSIST_KEEPIF ) ) {
        set_title("%s closing", host->host);

	/* Gracefully destroy interface */
	switch( host->flags & VTUN_TYPE_MASK ){
           case VTUN_TUN:
              tun_close(fd[0], dev); // or even nerver close???
	      break;

           case VTUN_ETHER:
	      tap_close(fd[0], dev);
	      break;
	}

       	//close(host->loc_fd); // never close too
     }

     /* Close all other fds */
     close(host->rmt_fd);

     return opt;
}
