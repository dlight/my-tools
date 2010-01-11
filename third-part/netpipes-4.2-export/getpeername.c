/*

    getpeername.c, part of
    faucet and hose: network pipe utilities
    Copyright (C) 1995-98 Robert Forsman

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

    */

static char info[] = "getpeername: a network utility for sockets\nWritten 1995-98 by Robert Forsman <thoth@cis.ufl.edu>\n";
#include	<stdio.h>
#include	<string.h>
#include	<errno.h>
extern int errno;		/* I hate the errno header file */
#include	<stdlib.h>
#include	<sys/types.h>
#include	<sys/socket.h>
#ifndef NOUNIXSOCKETS
#include	<sys/un.h>
#endif
#include	<netinet/in.h>
#include	<netdb.h>

#include "common.h"


void usage()
{
    fprintf(stderr, "Usage: %s [ -verbose ] [ fd# ]\n", progname);
}

int getpeername();
int getsockname();

int main(argc, argv)
    int argc;
    char **argv;
{
    int	i;
    int	peer_not_sock = 1;
    int	fd = 0;			/* assume socket is attached to stdin.
				  (if it's attached to stdout, our
				  output will go over the socket) */
    int	verbose=0;

    set_progname(argv[0]);

    if (0==strcmp(progname + strlen(progname) - 11, "getsockname") &&
	(strlen(progname)<12 || progname[strlen(progname)-12] == '/'))
	peer_not_sock = 0;

    for (i=1; i<argc; i++) {
	if (0==strncmp(argv[i], "-verbose", strlen(argv[i]))) {
	    verbose++;
	} else if (0==strncmp(argv[i], "-sock", strlen(argv[i]))) {
	    peer_not_sock=0;
	} else if (0==strncmp(argv[i], "-peer", strlen(argv[i]))) {
	    peer_not_sock=1;
	} else {
	    break;
	}
    }
    if (i<argc) {
	fd = atoi(argv[i++]);
    }
    if (i<argc) {
	fprintf(stderr, "Too many arguments\n");
	usage();
	exit(1);
    }

    if (verbose>1) {
      emit_version("getpeername", 1995);
    }

    {
	union {
	    struct sockaddr	base;
	    struct sockaddr_in	in;
#ifndef NOUNIXSOCKETS
	    struct sockaddr_un	un;
#endif
	} saddr;
	struct sockaddr_in	*sinp = &saddr.in;
#ifndef NOUNIXSOCKETS
	struct sockaddr_un	*sunp = &saddr.un;
#endif
	int	saddrlen = sizeof(saddr);
	int (*f)();
	char	*name;
	if (peer_not_sock) {
	    f = getpeername;
	    name = "peer";
	} else {
	    f = getsockname;
	    name = "sock";
	}

	if (0!= f(fd, &saddr, &saddrlen)) {
	    fprintf(stderr, "%s: get%sname failed on descriptor %d: ", progname, name, fd);
	    perror("");
	    exit(1);
	}
#ifndef NOUNIXSOCKETS
	if (saddr.base.sa_family == AF_UNIX) {
	    if (verbose) puts("Unix\nPort");
	    puts(sunp->sun_path); /* with newline */
	} else
#endif
	  if (saddr.base.sa_family == AF_INET) {
	    if (verbose) puts("Internet\nPort");
	    printf("%d\n", ntohs(sinp->sin_port));
	    if (verbose) puts("Host");
	    {
		struct in_addr* addr = &sinp->sin_addr;
		int	i;
		printf("%d", ((u_char*)addr)[0]);
		for (i=1; i<sizeof(*addr); i++)
		    printf(".%d",((u_char*)addr)[i]);
		printf("\n");
	    }
	    if (verbose) {
		struct hostent	*host;
		host = gethostbyaddr((char*)&sinp->sin_addr, sizeof(sinp->sin_addr),
				     AF_INET);
		if (host) {
		    int	j,k;
		    for (j=0; host->h_addr_list[j]; j++) {
			struct in_addr	*ia =
			    (struct in_addr *)host->h_addr_list[j];
			if (0==memcmp(host->h_addr_list[j],
				      &sinp->sin_addr, host->h_length)) {
			    continue; /* skip this one */
			}
			printf("%d", ((u_char*)ia)[0]);
			for (k=1; k<sizeof(*ia); k++)
			    printf(".%d",((u_char*)ia)[k]);
			printf("\n");
		    }
		    puts(host->h_name);
		    for (j=0; host->h_aliases[j]; j++) {
			puts(host->h_aliases[j]);
		    }
		} else {
		    puts(" (no name for host)");
		}
	    }
	} else {
	    fprintf(stderr, "%s: unknown address family (%d) returned by get%sname\n", progname, saddr.base.sa_family, name);
	}
    }

    return 0;
}
