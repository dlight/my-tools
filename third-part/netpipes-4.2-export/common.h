/*

    $id$, part of
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

#ifndef common_h_
#define common_h_

/* set from argv[0] by the main() routine */
extern char *programname;

/* list of file descriptors to dup from the socket */
extern int	*fds;
extern int	nfds;

/* the mode to use when shutting down the socket
	<0: don't do any shutdown
	0 : shut down recieves (write-only)
	>0: shut down sends (read-only)
  */

extern int	how_shutdown;

/* add a file descriptor to the list */
void add_fd(/* int fd */);

/* add a file descriptor to the list */
void reserve_fds(/* int placeholder */);

/* do the dup from the argument socket to the list of file descriptors,
   perform the requested shutdown... */
void dup_n(/*int socket */);

/**********************************************************************/

int name_to_inet_port(/* char* */);

/**********************************************************************/

struct in_addr ** /* addr_array */
convert_hostname(/* char *, int * */);

/**********************************************************************/

void
printhost(/* struct in_addr * */);

/**********************************************************************/

int
bindlocal(/* int, char *, char*, int, int */);

/**********************************************************************/

void				/* this is in version.c */
emit_version(/* char*, int */);

				/* this is also in version.c */
extern char *progname;
void set_progname(/* char *argv0 */);

/* system dependencies */

#ifdef NO_STRERROR
/* XXX assumption here that no strerror() means */
/* no definition of sys_errlist in errno.h */
extern char *sys_errlist[];

char * strerror(/*int*/);
#endif

#ifdef POSIX_SIG
#define signal(a, b) { \
  struct sigaction sa; \
  sa.sa_handler = b; \
  sigemptyset(&sa.sa_mask); \
  sa.sa_flags = 0; \
  sigaction(a, &sa, NULL); \
}
#endif
#ifdef SYSV
#define signal(a, b) sigset(a, b)
#endif

int valid_descriptor(/*int fd */);

#endif /* common_h_ */
