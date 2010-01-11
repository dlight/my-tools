/*

    $Id: common.c,v 1.20 1998/08/13 14:53:30 thoth Exp $, part of
    faucet and hose: network pipe utilities
    Copyright (C) 1992-98 Robert Forsman

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

#include	<stdio.h>
#include	<stdlib.h>
#include	<string.h>
#include	<fcntl.h>
#include	<errno.h>
#include	<unistd.h>
#include	<sys/socket.h>
#ifndef NOUNIXSOCKETS
#include	<sys/un.h>
#endif /*NOUNIXSOCKETS*/
#include	<sys/time.h>
#include	<netdb.h>
#include	<netinet/in.h>
#include	<arpa/inet.h>
#include "common.h"

#define	EXITCODE_CONNECTION	127
#define	EXITCODE_ARGS	126

/**********************************************************************/

int	*fds=0;
int	nfds;
int	fdsize=0;

int how_shutdown = -2;

void add_fd(fd)
    int	fd;
{
    if (fds==0) {
	fds = (int*)malloc(sizeof(*fds)*(fdsize=4));
    } else if (nfds >= fdsize) {
	fds = (int*)realloc(fds, sizeof(*fds)*(fdsize*=2));
	if (fds==0) {
	    fprintf(stderr, "%s: Out of memory\n", progname);
	    exit(1);
	}
    }
    fds[nfds++] = fd;
    if (fd>2)
	/* We should reserve this spot in the file descriptor table.
	 If we don't it could get allocated by the socket(2) call and
	 we would have an awful mess on our hands. */
	dup2(0, fd);
}

void reserve_fds(placeholder)
     int	placeholder;	/* I usually pass in 0, assuming that
				   the process will have a stdin, even
				   if it's attached to /dev/null */
{
    int	i;
    for (i=0; i<nfds; i++) {
	if (!valid_descriptor(fds[i]))
	    dup2(0, fds[i]);
    }
}

void dup_n(socket)
    int	socket;
{
    int	i;
#if 0
    printf("I will redirect fds");
    for (i=0; i<nfds; i++) {
	printf(" %d", fds[i]);
    }
    printf(" and shutdown with %d\n", how_shutdown);
#endif
    if (how_shutdown>=0)
	shutdown(socket, how_shutdown!=0);
    for (i=0; i<nfds; i++) {
	dup2(socket, fds[i]);
    }
}

/**********************************************************************/

int name_to_inet_port(portname)
char *portname;
/* This procedure converts a character string to a port number.  It looks
   up the service by name and if there is none, then it converts the string
   to a number with sscanf */
{
  struct servent	*p;

  if (portname==NULL)
    return 0;

  p = getservbyname(portname,"tcp");
  if (p!=NULL)
    {
      return p->s_port;
    }
  else
    {
      int	port;
      if (sscanf(portname,"%i",&port)!=1)
	{
	  return 0;
	}
      else
	return htons(port);
    }
}

struct in_addr ** /* addr_array */
convert_hostname(name, count_ret)
    char	*name;
    int		*count_ret;
{
  struct hostent	*hp;
  struct in_addr	**rval;

  hp = gethostbyname(name);
  if (hp != NULL) {
    int	i;
    if (hp->h_length != sizeof(struct in_addr)) {
	fprintf(stderr, "%s: Funky: (hp->h_length = %d) != (sizeof(struct in_addr) = %ld)\n", progname, hp->h_length, (long) sizeof(struct in_addr));
    }
    for (i = 0; hp->h_addr_list[i]; i++)
	{ }
    *count_ret = i;
    rval = (struct in_addr **)malloc(sizeof(*rval) * (i+1));
    for (i=0; i<*count_ret; i++) {
	rval[i] = (struct in_addr*)malloc(hp->h_length);
	memcpy((char*)rval[i], hp->h_addr_list[i], hp->h_length);
    }
    rval[*count_ret] = 0;
    return rval;
  } else {
#ifndef HAVE_INET_ATON
      int	count, len;
      unsigned int	a1,a2,a3,a4;
#endif
      rval = (struct in_addr**)malloc(2*sizeof(*rval));
      rval[0] = (struct in_addr*)malloc(sizeof(struct in_addr));
#ifdef HAVE_INET_ATON
      if (0==inet_aton(name, rval[0])) {
	  *count_ret = 0;
	  free(rval[0]);
	  free(rval);
	  return 0;
      }
#else
      count = sscanf(name,"%i.%i.%i.%i%n", &a1, &a2, &a3, &a4, &len);
      if (4!=count || 0!=name[len] )
	  return 0;
      rval[0]->s_addr = (((((a1 << 8) | a2) << 8) | a3) << 8) | a4;
#endif
      *count_ret = 1;
      rval[1] = 0;

      return rval;
  }
}


/* print an internet host address prettily */
void printhost(fp, addr)
     FILE	*fp;
     struct in_addr	*addr;
{
  struct hostent	*h;
  char	*s,**p;

  h = gethostbyaddr((char*)addr, sizeof(*addr),AF_INET);
  s = (h==NULL) ? NULL : (char*)/*gratuitous cast away const*/h->h_name;

  fputs(inet_ntoa(*addr), fp);

  fprintf(fp, "(%s",s?s:"name unknown");
  if (s)
    for (p=h->h_aliases; *p; p++)
      fprintf(fp, ",%s",*p);
  fprintf(fp, ")");
}

#ifdef NO_STRERROR
/* Added for those systems without */
extern char *sys_errlist[];
extern int sys_nerr;

char *
strerror(num)
     int num;
{
  static char ebuf[40];		/* overflow this, baby */
  
  if (num < sys_nerr)
    return sys_errlist[num];
  else
    sprintf(ebuf, "Unknown error: %i\n", num);
  return ebuf;
}
#endif

/* bind to a port on the local machine. */
int
bindlocal(fd, name, addrname, domain, reuseaddr)
     int	fd, domain;
     char	*name, *addrname;
     int	reuseaddr;
{
  struct sockaddr	*laddr;
  int	addrlen;
  int	countdown;
  int	rval;
  
  if (reuseaddr && domain == AF_INET) {
#ifdef SO_REUSEADDR
      /* The below fix is based on articles that came from comp.sys.hp.hpux
	 with the problem of having FIN_WAIT_2 statuses on sockets.  But even
	 on Solaris the sockets with TIME_WAIT block the bind() call, so I
	 thought it would be a good idea to try the setsockopt() call.
	 1998/01/18 Thomas Endo <tendo@netcom.com> */
      int	enable = 1;
      if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char *)&enable,
		     sizeof(enable)) < 0)
	  {
	      fprintf(stderr,"%s: error in setsockopt (%s)\n",progname,
		      strerror(errno));
	      exit(EXITCODE_CONNECTION);
	  }
#else
      fprintf(stderr, "%s: Warning. SO_REUSEADDR is not available\n",
	      progname);
#endif
  }

  if (domain==AF_INET)
    {
      static struct sockaddr_in	srv;
      static int	initted=0;

      laddr = (struct sockaddr*)&srv;
      addrlen = sizeof(srv);

      if (!initted) {
	srv.sin_family = AF_INET;

	if (addrname) {
	    int	count;
	    struct in_addr **addresses;
	    addresses = convert_hostname(addrname, &count);
	    if (addresses == 0) {
		fprintf(stderr, "%s: Unable to convert %s to an internet address\n", progname, addrname);
		errno=0;
		return 0;
	    }
	    srv.sin_addr = *(addresses[0]);
	} else {
	    srv.sin_addr.s_addr = INADDR_ANY;
	}
	
	srv.sin_port = name_to_inet_port(name);
      
	if (srv.sin_port==0)
	  {
	    fprintf(stderr, "%s: port %s unknown\n", progname, name);
	    errno = 0;
	    return 0;
	  }
      }
      initted = 1;		/* bindlocal is only called once in
				   each netpipes program */
    }
#ifndef NOUNIXSOCKETS
  else if (domain == AF_UNIX)
    {
      static struct sockaddr_un	srv;
      laddr = (struct sockaddr*)&srv;
      addrlen = sizeof(srv);
      
      srv.sun_family = AF_UNIX;
      strncpy(srv.sun_path, name, sizeof(srv.sun_path));
      srv.sun_path[sizeof(srv.sun_path) -1] = 0; /* NUL terminate that string*/
    }
#endif
  else
    {
      fprintf(stderr, "%s: unknown address family %d in bindlocal()\n",
	      progname, domain);
      exit(EXITCODE_ARGS);
    }
  
  countdown= (domain!=AF_INET || reuseaddr)?1:10;
  do {
    rval = bind(fd, laddr, addrlen);
    if (rval != 0)
      {
	if (errno==EADDRINUSE && --countdown>0)
	  {
	    fprintf(stderr,"%s: Address %s in use, sleeping 10.\n",
		    progname, name);
	    sleep (10);
	    fprintf(stderr,"%s: Trying again . . .\n", progname);
	  }
	else
	  return 0;
      }
  } while (rval);

  return 1;
}


/* check to see if the descriptor is assigned to a pipe/file/socket (valid)
   or is unused (INvalid) */
int valid_descriptor(int fd)
{
	int	rval;
	fd_set	fds;
	struct timeval	tv;

	tv.tv_sec = 0; tv.tv_usec = 0; /* POLL */

	FD_ZERO(&fds);
	FD_SET(fd, &fds);

	rval = select(fd+1, &fds, (fd_set *)0, (fd_set *)0, &tv);

	if (rval<0 && errno == EBADF) {
#ifdef DEBUG
	    fprintf(stderr, "%s: descriptor %d not in use\n",
		    progname, fd);
#endif
	    return 0;
	} else {
#ifdef DEBUG
	    fprintf(stderr, "%s: descriptor %d already in use\n",
		    progname, fd);
#endif
	    return 1;
	}
}
