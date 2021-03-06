#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mdnsd.h"
#include "sdtxt.h"
#include "ifenum.h"

void con(char *name, int type, void *arg)
{
    printf("conflicting name detected %s for type %d\n",name,type);
    exit(1);
}

int _shutdown = 0;
mdnsd _d;
int _zzz[2];

void kill_responder()
{
    _shutdown = 1;
    mdnsd_shutdown(_d);
    write(_zzz[1]," ",1);
}

// create multicast 224.0.0.251:5353 socket
int msock()
{
    int s, flag = 1, ittl = 255;
    struct sockaddr_in in;
    struct ip_mreq mc;
    char ttl = 255;

    bzero(&in, sizeof(in));
    in.sin_family = AF_INET;
    in.sin_port = htons(5353);
    in.sin_addr.s_addr = 0;

    if((s = socket(AF_INET,SOCK_DGRAM,0)) < 0) return 0;
#ifdef SO_REUSEPORT
    setsockopt(s, SOL_SOCKET, SO_REUSEPORT, (char*)&flag, sizeof(flag));
#endif
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char*)&flag, sizeof(flag));
    if(bind(s,(struct sockaddr*)&in,sizeof(in))) { close(s); return 0; }

    mc.imr_multiaddr.s_addr = inet_addr("224.0.0.251");
    mc.imr_interface.s_addr = htonl(INADDR_ANY);
    setsockopt(s, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mc, sizeof(mc)); 
    setsockopt(s, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
    setsockopt(s, IPPROTO_IP, IP_MULTICAST_TTL, &ittl, sizeof(ittl));

    flag =  fcntl(s, F_GETFL, 0);
    flag |= O_NONBLOCK;
    fcntl(s, F_SETFL, flag);

    return s;
}

int announce(char *serviceName, char* portStr, char* optionalPath)
{
    mdnsd d;
    mdnsdr r;
    struct message m;
    unsigned long int ip;
    unsigned short int port;
    struct timeval *tv;
    int bsize, ssize = sizeof(struct sockaddr_in);
    unsigned char buf[MAX_PACKET_LEN];
    struct sockaddr_in from, to;
    fd_set fds;
    int s;
    unsigned char *packet, hlocal[256], nlocal[256];
    int len = 0;
    xht h;
    int zz;

    /* Find all the interface addresses */
    char* addresses[8];
    int numIfaces = ifenum(addresses);

    port = atoi(portStr);

    pipe(_zzz);
    _d = d = mdnsd_new(1,1000);
    if((s = msock()) == 0) { printf("can't create socket: %s\n",strerror(errno)); return 1; }

    for (zz =0;zz<numIfaces;zz++) {
       sprintf(nlocal,"freemote-%s-%d.local.",serviceName,zz);
       sprintf(hlocal,"%s-%d._freemote._tcp.local.",serviceName,zz);
       r = mdnsd_shared(d,"_freemote._tcp.local.",QTYPE_PTR,120);
       mdnsd_set_host(d,r,hlocal);
       r = mdnsd_unique(d,hlocal,QTYPE_SRV,600,con,0);
       mdnsd_set_srv(d,r,0,0,port,nlocal);
       r = mdnsd_unique(d,nlocal,QTYPE_A,600,con,0);
       ip = inet_addr(addresses[zz]);
       mdnsd_set_raw(d,r,(unsigned char *)&ip,4);
       r = mdnsd_unique(d,hlocal,16,600,con,0);
       h = xht_new(11);
       if(optionalPath && strlen(optionalPath) > 0) xht_set(h,"path",optionalPath);
       packet = sd2txt(h, &len);
       xht_free(h);
       mdnsd_set_raw(d,r,packet,len);
       free(packet);

       free(addresses[zz]);
    }

    while(1)
    {
        tv = mdnsd_sleep(d);
        FD_ZERO(&fds);
        FD_SET(_zzz[0],&fds);
        FD_SET(s,&fds);
        select(s+1,&fds,0,0,tv);

        // only used when we wake-up from a signal, shutting down
        if(FD_ISSET(_zzz[0],&fds)) read(_zzz[0],buf,MAX_PACKET_LEN);

        if(FD_ISSET(s,&fds))
        {
            while((bsize = recvfrom(s,buf,MAX_PACKET_LEN,0,(struct sockaddr*)&from,&ssize)) > 0)
            {
                bzero(&m,sizeof(struct message));
                message_parse(&m,buf);
                mdnsd_in(d,&m,(unsigned long int)from.sin_addr.s_addr,from.sin_port);
            }
            if(bsize < 0 && errno != EAGAIN) { printf("can't read from socket %d: %s\n",errno,strerror(errno)); return 1; }
        }
        while(mdnsd_out(d,&m,&ip,&port))
        {
            bzero(&to, sizeof(to));
            to.sin_family = AF_INET;
            to.sin_port = port;
            to.sin_addr.s_addr = ip;
            if(sendto(s,message_packet(&m),message_packet_len(&m),0,(struct sockaddr *)&to,sizeof(struct sockaddr_in)) != message_packet_len(&m))  { printf("can't write to socket: %s\n",strerror(errno)); return 1; }
        }
        if(_shutdown) break;
    }

    mdnsd_shutdown(d);
    mdnsd_free(d);
    return 0;
}
