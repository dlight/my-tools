/*

    encapsulate.c, part of
    netpipes: network pipe utilities
    Copyright (C) 1996 Robert Forsman

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

static char info[] = "encapsulate: a  utility for sockets\nWritten 1996 by Robert Forsman <thoth@purplefrog.com>\n";

/*
   Encapsulate

   encapsulate is designed to be used in a script spawned as the child
   of a faucet or hose.

  ============================================================

  NEW version of encapsulate now.  It uses the Session Control
  Protocol described in http://sunsite.unc.edu/ses/scp.html .

  ARGS:

  If they don't specify --client or --server, compare your socket ID
  (IP first, then port if IPs match) to the other end.  The
  numerically lower one is the server.  The MSB is the first octet of
  the IP.  If they're identical, you have bigger problems >:)

  For each --outfd on the subprocess, send a SYN packet with a new
  session ID.  For each --infd on the subprocess, set aside a
  structure to wait for a SYN packet from the other end.  Each SYN
  packet will be associated with a file descriptor in the order they
  were specified on the command line.

  The --duplex descriptors are trickier.  They need one session ID,
  but who gets to allocate it?  With --Duplex the "client" (which
  could be local or remote) allocates the session ID and sends the SYN
  packet.  The "server" then sends a SYN packet for the same session
  ID causing the connection to become two-way.

  With --duplex, the local program waits for a SYN from the other side
  and responds with a SYN of its own to duplex the session.  With
  --DUPLEX the local program sends the SYN packet and the remote side
  is expected to duplex the session.  The short flags correspond to
  the long like so:

  --Duplex n		-dn
  --duplex n		-ion
  --DUPLEX n		-oin

  Notice how the order of the i and o correspond to the order in which
  the SYN packets are sent.

  INTEROPERABILITY

  This program is immensely complicated by the fact that I want it to
  interoperate with a server that could also be speaking SCP (with
  some theoretical restrictions to prevent the following problems).

  1) SYN packets to associate with input descriptors could come
  arbitrarily late in the conversaion.

  2) The remote program might not be obeying our rules for the
  --Duplex family of directives (this particular problem, and others,
  could also be caused by command line mismatches on either side of
  the link).

  3) In my reading I haven't found any restrictions on bouncing the
  connection between RO, RW and WO, which would wreak havoc on the
  file-descriptor model `encapsulate' is based on.

  For #1, I plan to just cope.  As long as the SYNs eventually arrive,
  things should work.  #2 is stickier, and I want to figure out an
  appropriate way to give a warning that a deadlock might be
  occurring.  #3 I will handle by ruthless application of RESET
  packets and messages to stderr.


  INCOMING BUFFER: store data from socket before it is sent to the
  subprocess.

  HEADER BUFFER: store headers from new SCP packets.

  OUTGOING BUFFER 1: store data from subprocess before it is
  encapsulated in an SCP packet.  NOTE: once data from a subprocess
  descriptor is stored in here, no other outgoing (subprocess-
  writeable) descriptors should be polled.  There is only one of these
  buffers.  This might result in a kind of starvation, so consider
  doing a round-robin thing with the outgoing subprocess descriptors
  that have data available.

  OUTGOING BUFFER 2: store the SCP packet while it's being written to
  the socket.  While this buffer is in use, no packets can be
  encapsulated, so data can accumulate in outgoing buffer 1.  This is
  good because then we have a bigger packet for next time.

  Note: we'll be able to read arbitrarily-sized SCP packets due to the
  design of the state machine.  We'll be limited in the size of
  packets we send by the size of outgoing buffer 1.  I can hear you
  saying "so what".

 */

#include <stdio.h>
#include	<errno.h>
extern int errno;		/* I hate the errno header file */
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/un.h>

#include "common.h"
#include "memmove.h"

#define EXITCODE_ARGS		127
#define EXITCODE_SYSCALLFAILED	126
#define EXITCODE_EXECFAILED	125
#define EXITCODE_SIGNAL		124
#define EXITCODE_PROTOCOL	123
#define EXITCODE_GOT_RESET	122

#define PACKETCODE_EOF		0
#define PACKETCODE_EOF_WAITING	1
#define PACKETCODE_EXITSTATUS	2

/*typedef	short Nshort;*/

#define SCP_SYN		0x80
#define SCP_FIN		0x40
#define SCP_PUSH	0x20
#define SCP_RESET	0x10

#define SESSION_ENCAP	70

/* encapsulate Control Protocol */
#define ECP_EOF		0
#define ECP_EXITCODE	1

/********************************************************************/

static int subproc=0;
static int verbose=0;

/********************************************************************/

static void usage()
{
    fprintf(stderr,"Usage : %s --fd n [ --verbose ] [ --subproc [ --infd n ] [ --outfd n ] [ --Duplex n ] [ --duplex n ] [ --DUPLEX n ] [ --prefer-local ] [ --prefer-remote ] [ --local-only ] [ --remote-only ] [ --client ] [ --server ] -[#n][v][s[in][on][dn][ion][oin][l][r][L][R]] <command> <args>  ]\n",
	    progname);
}

/********************************************************************/

static int remote_return_code = 0;
static int local_return_code = 0;
static int exitcode_warnings = 0;

static int	child_unreaped = 0;
static int	child_running = 0;
static void probe_child();

static int Im_server_p(sock_fd)
     int	sock_fd;
{
    struct sockaddr_in	me, him;
    int	len, i;

    len = sizeof(me);
    getsockname(sock_fd, (struct sockaddr*)&me, &len);

    len = sizeof(him);
    getpeername(sock_fd, (struct sockaddr*)&him, &len);

    i = memcmp(&me.sin_addr, &him.sin_addr, 4);
    if (i<0)
	return 1;
    else if (i>0)
	return 0;

    if (me.sin_port<him.sin_port)
	return 1;
    else
	return 0;
}

struct pipeline {
    int	child_fd;
    int pipe[2]; /* [0] is for the parent.  [1] is for the child */
    /* this is different from the pipe system call */
    enum pipe_polarity {
	READABLE,
	DUPLEX/*conditional initiate*/,
	DUPLEX_IO/*they initiate*/,
	DUPLEX_OI/*we initiate*/,
	WRITEABLE
    } code;

    /* once the connections are live we need extra info */
    int	session_id;		/* odd for servers, even for clients */
    int	syn_received;

    int bytes_left;		/* in the packet we've partially read */

    struct pipeline *next;	/* linked list */
    enum special_action {
	NOTHING, 
	CLOSE_TO_READ,		/* we got a FIN for this session_id */
	CLOSE_TO_RW,		/* we got a RESET for this session_id */
	CLOSE_TO_WRITE,		/* the child closed the descriptor */
    } specact;
};

struct pipeline	pl_bit_bucket;
struct pipeline	*pl_encapsulate_control;
struct pipeline *pipe_head=0;

int	session_id_;

/******************/

void add_subproc_fd(fd, code, sid)
     int	fd;
     enum pipe_polarity	code;
     int	sid;
{
    struct pipeline	*temp;
    temp = malloc(sizeof(*temp));
    temp->child_fd = fd;
    temp->code = code;
    temp->session_id = sid;
    temp->syn_received = 0;
    temp->bytes_left = 0;
    temp->specact = NOTHING;
    temp->next = 0;
    /* add to tail */
    {
	struct pipeline	**curr;
	for (curr = &pipe_head; *curr; curr = &( (*curr)->next ) )
	    ;
	*curr = temp;
    }
}

void add_control_channel()
{
    struct pipeline	*temp;
    temp = malloc(sizeof(*temp));
    temp->child_fd = -1;
    temp->pipe[0] = -10;
    temp->code = DUPLEX_OI;
    temp->session_id = SESSION_ENCAP;
    temp->syn_received = 0;
    temp->bytes_left = 0;
    temp->specact = NOTHING;
    temp->next = 0;
    /* add to tail */
    {
	struct pipeline	**curr;
	for (curr = &pipe_head; *curr; curr = &( (*curr)->next ) )
	    ;
	*curr = temp;
    }

    pl_encapsulate_control = temp;
}

void remove_pipeline_(pl)
     struct pipeline ** pl;
{
    struct pipeline *tmp;
    tmp = *pl;
    *pl = tmp->next;
    free(tmp);
}

void remove_pipeline(pl)
     struct pipeline * pl;
{
    struct pipeline	**curr;
    for (curr = &pipe_head;
	 *curr && *curr != pl;
	 curr = &( (*curr)->next ) )
	;
    if (0==*curr) {
	fprintf(stderr, "%s: greivous internal error.  attempt to delete unlisted pipeline from queue\n", progname);
	exit(1);
    }
    remove_pipeline_(curr);
}

/* build a buffer full of SYN packets.  The beginning of the buffer is
   the return value, and the length is stored into len_ret. */
static char * prepare_SYNs(len_ret)
     int	*len_ret;
{
    struct pipeline	*curr;
    int	size = 64;
    char *rval = (char*)malloc(size);
    *len_ret = 0;

    for (curr = pipe_head; curr; curr = curr->next) {
	switch (curr->code) {
	case WRITEABLE:
	case DUPLEX_IO:
	    /* we need to wait for the other side to send a SYN for
               these two */
	    /* curr->session_id = -1; it's already -1 */
	    break;
	case READABLE:
	case DUPLEX_OI:
	    if (*len_ret + 8 > size) {
		rval = (char*)realloc(rval, size *=2);
	    }
	    {
		char	*p = rval + *len_ret;
		if (curr->session_id<0) {
		    curr->session_id = session_id_;
		    /* XXX - gotta prevent stompage in later rev */
		    session_id_ += 2; /* odd for servers, even for clients */
		}
		p[0] = SCP_SYN;
		p[1] = (curr->session_id >>16 ) &0xff;
		p[2] = (curr->session_id >> 8 ) &0xff;
		p[3] = (curr->session_id >> 0 ) &0xff;
		p[4] = p[5] = p[6] = p[7] = 0;
		*len_ret += 8;
#ifdef DEBUG
		fprintf(stderr, "%s: prepared SYN for session 0x%06x\n",
			progname, curr->session_id);
#endif
	    }
	    break;
	case DUPLEX:		/* can't happen */
	    abort();
	    exit(1);
	}
    }

    return rval;
}

struct special_packet {
    char	*buf;
    int		len;

    struct special_packet	*next;
};

struct special_packet	*special_packet_queue = 0;


/* stuffs a special packet into the special-packet-queue for sending
   at the next possible moment.  sp and sp->buf must be heap-allocated. */
void prime_packet_queue_(sp)
     struct special_packet	*sp;
{
    struct special_packet	**curr;

    /* add to tail */
    for (curr = &special_packet_queue; *curr; curr = &(*curr)->next)
	;

    *curr = sp;
    sp->next = 0;
}

void prime_packet_queue(buf, len)
     char	*buf;
     int	len;
{
    struct special_packet	*tmp;
    tmp = malloc(sizeof(*tmp));
    tmp->buf = buf;
    tmp->len = len;
    prime_packet_queue_(tmp);
}

/********************************************************************/

void maybe_inject_special_packets(buf, len, size)
     char	*buf;
     int	*len;
     int	size;
{
    struct special_packet	*sp;
    if (*len>0)
	return;			/* can't fit one in */

    sp = special_packet_queue;

    if (sp ==0)
	return;			/* no special packets */

    if (sp->len > size) {
	memcpy(buf, sp->buf, size);
	memmove(sp->buf, sp->buf + size, sp->len -size);
	sp->len -= size;
	*len = size;
    } else {
	memcpy(buf, sp->buf, sp->len);
	*len = sp->len;

	special_packet_queue = sp->next;

	free(sp->buf);
	free(sp);
    }
}

void build_fd_lists(readfds, writefds,
		    curr_readable, curr_writeable,
		    sock_fd, maxfd,
		    ibuf_len, ibuf_size,
		    obuf_len, obuf_size,
		    obuf2_len, obuf2_off)
     fd_set	*readfds, *writefds;
     struct pipeline	*curr_readable, *curr_writeable;
     int	sock_fd, *maxfd;
     int	ibuf_len, ibuf_size;
     int	obuf_len, obuf_size;
     int	obuf2_len, obuf2_off;
{
	FD_ZERO(readfds);
	FD_ZERO(writefds);
	*maxfd = -1;
#define    MAX_FD(i)	if (*maxfd < (i)) *maxfd = (i)

	/* when can we write to a child? */
	if (ibuf_len>0 &&
	    curr_writeable->pipe[0]>=0 ) {
	    /* curr_writeable != 0 */
	    FD_SET(curr_writeable->pipe[0], writefds);
	    MAX_FD( curr_writeable->pipe[0] );
	}

	/* when can we read from the socket? */
	if ( curr_writeable == 0 
	     ||
	     (ibuf_len<ibuf_size &&
	      curr_writeable &&
	      curr_writeable->bytes_left > 0 )
	    ) {
#if 1
	    FD_SET(sock_fd, readfds);
	    MAX_FD(sock_fd);
#else
	    struct pipeline	*curr;
	    for (curr = pipe_head; curr; curr = curr ? curr->next : 0) {
		switch(curr->code) {
		case DUPLEX_IO:
		case DUPLEX_OI:
		case DUPLEX:	/* DUPLEX is not really possible */
		case WRITEABLE:
		    FD_SET(sock_fd, readfds);
		    MAX_FD(sock_fd);
		    curr = 0;	/* bail from for loop */
		    break;
		case READABLE:
		    /* we don't write to this one */
		    break;
		}
	    }
#endif
	}

	/* when can we read from a child? */
	if ( curr_readable == 0 ) {
	    /* select from ANY incoming child descriptor */
	    struct pipeline	*curr;
	    for (curr = pipe_head; curr; curr = curr->next) {
		if (curr->pipe[0]<0) continue;
		if ( curr->code == DUPLEX_IO &&
		     ! curr->syn_received)
		    continue;
		if ( curr->specact == CLOSE_TO_WRITE ) continue;
		switch(curr->code) {
		case READABLE:
		case DUPLEX_IO:
		case DUPLEX_OI:
		case DUPLEX:	/* DUPLEX is not really possible */
		    FD_SET(curr->pipe[0], readfds);
		    MAX_FD(curr->pipe[0]);
		    break;
		case WRITEABLE:
		    /* we don't read from this one */
		    break;
		}
	    }
	} else if (obuf_len < obuf_size &&
		   /* yes, the curr_readable could be WRITEABLE-only
		      when the child closes its descriptor, but we
		      haven't copied the bytes into the outgoing
		      socket buffer. In that case we don't want to
		      read from the descriptor. curr_readable will be
		      zeroed when the bytes get copied into the
		      outgoing socket buffer */
		   curr_readable->code != WRITEABLE &&
		   !( curr_readable->specact == CLOSE_TO_WRITE
		      || curr_readable->specact == CLOSE_TO_RW)
		   ) {
	    FD_SET(curr_readable->pipe[0], readfds);
	    MAX_FD(curr_readable->pipe[0]);
	}

	/* when can we write to the socket? */
	if (obuf2_len>obuf2_off) {
	    FD_SET(sock_fd, writefds);
	    MAX_FD(sock_fd);
	}
}

void build_packet_header (buf, flags, session, length)
     char	*buf;		/* length 8 */
     int	flags, session;
     unsigned	length;
{
    buf[0] = flags;
    buf[1] = session>>16;
    buf[2] = session>>8;
    buf[3] = session;
    buf[4] = length>>24;
    buf[5] = length>>16;
    buf[6] = length>>8;
    buf[7] = length;
}

void send_reset(struct pipeline *pl)
{
    char	*buf = malloc(8);
    build_packet_header(buf, SCP_RESET, pl->session_id, 0);
    prime_packet_queue(buf, 8);
}

/* returns number of bytes read into buf at offset *buf_len, limited by
   buf_size.  An uncrecoverable error aborts the program */
int read_from_child(pl, buf, buf_len, buf_size)
     struct pipeline	*pl;
     char	*buf;
     int	*buf_len;
     int	buf_size;
{
    int	rval;

#if DEBUG>1
    fprintf(stderr, "%s: read(in[c%d], buf+%d, %d-%d)",
	    progname, pl->child_fd, *buf_len, buf_size, *buf_len);
#endif
    rval = read(pl->pipe[0], buf+*buf_len, buf_size - *buf_len);
#if DEBUG>1
    fprintf(stderr, "=%d;\n", rval);
#endif
    if (rval == 0) {
#ifdef DEBUG
	fprintf(stderr, "%s: closing in[%d=c%d]\n", progname, pl->pipe[0], pl->child_fd);
#endif
	pl->specact = CLOSE_TO_WRITE;
    } else if (rval<0) {
	if (errno == EINTR) {
	    return 0;		/* no biggie, retry later */
	} else {
	    fprintf(stderr, "%s: error during read(in[%d],,).  Aborting.  ",
		    progname, pl->pipe[0]);
	    perror("");
	    exit(EXITCODE_SYSCALLFAILED);
	}
    } else {
	*buf_len += rval;
    }
    return rval;
}

/* returns number of bytes written from buf, limited by (*buf_len).
   It takes care of moving the bytes in the buffer for the case of an
   incomplete write.  An uncrecoverable error aborts the program */
int write_to_child(pl, buf, buf_len)
     struct pipeline	*pl;
     char	*buf;
     int	*buf_len;
{
    int	rval;

#if DEBUG>1
    fprintf(stderr, "%s: write(out[%d/c%d], buf, %d)",
	    progname, pl->pipe[0], pl->child_fd, *buf_len);
#endif
    rval = write(pl->pipe[0], buf, *buf_len);
#if DEBUG>1
    fprintf(stderr, "=%d;\n", rval);
#endif
    if (rval == 0) {
#ifdef DEBUG
	fprintf(stderr, "%s: Inexplicable!  write(out[%d],,%d) returns 0", progname, pl->pipe[0], *buf_len);
#endif
    } else if (rval<0) {
	if (errno == EINTR) {
	    return 0;		/* no biggie, retry later */
	} else if (errno == EPIPE) {
	    /* DOH! fake it */
	    fprintf(stderr, "%s: EPIPE while writing to child fd %d\n", progname, pl->child_fd);
	    exitcode_warnings = EXITCODE_GOT_RESET;
	    pl->specact = CLOSE_TO_READ;
	    *buf_len = 0;
	    send_reset(pl);
	    return 0;		/* this is NOT a normal thing */
	} else {
	    fprintf(stderr, "%s: error during write(out[%d],,).  Aborting.  ",
		    progname, pl->pipe[0]);
	    perror("");
	    exit(EXITCODE_SYSCALLFAILED);
	}
    } else {
	memmove(buf, buf+rval, *buf_len - rval);
	*buf_len -= rval;
    }
    return rval;
}

/* updates obuf_off to reflect how many bytes it copied from obuf to fd */
void write_to_socket(fd, obuf, obuf_off, obuf_len)
     int	fd;
     char	*obuf;
     int	*obuf_off;
     int	obuf_len;
{
    int	rval;

#if DEBUG>1
    fprintf(stderr, "%s: write(sock_fd, buf+%d, %d-%d)",
	    progname, *obuf_off, obuf_len, *obuf_off);
#endif
    rval = write(fd, obuf+ *obuf_off, obuf_len-*obuf_off);
#if DEBUG>1
    fprintf(stderr, "=%d;\n", rval);
#endif
    if (rval==0) {
	fprintf(stderr, "%s: Inexplicable!  write(sock_fd,,%d) returns 0\n",
		progname, obuf_len-*obuf_off);
	exit(EXITCODE_SYSCALLFAILED);
    } else if (rval<0) {
	if (errno!=EINTR) {
	    fprintf(stderr, "%s: error during write(sock_fd,,%d).  Aborting.  ",
		    progname, obuf_len-*obuf_off);
	    perror("");
	    exit(EXITCODE_SYSCALLFAILED);
	}
	/* never mind. try again later */
    } else {
	*obuf_off += rval;
    }
}

/* read bytes from the socket */

void read_from_socket(fd, pl, buf, buf_len, buf_size)
     int	fd;
     struct pipeline	*pl;
     char	*buf;
     int	*buf_len, buf_size;
{
    int	desired_read = buf_size - *buf_len;
    int	rval;

    if (pl && desired_read > pl->bytes_left) {
	desired_read = pl->bytes_left;
    }
#if DEBUG>1
    fprintf(stderr, "%s: read(sock_fd, buf+%d, %d)",
	    progname, *buf_len, desired_read);
#endif
    rval = read(fd, buf+*buf_len, desired_read);
#if DEBUG>1
    fprintf(stderr, "=%d;\n", rval);
#endif
    if (rval==0) {
	fprintf(stderr, "%s: encapsulation protocol error reading socket.  Socket closed prematurely by remote end.\n", progname);
	/*sock_closed = 1;*/
	exit(EXITCODE_PROTOCOL);
    } else if (rval<0) {
	if (errno!=EINTR) {
	    fprintf(stderr, "%s: error during read(sock_fd,,%d).  Aborting.  ",
		    progname, desired_read);
	    perror("");
	    exit(EXITCODE_SYSCALLFAILED);
	}
	/* never mind. try again later */
    } else {
	*buf_len += rval;

	if (pl) pl->bytes_left -= rval;
    }

    if (pl == &pl_bit_bucket) {
	/* throw away the bytes */
#ifdef DEBUG
	fprintf(stderr, "%s: %d bytes into the bit bucket\n", progname, *buf_len);
#endif
	*buf_len = 0;
    }
}

void interpret_scp_header(header, pl)
     unsigned char	*header;	/* size 8 */
     struct pipeline	**pl;
{
    int	flags, session_id;
    unsigned int	length;
    struct pipeline	*found = 0;

    flags = header[0];
    session_id = ( header[1] << 16 ) | ( header[2] << 8 ) | header[3];
    length = ( header[4] << 24 ) | ( header[5] << 16 ) | ( header[6] << 8 ) | header[7];

#ifdef DEBUG
    fprintf(stderr, "%s: packet. flags=0x%02x, session=0x%06x, length=%d\n",
	    progname, flags, session_id, length);
#endif

    if ( (flags & (SCP_SYN|0xf)) == SCP_SYN) {
	struct pipeline	*curr;
	found = 0;
	/* see if there's one descriptor that's waiting for this
           particular session_id */
	for (curr = pipe_head; curr && !found; curr = curr->next ) {
	    if (curr->syn_received)
		continue;
	    switch(curr->code) {
	    case WRITEABLE: case DUPLEX_IO: case DUPLEX_OI: case DUPLEX:
		/* DUPLEX not actually possible */
		if (curr->session_id == session_id) {
		    found = curr;
		    found->syn_received = 1;
		}
		break;
	    case READABLE:
		break;		/* like, whatever */
	    }
	}
	if (!found) for (curr = pipe_head; curr && !found; curr = curr->next ) {
	    if (curr->syn_received)
		continue;
	    switch(curr->code) {
	    case WRITEABLE: case DUPLEX_IO: case DUPLEX_OI:
	    case DUPLEX: /* DUPLEX not actually possible */
		if (curr->session_id<0) {
		    found = curr;
		    found->session_id = session_id;
		    found->syn_received = 1;
		}
		break;
	    case READABLE:
		break;		/* like, whatever */
	    }
	}
	if (!found) {
	    fprintf(stderr, "%s: Warning! discarding SYN for session_id=0x%06x, to the bit bucket!\n", progname, session_id);
	    found = &pl_bit_bucket;
	} else {
#ifdef DEBUG
	    fprintf(stderr, "%s: received SYN for session 0x%06x", progname, session_id);
#endif
	    if (found->code == DUPLEX_IO) {
		char	*buf = malloc(8);
		build_packet_header(buf, SCP_SYN, session_id, 0);
		prime_packet_queue(buf, 8);
#ifdef DEBUG
		fprintf(stderr, "; sending one back");
#endif
	    }
#ifdef DEBUG
	    putc('\n', stderr);
#endif
	}
	/* fall through and accept any data that might be in the packet */
    } else if ( (flags & (SCP_FIN|0xf)) == SCP_FIN) {
	struct pipeline	*curr;
	for (curr = pipe_head; curr && !found; curr = curr->next ) {
	    if (! curr->syn_received)
		continue;
	    if (curr->session_id != session_id)
		continue;

	    switch(curr->code) {
	    case WRITEABLE: case DUPLEX_IO: case DUPLEX_OI:
	    case DUPLEX: /* DUPLEX not actually possible */
		found = curr;
		found->specact = CLOSE_TO_READ;
		break;
	    case READABLE:
		break;		/* like, whatever */
	    }
	}
	if (!found) {
	    fprintf(stderr, "%s: Warning! discarding FIN for session_id=0x%06x, to the bit bucket!\n", progname, session_id);
	    found = &pl_bit_bucket;
	} else {
#ifdef DEBUG
	    fprintf(stderr, "%s: received FIN for session 0x%06x\n", progname, session_id);
#endif
	}
    } else if ( (flags & (SCP_PUSH|0xf)) == SCP_PUSH) {
	/* like, whatever */
    } else if ( (flags & (SCP_RESET|0xf)) == SCP_RESET) {
	struct pipeline	*curr;
	/* why are they doing this to me? */
	fprintf(stderr, "%s: RESET received for session_id=%d.  closing descriptor bidirectionally\n", progname, session_id);
	for (curr = pipe_head; curr && !found; curr = curr->next ) {
	    if (! curr->syn_received)
		continue;
	    if (curr->session_id != session_id)
		continue;

	    switch(curr->code) {
	    case READABLE: case DUPLEX_IO: case DUPLEX_OI:
	    case DUPLEX: /* DUPLEX not actually possible */
	    case WRITEABLE:
		found = curr;
		found->specact = CLOSE_TO_RW;
		exitcode_warnings = EXITCODE_GOT_RESET;
		break;
	    }
	}
	if (!found) {
	    fprintf(stderr, "%s: Warning! discarding RESET for session_id=0x%06x, to the bit bucket!\n", progname, session_id);
	    found = &pl_bit_bucket;
	}
    } else if (flags != 0) {
	fprintf(stderr, "%s: Warning! funky flags 0x%02x on packet, to the bit bucket!\n", progname, flags);
	found = &pl_bit_bucket;
    } else {
	struct pipeline	*curr;
	for (curr = pipe_head; curr && !found; curr = curr->next ) {
	    if (curr->session_id == session_id) {
		found = curr;
		break;
	    }
	}
	if (!found) {
	    fprintf(stderr, "%s: Warning! discarding data packet for session_id=0x%06x, to the bit bucket!\n", progname, session_id);
	    found = &pl_bit_bucket;
	}
    }

    if (length) {

	if (found->code == READABLE) {
	    fprintf(stderr, "%s: Got data for session 0x%06x I can't write to.  RESETting\n", progname, session_id);
	    send_reset(found);
	    found = &pl_bit_bucket;
	}

	*pl = found;
	(*pl)->bytes_left = length;
    }
}

void handle_control_message(buf, len)
     char	*buf;
     int	len;
{
    if (len<1) {
	fprintf(stderr, "%s: Internal Error: control message length <1. \n", progname);
	exit(1);
    }

    switch(buf[0]) {
    case ECP_EOF:
	if (len!=1) {
	    fprintf(stderr, "%s: Protocol Error: control message[0] length(%d) !=1. \n", progname, len);
	    exit(EXITCODE_PROTOCOL);
	}
#ifdef DEBUG
	fprintf(stderr, "%s: Got EOF from remote.\n", progname);
#endif

	if (pl_encapsulate_control->code == WRITEABLE) {
	    remove_pipeline(pl_encapsulate_control);
	    pl_encapsulate_control = 0;
	} else {
	    pl_encapsulate_control->code = READABLE;
	}

	break;

    case ECP_EXITCODE:
	if (len!=2) {
	    fprintf(stderr, "%s: Protocol Error: control message[1] length(%d) !=2. \n", progname, len);
	    exit(EXITCODE_PROTOCOL);
	}
	remote_return_code = (unsigned char )buf[1];
#ifdef DEBUG
	fprintf(stderr, "%s: remote exit status: %d\n",
		progname, remote_return_code);
#endif

	if (pl_encapsulate_control->code == WRITEABLE) {
	    remove_pipeline(pl_encapsulate_control);
	    pl_encapsulate_control = 0;
	} else {
	    pl_encapsulate_control->code = READABLE;
	}

	break;

    default:
	fprintf(stderr, "%s: Protocol Error: unknown control message [%d]. \n", progname, (unsigned char)buf[0]);
	exit(EXITCODE_PROTOCOL);
	break;
    }
}

void perform_special_actions(ibuf_len, obuf_len, curr_readable, curr_writeable)
     int	*ibuf_len;
     int	*obuf_len;
     struct pipeline	**curr_readable, **curr_writeable;
{
    struct pipeline	**curr;
    for (curr = &pipe_head;
	 *curr;
	 curr = curr ? (&(*curr)->next) : &pipe_head ) {
	switch ((*curr)->specact) {
	case NOTHING:
	    break;
	case CLOSE_TO_READ:
	    /* got a FIN packet */
	    if (*ibuf_len>0)
		break;		/* can't drop it yet */
	    if (*curr_writeable == *curr
		|| *curr_readable == *curr) {
		/* fprintf(stderr, "%s: weird, special action CLOSE_TO_READ applied to a curr_ pipeline %lx\n", progname, (long)*curr); */
		break;
	    }
	    switch((*curr)->code) {
	    case WRITEABLE:
#ifdef DEBUG
		fprintf(stderr, "%s: closing W child fd %d\n", progname, (*curr)->child_fd);
#endif
		close((*curr)->pipe[0]);
		remove_pipeline_(curr);
		curr = 0;	/* start scanning from the beginning */
		break;
	    case DUPLEX:	/* DUPLEX not actually possible */
	    case DUPLEX_IO:
	    case DUPLEX_OI:
#ifdef DEBUG
		fprintf(stderr, "%s: converting child fd %d to child-write-only\n", progname, (*curr)->child_fd);
#endif
		(*curr)->specact = NOTHING;
		(*curr)->code = READABLE;
		shutdown((*curr)->pipe[0], 1);

		break;
	    case READABLE:
		fprintf(stderr, "%s: internal error, attempt to CLOSE_TO_READ on a READABLE descriptor\n", progname);
		exit(1);
	    }
	    break;
	case CLOSE_TO_RW:
	    /* got a RESET packet.  get drastic */
	    if ( (*curr)->bytes_left >0) {
		fprintf(stderr, "%s: Freaky, %d bytes_left in CLOSE_TO_RW channel\n", progname, (*curr)->bytes_left);
		break;		/* can't dump it without corrupting the stream */
	    }
	    if (*curr_readable == *curr) {
		*obuf_len = 0;
		*curr_readable = 0;
	    }
	    if (*curr_writeable == *curr) {
		*ibuf_len = 0;
		*curr_writeable = 0;
	    }
#ifdef DEBUG
	    fprintf(stderr, "%s: RESETting child fd %d\n", progname, (*curr)->child_fd);
#endif
	    {
		struct pipeline *temp = *curr;
		close(temp->pipe[0]);
		*curr = temp->next;
		free(temp);
	    }
	    curr = 0;	/* start scanning from the beginning */
	    break;
	case CLOSE_TO_WRITE:
	    /* child closed the descriptor */
	    if (*curr_writeable == *curr
		|| *curr_readable == *curr) {
		/*fprintf(stderr, "%s: weird, special action CLOSE_TO_WRITE applied to a curr_ pipeline %lx\n", progname, (long)*curr);*/
		break;
	    }

	    {
	      char	*buf;
	      int	len;
	      len = 8;
	      buf = malloc(len);
	      build_packet_header(buf, SCP_FIN, (*curr)->session_id, 0);
	      prime_packet_queue(buf, len);
#ifdef DEBUG
	      fprintf(stderr, "%s: sending FIN for session 0x%06x\n", progname, (*curr)->session_id);
#endif
	    }

	    switch((*curr)->code) {
	    case READABLE:
#ifdef DEBUG
		fprintf(stderr, "%s: closing R child fd %d\n", progname, (*curr)->child_fd);
#endif
		{
		    struct pipeline *temp = *curr;
		    temp = *curr;
		    close(temp->pipe[0]);
		    *curr = temp->next;
		    free(temp);
		}
		curr = 0;	/* start scanning from the beginning */
		break;
	    case DUPLEX:	/* DUPLEX not actually possible */
	    case DUPLEX_IO:
	    case DUPLEX_OI:
#ifdef DEBUG
		fprintf(stderr, "%s: converting child fd %d to child-read-only\n", progname, (*curr)->child_fd);
#endif
		(*curr)->specact = NOTHING;
		(*curr)->code = WRITEABLE;
		shutdown((*curr)->pipe[0], 0);

		break;
	    case WRITEABLE:
		fprintf(stderr, "%s: internal error, attempt to CLOSE_TO_WRITE on a WRITEABLE descriptor\n", progname);
		exit(1);
	    }

	    break;
	}
    }
}

#define BUF_SIZE	4096

static void main_io_loop(sock_fd)
     int	sock_fd;
{
    char incoming_buf[BUF_SIZE]; /* read from socket, will write to child */
    char outgoing_buf[BUF_SIZE]; /* read from child, will packetize into : */
    char outgoing2_buf[BUF_SIZE+8]; /* packet buf, will write to socket */

    /* bytes in the buffer to child */
    /* this is nonzero only if curr_writeable is nonNULL */
    int	incoming_len=0;

    /* this is nonzero only if curr_readable is nonNULL */
    /* bytes in the buffer from child */
    int	outgoing_len=0;

    /* bytes in the buffer to socket */
    /* this is independent of the curr_ variables */
    int	outgoing2_len=0;
    int	outgoing2_off=0;

    /* for reading SCP headers */
    char	header_buf[8];
    int		header_len;

    fd_set	readfds, writefds;
    int	maxfd;

    struct pipeline	*curr_readable=0; /* child descriptor we're reading */
    struct pipeline	*curr_writeable=0; /* child descriptor we're writing */

    while (1) {
	int	rval;

	build_fd_lists (
			&readfds, &writefds,
			curr_readable, curr_writeable,
			sock_fd, &maxfd,
			incoming_len, sizeof(incoming_len),
			outgoing_len, sizeof(outgoing_buf),
			outgoing2_len, outgoing2_off);

	if (
#if 0
	    maxfd<0
	    || 
#else
	    (!subproc || !child_unreaped)
	    &&
#endif
	    (0==pipe_head && 0 == outgoing2_len
		&& special_packet_queue == 0)
	    ) {
	    if (incoming_len != 0 ||
		outgoing_len != 0 ||
		outgoing2_len != 0) {
		fprintf(stderr, "%s: leftover bytes in buffers at end of encapsulation.  That is bad because it means Bob made a logic error in his code.\n", progname);
	    }
	    break;		/* run out of things to do */
	}

	if (maxfd>0) {
	    struct timeval	tv;
	    tv.tv_sec = 0;
	    tv.tv_usec = 500000;
	    rval = select(maxfd+1, &readfds, &writefds, (fd_set*)0, &tv);

	    if (rval<0) {
		if (errno == EINTR) {
		    continue;
		} else {
		    fprintf(stderr, "%s: error during select: ", progname);
		    perror("");
		    exit(EXITCODE_SYSCALLFAILED);
		}
		/* NOTREACHED */
	    } else if (rval==0) {
		/* got a timeout */

	    }

	    /* read bytes from the child */
	    {
		struct pipeline	*curr;
		for (curr = pipe_head; curr; curr = curr->next) {
		    switch (curr->code) {
		    case READABLE:
		    case DUPLEX_IO:
		    case DUPLEX_OI:
		    case DUPLEX:
			if (curr->pipe[0]>=0 &&
			    FD_ISSET(curr->pipe[0], &readfds) &&
			    (curr==curr_readable || 0==curr_readable)) {
			    if (read_from_child(curr, outgoing_buf, &outgoing_len,
						sizeof(outgoing_buf)) )
				curr_readable = curr;
			}
			break;
		    case WRITEABLE:
			break;
		    }
		}
	    }

	    /* write bytes to the child */
	    if (curr_writeable && incoming_len>0 &&
		curr_writeable->pipe[0] >=0 &&
		FD_ISSET(curr_writeable->pipe[0], &writefds) ) {
		write_to_child(curr_writeable, incoming_buf, &incoming_len);
		if (incoming_len<1 && curr_writeable->bytes_left <1) {
		    curr_writeable = 0;
		}
	    }


	    /* write bytes to the socket */
	    if (FD_ISSET(sock_fd, &writefds)) {
		write_to_socket(sock_fd, outgoing2_buf,
				&outgoing2_off, outgoing2_len);
		if (outgoing2_off >= outgoing2_len) {
		    outgoing2_len = 0;
		    outgoing2_off = 0;
		}
	    }

	    /* read bytes from the socket */
	    if (FD_ISSET(sock_fd, &readfds)) {

		if (curr_writeable) {
		    read_from_socket(sock_fd, curr_writeable,
				     incoming_buf, &incoming_len,
				     sizeof(incoming_buf) );
		} else {
		    read_from_socket(sock_fd, (struct pipeline *)0,
				     header_buf, &header_len,
				     sizeof(header_buf));

		    if (header_len==sizeof(header_buf)) {
			interpret_scp_header(header_buf, &curr_writeable);
			header_len = 0;
		    }
		}
	    }
	}

	maybe_inject_special_packets(outgoing2_buf, &outgoing2_len,
				     sizeof(outgoing2_buf));

	if (outgoing2_len==0 && outgoing_len>0) {
	    build_packet_header(outgoing2_buf, 0, curr_readable->session_id,
				outgoing_len);
	    memcpy(outgoing2_buf + 8, outgoing_buf, outgoing_len);
	    outgoing2_len = outgoing_len + 8;
	    outgoing_len = 0;
	}

	if (curr_readable && outgoing_len==0) {
	    curr_readable = 0;
	}

	if (curr_writeable
	    && curr_writeable->bytes_left == 0
	    && curr_writeable == pl_encapsulate_control
	    && incoming_len>0) {
	    handle_control_message(incoming_buf, incoming_len);
	    incoming_len = 0;
	    curr_writeable = 0;
	}

	if (subproc) {
	    if (!child_running && child_unreaped) {
		probe_child();
	    }
	} else {
	    if (pipe_head != 0 &&
		pipe_head == pl_encapsulate_control &&
		pipe_head->next == 0 &&
		pipe_head->code != WRITEABLE) {
		/* all channels are closed */
		char	*buf;
		int	len = 1;
#ifdef DEBUG
		fprintf(stderr, "%s: sending EOF\n",
			progname);
#endif
		buf = malloc(8+len);
		build_packet_header(buf, 0, SESSION_ENCAP, len);
		buf[8] = ECP_EOF;
		prime_packet_queue(buf, 8+len);

		if (pl_encapsulate_control->code == READABLE) {
		    remove_pipeline(pl_encapsulate_control);
		    pl_encapsulate_control = 0;
		} else {
		    pl_encapsulate_control->code = WRITEABLE;
		}
	    }
	}

	perform_special_actions(&incoming_len, &outgoing_len,
				&curr_readable, &curr_writeable);
    }
}

/********************************************************************/

static int	childpid = -1;

static void waitonchild()
{
    /* got a SIGCHILD.
       It must be that: */
    child_running = 0;
}

static void probe_child()
{
    if (child_running || !child_unreaped)
	return;

    if ( 0>=wait(&local_return_code)) {
	fprintf(stderr, "%s: wait returned error or zero: ", progname);
	perror("");
	exit(EXITCODE_SYSCALLFAILED);
    }
    if (!WIFEXITED(local_return_code))
	local_return_code = EXITCODE_SIGNAL;
    else
	local_return_code = WEXITSTATUS(local_return_code);

    {
	char	*buf;
	int	len = 2;
#ifdef DEBUG
	fprintf(stderr, "%s: sending exit status %d\n",
		progname, local_return_code);
#endif
	buf = malloc(8+len);
	build_packet_header(buf, 0, SESSION_ENCAP, len);
	buf[8] = ECP_EXITCODE;
	buf[8+1] = local_return_code;
	prime_packet_queue(buf, len+8);

	if (pl_encapsulate_control->code == READABLE) {
	    remove_pipeline(pl_encapsulate_control);
	    pl_encapsulate_control = 0;
	} else {
	    pl_encapsulate_control->code = WRITEABLE;
	}
    }

    child_unreaped = 0;
}

static void spawn_child(cmd, sockfd)
     char	**cmd;
     int	sockfd;
{
    struct pipeline	*curr;

    signal(SIGCHLD,waitonchild);
    child_running = 1;		/* well, not yet. */
    child_unreaped = 1;

    /* We're about to allocate a big stack of descriptors.  Let's make
       sure we don't step on on our own toes.  Dup descriptor 0 onto
       each of the descriptors so that our allocations won't get one
       of them.  If you don't have a descriptor 0, then you're a FREAK! */
    /* Uhoh.  Some of them may already be connected to the parent.
       Bob attaches some funky things in funky places. */
    /* The way I tell if a descriptor has been allocated or not is I
       select() on it.  If I get EBADF, the descriptor has not been
       allocated and I can stomp on it before the fork.  If I don't,
       then I won't stomp on it till after the fork. */
    {
	for (curr = pipe_head; curr; curr = curr->next) {
	    if (curr->child_fd<0)
		continue;
	    if (!valid_descriptor(curr->child_fd))
		dup2(0, curr->child_fd); /* "reserve" it */
	}
    }

    for (curr = pipe_head; curr; curr = curr->next) {
	if (curr->child_fd<0)
	    continue;		/* skip this special channel */
	switch (curr->code) {
	case READABLE:
	case WRITEABLE:
	    if (pipe(curr->pipe) !=0) {
		fprintf(stderr, "%s: totally failed to pipe(2): ", progname);
		perror("");
		exit (EXITCODE_SYSCALLFAILED);
	    }
	    break;
	case DUPLEX:
	case DUPLEX_IO:
	case DUPLEX_OI:
	    if (0!=socketpair(AF_UNIX, SOCK_STREAM, 0/*let it choose*/,
			      curr->pipe)) {
		fprintf(stderr, "%s: totally failed to socketpair(2): ",
			progname);
		perror("");
		exit (EXITCODE_SYSCALLFAILED);
	    }
	}
	if (curr->code == WRITEABLE) {
	    /* we need to invert the polarity for this case, eh, Geordi? */
	    int	t;
	    t = curr->pipe[0];
	    curr->pipe[0] = curr->pipe[1];
	    curr->pipe[1] = t;
	}
    }

    childpid = fork();
    if (childpid<0) {
	fprintf(stderr, "%s: unable to fork: ", progname);
	perror("");
	/* I would clear child_running, but, look at the next line */
	exit(EXITCODE_SYSCALLFAILED);
    }

    /* now there's a child running (assuming no race conditions, which
       is why I set it up above and not here.  I'm stupid, but
       paranoid). */

    if (childpid==0) {
	/* child */
	close(sockfd);		/* can't have the child accidentally
				   stomping on our conversation. */
	for (curr = pipe_head; curr; curr = curr->next) {
	    if (curr->child_fd<0)
		continue;		/* skip this special channel */
	    close(curr->pipe[0]);
	    dup2(curr->pipe[1], curr->child_fd);
	    close(curr->pipe[1]);
	}

	execvp(*cmd, cmd);
	fprintf(stderr, "%s: Unable to exec %s: ", progname, *cmd);
	perror("");

	exit(EXITCODE_EXECFAILED);

    } else {
	/* parent */
	for (curr = pipe_head; curr; curr = curr->next) {
	    if (curr->child_fd<0)
		continue;		/* skip this special channel */
	    close(curr->pipe[1]);
	}
    }
}

static void rig_single()
{
    struct pipeline	*curr;
    /* Dear mother of god.  I have to invert the polarity of all the
       pipes.  How the hell am I going to explain this in the manual
       page? */

    for (curr = pipe_head; curr; curr = curr->next) {
	switch (curr->code) {
	case READABLE:
	    curr->code = WRITEABLE;
	    break;
	case WRITEABLE:
	    curr->code = READABLE;
	    break;
	default:
	    /* I don't need to diddle the duplex cases really */
	    break;
	}

	/* so that select and I/O will work */
	curr->pipe[0] = curr->child_fd;
	curr->pipe[1] = -1;
    }
    
}

/********************************************************************/

static int scan_flag_numeric_fd(s, fdp)
    char *s;
    int	*fdp;
{
    int	n;
    if (1 != sscanf(s, "%i%n", fdp, &n)) {
	fprintf(stderr, "%s: parse error in file descriptor list at '%s'\n", progname, s);
	exit(EXITCODE_ARGS);
    }
    return n;
}

/********************************************************************/

enum mergereturns_ {
    UNINITIALIZED,
    PREFER_LOCAL,		/* if both local and remote processes
				   error, return the local code */
    PREFER_REMOTE,		/* if both local and remote processes
				   error, return the remote code.  */
    LOCAL_ONLY,			/* return the exit code of the local
				   process, ignoring the return code
				   of the remote process. */
    REMOTE_ONLY,		/* return the exit code of the remote
				   process, ignoring the return code
				   of the local process. */
} merge_returns = UNINITIALIZED;

static int	sockfd = -1;
static int	im_server = -1;

int main (argc,argv)
     int argc;
     char ** argv;
     
{
    set_progname(argv[0]);

#if 0
    if (sizeof(Nshort) != 2) {
	fprintf(stderr, "%s: greivous porting error. sizeof(Nshort) == %d, not 2.\n",
		progname, sizeof(Nshort));
	exit(EXITCODE_ARGS);
    }
#endif

    while (argc>1) {
	char *arg = argv[1];
	if (arg[0] != '-') {
	    break;
	}
	arg++;
	if (arg[0] == '-') {
	    arg++;
	    if (0==strcmp(arg, "verbose")) {
		verbose = 1;
		argv++; argc--;
	    } else if (0==strcmp(arg, "fd")) {
		argv++; argc--;
		if (argc<2) {
		    fprintf(stderr, "%s: --fd requires file number for socket.\n",
			    progname);
		    usage();
		    exit(EXITCODE_ARGS);
		} else if (sockfd>=0) {
		    fprintf(stderr, "%s: --fd can only be specified once\n",
			    progname);
		    usage();
		    exit(EXITCODE_ARGS);
		} else  {
		    sockfd = atoi(argv[1]);
		    argv++; argc--;
		}
	    } else if (0==strcmp(arg, "subproc")) {
		subproc = 1;
		argv++; argc--;
	    } else if (0==strcmp(arg, "infd") ||
		       0==strcmp(arg, "outfd") ||
		       0==strcmp(arg, "duplex") ||
		       0==strcmp(arg, "Duplex") ||
		       0==strcmp(arg, "DUPLEX")) {
		long	fd, sid;
		char	*p;
		int	err = 0;
		enum pipe_polarity	pol = -1;
		argv++; argc--;
		if (argc<2 || argv[1][0] == 0) {
		    err = 1;
		} else {
		    fd = strtol(argv[1], &p, 0);
		    if (*p==0) {
			sid = -1;
		    } else if (p[0] == '=' && p[1] != 0) {
			sid = strtol(p+1, &p, 0);
			if (*p != 0) {
			    err = 1;
			}
		    } else {
			err = 1;
		    }
		}
		if (err) {
		    fprintf(stderr, "%s: %s requires descriptor number as argument\n", progname, arg-2);
		    usage();
		    exit(EXITCODE_ARGS);
		}

		{
		  if      (0==strcmp(arg, "infd"))   pol = WRITEABLE;
		  else if (0==strcmp(arg, "outfd"))  pol = READABLE;
		  else if (0==strcmp(arg, "duplex")) pol = DUPLEX_IO;
		  else if (0==strcmp(arg, "Duplex")) pol = DUPLEX;
		  else if (0==strcmp(arg, "DUPLEX")) pol = DUPLEX_OI;
		}
		if (pol == -1) {
		    fprintf(stderr, "%s: code error, polarity uninitialized %s:%d\n", progname, __FILE__, __LINE__);
		    abort();
		}
		add_subproc_fd(fd, pol, -1);
		argv++; argc--;
	    } else if (0==strcmp(arg, "prefer-local")||
		       0==strcmp(arg, "preferlocal")) {
		merge_returns = PREFER_LOCAL;
		argv++; argc--;
	    } else if (0==strcmp(arg, "prefer-remote")||
		       0==strcmp(arg, "preferremote")) {
		merge_returns = PREFER_REMOTE;
		argv++; argc--;
	    } else if (0==strcmp(arg, "local-only")||
		       0==strcmp(arg, "localonly")) {
		merge_returns = LOCAL_ONLY;
		argv++; argc--;
	    } else if (0==strcmp(arg, "remote-only")||
		       0==strcmp(arg, "remoteonly")) {
		merge_returns = REMOTE_ONLY;
		argv++; argc--;
	    } else if (0==strcmp(arg, "client")) {
		im_server = 0;
		argv++; argc--;
	    } else if (0==strcmp(arg, "server")) {
		im_server = 1;
		argv++; argc--;
	    } else {
		/* unknown -- flag.  Assume it's a command :) */
		break;
	    }
	} else {
	    /* it's a set of single dash flags. */
	    do { switch (arg[0]) {
	    case '#':
		arg += scan_flag_numeric_fd(arg+1, &sockfd);
		break;
	    case 'v':
		verbose = 1;
		break;
	    case 's':
		subproc=1;
		break;
	    case 'i':
		if (arg[1] == 'o') {
		    int	fd;
		    arg += scan_flag_numeric_fd(arg+2, &fd);
		    add_subproc_fd(fd, DUPLEX_IO, -1);
		} else {
		    int	fd;
		    arg += scan_flag_numeric_fd(arg+1, &fd);
		    add_subproc_fd(fd, WRITEABLE, -1);
		}
		break;
	    case 'o':
		if (arg[1] == 'i') {
		    int	fd;
		    arg += scan_flag_numeric_fd(arg+2, &fd);
		    add_subproc_fd(fd, DUPLEX_OI, -1);
		} else {
		    int	fd;
		    arg += scan_flag_numeric_fd(arg+1, &fd);
		    add_subproc_fd(fd, READABLE, -1);
		}
		break;
	    case 'd':
		{
		    int	fd;
		    arg += scan_flag_numeric_fd(arg+1, &fd);
		    add_subproc_fd(fd, DUPLEX, -1);
		}
		break;
	    case 'l':
		merge_returns = PREFER_LOCAL;
		break;
	    case 'r':
		merge_returns = PREFER_REMOTE;
		break;
	    case 'L':
		merge_returns = LOCAL_ONLY;
		break;
	    case 'R':
		merge_returns = REMOTE_ONLY;
		break;
	    case 0:
		fprintf(stderr, "%s: blank compact flag.\n", progname);
		/* fall through */
	    default:
		fprintf(stderr, "%s: Unknown compact flag beginning %s\n", progname, arg);
		usage();
		exit (EXITCODE_ARGS);
	    } arg++;
	    } while (arg[0]);

	    argv++;
	    argc--;
	}
    }

    /* argv+1 points to an unrecognized flag that must be the
       subprocess cmd and arguments. */

    if (argc>1 && !subproc) {
	fprintf(stderr, "%s: Unknown flag %s\n", progname, argv[1]);
	usage();
	exit (EXITCODE_ARGS);
    }

    if (sockfd<0) {
	fprintf(stderr, "%s:  I must know the file number for the socket.\n",
		progname);
	usage();
	exit(EXITCODE_ARGS);
    }

    if (subproc) {
	if (merge_returns == UNINITIALIZED)
	    merge_returns = PREFER_LOCAL;
	/* check to make sure at least one descriptor is rerouted */
	if (pipe_head == 0) {
	    fprintf(stderr, "%s: must redirect at least one descriptor of subprocess.\n", progname);
	    usage();
	    exit(EXITCODE_ARGS);
	}
    } else {
	if (pipe_head == 0) {
	    /* rig the degenerate case */
	    add_subproc_fd(0, WRITEABLE, -1);
	    add_subproc_fd(1, READABLE, -1);
	}
	if (merge_returns == PREFER_LOCAL ||
	    merge_returns == PREFER_REMOTE ||
	    merge_returns == LOCAL_ONLY ||
	    merge_returns == REMOTE_ONLY) {
	    fprintf(stderr, "%s: no local process to get a return code from\n", progname);
	    usage();
	    exit (EXITCODE_ARGS);
	}

	merge_returns = LOCAL_ONLY;
    }

    add_control_channel();	/* for passing exit status.  Is DUPLEX_OI. */

    if (verbose)
        emit_version("encapsulate", 1996);

    if (im_server<0) {
	im_server = Im_server_p(sockfd);
    }

    {
	struct pipeline	*curr;
	for (curr = pipe_head; curr; curr = curr->next) {
	    if (curr->code == DUPLEX)
		curr->code = im_server ? DUPLEX_IO : DUPLEX_OI;
	}

	/* might as well initialize our session_id counter */
	session_id_ = im_server ? 1025 : 1024;
	/* below 1024 is reserved */
    }


    if (subproc)
	spawn_child(argv+1, sockfd);
    else {
	/* I have to invert the polarity of writable and readable
	   channels, but not duplex.  Also have to copy the child_fd
	   to pipe[0].  What a hellish mess. */
	rig_single(); }

    signal(SIGPIPE, SIG_IGN);	/* handle the error returns */

    {
	char	*buf;
	int	len;
	buf = prepare_SYNs( &len);
	prime_packet_queue(buf, len);
    }

    main_io_loop(sockfd);

#ifdef DEBUG
    fprintf(stderr, "%s: end of IO loop\n", progname);
#endif

#if 0
    while (child_running) {
	pause();
    }
    probe_child();
#endif

    if (local_return_code ==0)
	local_return_code = exitcode_warnings;

    switch (merge_returns) {
    case PREFER_LOCAL:
	if (local_return_code!=0)
	    exit (local_return_code);
	else
	    exit (remote_return_code);
	/* NOTREACHED */
    case PREFER_REMOTE:
	if (remote_return_code!=0)
	    exit (remote_return_code);
	else
	    exit (local_return_code);
	/* NOTREACHED */
    case LOCAL_ONLY:
	exit(local_return_code);
    case REMOTE_ONLY:
	exit(remote_return_code);
    default:
	fprintf(stderr, "%s: logic error.  merge_returns has bogus value.\n",
		progname);
	exit(EXITCODE_ARGS);
    }
}
