/*

    $Id: hose.c,v 1.25 1998/10/28 15:52:23 thoth Exp $, part of
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

static char info[] = "hose: a network utility for sockets\nWritten 1992-98 by Robert Forsman <thoth@purplefrog.com>\n$Id: hose.c,v 1.25 1998/10/28 15:52:23 thoth Exp $\n";
#include	<stdio.h>
#include	<string.h>
#include	<stdlib.h>
#ifdef hpux
#include	<sgtty.h>
#endif
#include	<signal.h>
#include	<errno.h>
#include	<sys/param.h>
#include	<sys/file.h>
#ifdef USE_IOCTL
#include	<sys/ioctl.h>
/* find the FIOCLEX ioctl */
#ifdef linux
#include	<sys/termios.h>
#else  /* defined(linux) */
#ifdef sco
#include	<sgtty.h>
#else  /* defined(sco) */
#include	<sys/filio.h>
#endif /* defined(sco) */
#endif /* defined(linux) */

#else  /* defined(USE_IOCTL) */
#include	<fcntl.h>
#endif /* defined(USE_IOCTL) */
#include	<unistd.h>
#include	<sys/types.h>
#include	<sys/socket.h>
#ifndef NOUNIXSOCKETS
#include	<sys/un.h>
#endif
#include	<netinet/in.h>
#include	<sys/time.h>
#include	<netdb.h>
#include	<sys/wait.h>
#ifdef AIX
#include 	<sys/select.h>
#endif
#include "memmove.h"
#include "common.h"

#ifndef NOUNIXSOCKETS
#define	DOUNIX		(1<<0)
#endif
#define	DOVERBOSE	(1<<1)
#define	DOJAM		(1<<2)
#define	DOSLAVE		(1<<3)
#define	DONETSLAVE_CT	(1<<4)
#define	DONETSLAVE_CF	(1<<5)
#define	DONETSLAVE	(DONETSLAVE_CT | DONETSLAVE_CF)

#define	EXITCODE_CONNECTION	127
#define	EXITCODE_ARGS	126
#define	EXITCODE_FAILED_SYSCALL	125
#define	EXITCODE_PIPE	124

struct in_addr ** /* addr_array */ convert_hostname();
long	doflags=0;

int	retry=0;	       /* how many times to retry after ECONNREFUSED */
unsigned delay=5;		/* how long to wait between each retry */
int	shutdn=0;		/* should we fork, wait and shutdown? */
char	*localport=NULL;	/* local port name */
char	*localaddr=NULL;	/* local internet address */
extern int	errno;


int name_to_inet_port();

void usage () {
  fprintf(stderr,"Usage : %s <hostname> <port> (--in|--out|--err|--fd N|--slave|--netslave|--netslave1|--netslave2)+ [--verb(|ose)] [--unix] [--localport <port>] [--localhost <inet-addr>] [--retry n] [--delay n] [--shutdown [r|w][a]] [--noreuseaddr] -[i][o][e][#3[,4[,5...]]][s][v][q][u] [-p <local port>] [-h <local host>] <command> [ args ... ]\n",progname);
}


int setup_socket(hostname,portname, reuseaddr)
char	*hostname;
char	*portname;
int	reuseaddr;

{
  int	sock = -1;
  struct in_addr ** addresses=0;
#ifdef DOUNIX
  struct sockaddr_un	unix_addr;
#endif
  struct sockaddr_in	inet_addr;
  int	num_addresses;
  int length;
  int tries;
  int cstat;

#ifdef DOUNIX
  if (doflags&DOUNIX) {
      unix_addr.sun_family = AF_UNIX;
      strncpy( unix_addr.sun_path, portname, sizeof(unix_addr.sun_path));
      unix_addr.sun_path[sizeof(unix_addr.sun_path) - 1] = 0;
      length = sizeof(struct sockaddr_un);
      num_addresses = 1;
  } else
#endif
    {
      inet_addr.sin_family = AF_INET;

      if (0==(addresses = convert_hostname(hostname, &num_addresses))) {
	  fprintf(stderr, "%s: could not translate %s to a host address\n",
		  progname, hostname);
	  exit(EXITCODE_CONNECTION);
      }

      inet_addr.sin_port = name_to_inet_port(portname);
      if (inet_addr.sin_port==0) {
	  fprintf(stderr,"%s: bogus port number %s\n",progname,portname);
	  exit(EXITCODE_CONNECTION);
      }

      length = sizeof(struct sockaddr_in);
    }
  
  for (tries = 0; retry<0 || tries <= retry; tries++) {
    int	j;
    int	family;

#ifdef DOUNIX
  if (doflags&DOUNIX)
    family = AF_UNIX;
  else
#endif
    family = AF_INET;

    /* multi-homed hosts are a little tricky */
    for ( j=0; j<num_addresses; j++) {

	sock = socket(family, SOCK_STREAM, 0);
	if (sock <0) {
	    perror("opening stream socket");
	    exit(EXITCODE_CONNECTION);
	}

	if ((localport) &&
	    !bindlocal(sock, localport, localaddr,
		       family, reuseaddr) ) {
	    fprintf(stderr,"%s: error binding stream socket %s (%s)\n",
		    progname,localport,strerror(errno));
	    exit(EXITCODE_CONNECTION);
	}
#ifdef DOUNIX
	if (!(doflags&DOUNIX))
#endif
	    {
		inet_addr.sin_addr = *(addresses[j]);
	    }
	if (doflags&DOVERBOSE) {
	    fprintf(stderr, "%s: attempting to connect to ", progname);
#ifdef DOUNIX
	    if (doflags&DOUNIX) {
		fputs(unix_addr.sun_path, stderr);
	    } else
#endif
	      {
		printhost(stderr, &inet_addr.sin_addr);
		fprintf(stderr, " port %d\n", ntohs(inet_addr.sin_port));
	      }
	}
	cstat=connect(sock,
#ifdef DOUNIX
		      (doflags&DOUNIX) ?
		      ((struct sockaddr*)&unix_addr) :
#endif
		      ((struct sockaddr*)&inet_addr) ,
		      length);
	if (cstat==0)
	    break;		/* success */

	if (errno==ECONNREFUSED) {
	    close(sock);
	    sock = -1;
	} else {
	    perror("connecting");
	    exit(EXITCODE_CONNECTION);
	}
    }
    if (j<num_addresses)
	break;			/* success */

    if (tries<retry) {
	/* failed, retry all addresses after a delay */
	if (doflags&DOVERBOSE) {
	    fprintf(stderr, "sleeping before retry...");
	    fflush(stdout);
	}
	sleep(delay);
	if (doflags&DOVERBOSE)
	    fprintf(stderr, "\n");
    }
  }

  if (sock < 0) {
    fprintf(stderr, "%s: Retries exhausted, failing connect to %s:%s\n",
	    progname, hostname, portname);
    exit(EXITCODE_CONNECTION);
  }

  return(sock);
}

/*
   copy bytes from stdin to the socket and
   copy bytes from the socket to stdout.
   */
void copyio(sock, aggressive_close)
    int	sock;
    int	aggressive_close;
{
    fd_set	readfds, writefds;
#define BSIZE	4096
    char	tosockbuf[BSIZE], fromsockbuf[BSIZE];
    int		tosocklen, fromsocklen;
    int		rval;
    int	exitval = 0;

    tosocklen = fromsocklen = 0;
    while (tosocklen>=0 || fromsocklen>=0) {
	/********************/
	FD_ZERO(&readfds);
	FD_ZERO(&writefds);
	if (tosocklen>=0) {
	    if (tosocklen==0) {
		FD_SET(0, &readfds);
	    } else {
		FD_SET(sock, &writefds);
	    }
	}
	if (fromsocklen>=0) {
	    if (fromsocklen==0) {
		FD_SET(sock, &readfds);
	    } else {
		FD_SET(1, &writefds);
	    }
	}

	if ( ( (aggressive_close&DONETSLAVE_CF)
	       && fromsocklen < 0
	       && tosocklen<1 )
	     ||
	     ( (aggressive_close&DONETSLAVE_CT)
	       && tosocklen < 0
	       && fromsocklen<1 )
	     ) {
	    /* One direction is closed and the other's buffer is empty.
	       Exit. */
	    close(sock);
	    break;
	}

	if ( ( (aggressive_close&DONETSLAVE_CF)
	       && fromsocklen < 0 )
	     ||
	     ( (aggressive_close&DONETSLAVE_CT)
	       && tosocklen < 0 )
	     ) {
	    /* One direction is closed but the other's buffer is not empty.
	       Don't accept any more input while we flush the buffer. */
	    FD_ZERO(&readfds);
	}
	/********************/

	rval=select(sock+1, &readfds, &writefds,
		    (fd_set*)0, (struct timeval*)0);
	/********************/
	if (rval<0) {
	    if (errno != EINTR) {
		perror("during copyio() select(2)");
		exit(EXITCODE_PIPE);
	    }
	} else if (rval==0) {
	    break;
	}
	/********************/
	if (FD_ISSET(1, &writefds)) {
	    rval = write(1, fromsockbuf, fromsocklen);
	    if (rval<0) {
		perror("during copyio() write(2)(1)");
		exitval = EXITCODE_PIPE;
		fromsocklen = -1;
		shutdown(sock, 0);
	    } else {
		memmove(fromsockbuf, fromsockbuf+rval, fromsocklen-rval);
		fromsocklen -= rval;
	    }
	}
	if (FD_ISSET(sock, &writefds)) {
	    rval = write(sock, tosockbuf, tosocklen);
	    if (rval<0) {
		perror("during copyio() write(2)(sock)");
		exitval = EXITCODE_PIPE;
		tosocklen = -1;
		shutdown(sock, 1);
	    } else {
		memmove(tosockbuf, tosockbuf+rval, tosocklen-rval);
		tosocklen -= rval;
	    }
	}
	if (FD_ISSET(0, &readfds)) {
	    tosocklen = read(0, tosockbuf, BSIZE);
	    if (tosocklen<0) {
		perror("during copyio() read(2)(0)");
		exitval = EXITCODE_PIPE;
		tosocklen = -1;
	    } else if (tosocklen==0) {
		tosocklen = -1;
		if (aggressive_close == 0)
		    shutdown(sock, 1);
	    }
	}
	if (FD_ISSET(sock, &readfds)) {
	    fromsocklen = read(sock, fromsockbuf, BSIZE);
	    if (fromsocklen<0) {
		perror("during copyio() read(2)(0)");
		exitval = EXITCODE_PIPE;
		fromsocklen = -1;
	    } else if (fromsocklen==0) {
		fromsocklen = -1;
		if (aggressive_close == 0)
		    shutdown(sock, 0);
	    }
	}
    }
    exit(exitval);
}

void endjam()
{
  doflags &= ~DOJAM;
}

/**********************************************************************/
/* since we have flag processing for long and short, we do the same thing
   in two separate pieces of code.  The non-trivial ones we encapsulate
   in a small function */

void flag_in()
{
    add_fd(0);
    if (how_shutdown == 0)	/* make sure we can read from the socket */
	how_shutdown = -1;
    else if (how_shutdown==-2)
	how_shutdown = 1;
}

void flag_out()
{
    add_fd(1);
    if (how_shutdown == 1)	/* make sure we can write to the socket */
	how_shutdown = -1;
    else if (how_shutdown==-2)
	how_shutdown = 0;
}

void flag_err()
{
    add_fd(2);
    if (how_shutdown == 1)	/* make sure we can write to the socket */
	how_shutdown = -1;
    else if (how_shutdown==-2)
	how_shutdown = 0;
}

int flag_scan_comma_fds(s)
    char *s;
{
    int	rval=0;
    while (1) {
	int	fd;
	int	n;
	if (1 != sscanf(s, "%i%n", &fd, &n)) {
	    fprintf(stderr, "%s: parse error in file descriptor list at 's'\n", progname);
	    usage();
	    exit(EXITCODE_ARGS);
	}
	add_fd(fd);
	rval +=n;
	s += n;
	if (*s == ',') {
	    rval++;
	    s++;
	} else {
	    break;
	}
    }
    return rval;
}

/**********************************************************************/


int main (argc,argv)
     int argc;
     char ** argv;
     
{
  int	sock,i;
  int	jampipe[2];
  char	**cmd;
  int	reuseaddr =1;

  set_progname(argv[0]);
  
  if (argc<4) {
    usage();
    exit(EXITCODE_ARGS); 
  }
  if (strcmp(argv[1],"-unix-")==0 || strcmp(progname,"uhose")==0 ){
#ifdef DOUNIX
    doflags |= DOUNIX;
#else
    fprintf(stderr, "%s: unix-domain sockets are not supported in this binary.\n", progname);
    exit(EXITCODE_ARGS);
#endif
  }
  for (i=3; i<argc; i++) {
    char	*arg;
    if (argv[i][0]!='-')
      break;
    arg = argv[i]+1;
    if (*arg == '-') arg++;
    if (strcmp(arg,"in")==0) {
	flag_in();
    } else if (strcmp(arg,"out")==0) {
	flag_out();
    } else if (strcmp(arg,"err")==0) {
	flag_err();
    } else if (strncmp(arg,"fd",2)==0) {
	  int	fd;
	  if (arg[2])
	      fd = atoi(arg+2);
	  else if (i+1<argc) {
	      fd = atoi(argv[++i]);
	  } else {
	      fprintf(stderr, "%s: --fd requires numeric file descriptor argument.\n", progname);
	      usage();
	      exit(EXITCODE_ARGS);
	  }
	  add_fd(fd);
	  how_shutdown = -1;
    } else if (strcmp(arg,"slave")==0) {
	if (doflags & DOSLAVE) {
	    fprintf(stderr, "%s: only one --slave or --netslave permitted.\n",
		    progname);
	    usage();
	    exit(EXITCODE_ARGS);
	}
	doflags |= DOSLAVE;
    } else if (strcmp(arg,"netslave")==0) {
	if (doflags & DOSLAVE) {
	    fprintf(stderr, "%s: only one --slave or --netslave permitted.\n",
		    progname);
	    usage();
	    exit(EXITCODE_ARGS);
	}
	doflags |= DOSLAVE|DONETSLAVE;
    } else if (strcmp(arg,"netslave1")==0) {
	if (doflags & DOSLAVE) {
	    fprintf(stderr, "%s: only one --slave or --netslave permitted.\n",
		    progname);
	    usage();
	    exit(EXITCODE_ARGS);
	}
	doflags |= DOSLAVE|DONETSLAVE_CT;
    } else if (strcmp(arg,"netslave2")==0) {
	if (doflags & DOSLAVE) {
	    fprintf(stderr, "%s: only one --slave or --netslave permitted.\n",
		    progname);
	    usage();
	    exit(EXITCODE_ARGS);
	}
	doflags |= DOSLAVE|DONETSLAVE_CF;
    } else if (strcmp(arg,"unix")==0) {
#ifdef DOUNIX
      doflags |= DOUNIX;
#else
      fprintf(stderr, "%s: unix-domain sockets are not supported in this binary.\n", progname);
      exit(EXITCODE_ARGS);
#endif
    } else if (strcmp(arg,"verbose")==0 ||
	     strcmp(arg,"verb")==0)
      doflags |= DOVERBOSE;
    else if (strcmp(arg,"jam")==0)
      doflags |= DOJAM;
    else if (strcmp(arg,"localport")==0) {
      if (i+1<argc)
	localport=argv[++i];
      else {
	fprintf(stderr,
		"%s: -localport requires port name or number argument.\n",
		progname);
	usage();
	exit(EXITCODE_ARGS);
      }
    } else if (strcmp(arg,"localhost")==0) {
      if (i+1<argc)
	localaddr=argv[++i];
      else {
	fprintf(stderr,
		"%s: -localhost requires internet name or number.\n",
		progname);
	usage();
	exit(EXITCODE_ARGS);
      }
    } else if (strcmp(arg,"retry")==0) {
      if (i+1<argc)
        retry=atoi(argv[++i]);
      else
	fprintf(stderr,"%s: retry requires count argument.\n",
		progname);
    } else if (strcmp(arg,"delay")==0) {
      if (i+1<argc)
        delay=atoi(argv[++i]);
      else {
	fprintf(stderr,"%s: delay requires time argument in seconds.\n",
		progname);
	usage();
	exit(EXITCODE_ARGS);
      }
    } else if (strcmp(arg,"shutdown")==0) {
	int	err=1;
	if (i+1<argc) {
	    arg = argv[++i];
	    err=0;
	    if (0==strcmp(arg, "r")) {
		how_shutdown = 1;
	    } else if (0==strcmp(arg, "w")) {
		how_shutdown = 0;
	    } else if (0==strcmp(arg, "ra")) {
		how_shutdown = 1;
		shutdn = 1;
	    } else if (0==strcmp(arg, "wa")) {
		how_shutdown = 0;
		shutdn = 1;
	    } else if (0==strcmp(arg, "a")) {
		shutdn = 1;
	    } else {
		err = 1;
	    }
	}
	if (err) {
	    fprintf(stderr,"%s: shutdown requires \"r\", \"w\" \"ra\", \"wa\", or \"a\" string.\n",
		    progname);
	    usage();
	    exit(EXITCODE_ARGS);
	}
    } else if (strcmp(arg,"noreuseaddr")==0) {
	reuseaddr=0;
    } else {
	int	j;
	for (j=0; arg[j]; j++) {
	    switch (arg[j]) {
	    case 'i': flag_in(); break;
	    case 'o': flag_out(); break;
	    case 'e': flag_err(); break;
	    case '#':
		j += flag_scan_comma_fds(arg+j+1);
		break;
	    case 's':
		if (doflags & DOSLAVE) {
		    fprintf(stderr, "%s: only one --slave or --netslave permitted.\n", progname);
		    usage();
		    exit(EXITCODE_ARGS);
		}
		doflags |= DOSLAVE;
		break;
	    case 'v': doflags |= DOVERBOSE; break;
	    case 'q': doflags &= ~DOVERBOSE; break;
	    case 'u':
#ifdef DOUNIX
	      doflags |= DOUNIX;
#else
	      fprintf(stderr, "%s: unix-domain sockets are not supported in this binary.\n", progname);
	      exit(EXITCODE_ARGS);
#endif
	      break;
	    case 'p':
		if (i+1<argc) 
		    localport=argv[++i];
		else
		    fprintf(stderr,
			    "%s: localport requires port name or number.\n",
			    progname);
		break;
	    case 'h':
		if (i+1<argc)
		    localaddr=argv[++i];
		else
		    fprintf(stderr,
			    "%s: localhost requires host name or number.\n",
			    progname);
		break;
	    default:
		fprintf(stderr,
			"%s: Unrecognized flag '%c' in argument -%s.\n",
			progname, arg[j], arg);
		usage();
		exit(EXITCODE_ARGS);
	    }
	}
    }
  }
  cmd = argv+i;

  if (doflags&DOVERBOSE) {
      emit_version("hose", 1992);
  }

  if ( nfds==0 && !(doflags&DOSLAVE) ) {
    fprintf(stderr,"%s: Need at least one {in|out|err|fd #|slave}.\n",progname);
    usage();
    exit(EXITCODE_ARGS);
  }

  if (doflags&DOSLAVE) {
    if (*cmd) {
      fprintf(stderr, "%s: you must not specify a subcommand (%s) when using \nthe -slave or -netslave option.\n", progname, *cmd);
      usage();
      exit (EXITCODE_ARGS);
    } else if (nfds>0) {
	fprintf(stderr, "%s: --in, --out, --err, and --fd are mutually exclusive \nwith --slave and --netslave.\n", progname);
    }
  } else {
    if (!*cmd) {
      fprintf(stderr, "%s: No subcommand specified.\n", progname);
      usage();
      exit (EXITCODE_ARGS);
    }
  }

  /* this wierd setup is to flood a socket with connections */
  if (doflags&DOJAM) {
    signal(SIGCHLD, endjam);
    if (0>pipe(jampipe)) {
      perror("opening jampipe");
      exit(EXITCODE_ARGS);
    }
  }

  fflush(stdout);
  fflush(stderr);
  while ( (doflags & DOJAM) && fork() ) {
    char	ch;
    close (jampipe[1]);
    while (1==read(jampipe[0], &ch, 1))
      ;
    close (jampipe[0]);
    jampipe[0] = -1;
    if (0>pipe(jampipe)) {
      perror("opening jampipe");
      exit(EXITCODE_FAILED_SYSCALL);
    }
  }

  if (doflags&DOJAM)
    close (jampipe[0]);

  reserve_fds(0);

  sock = setup_socket(argv[1],argv[2], reuseaddr);

#ifdef DOUNIX  
  if (doflags&DOUNIX && localport!=NULL)
    unlink(localport);
#endif

  if (doflags &DOSLAVE) {
      copyio(sock, doflags & DONETSLAVE );
  }

  fflush(stdout);
  fflush(stderr);
  /* if we're to shutdown(2) the socket when the subprocess exits we
     need to fork */
  i = shutdn ? fork() : 0;

  if (i) {
    /* we are supposed to shutdown(2) the socket and we are the parent */
    int	status;
    int	pid;
    pid = wait(&status);
    if (pid != -1 && i!=pid)
      fprintf(stderr, "Strange, wait returned a child I don't know about.  I'm an unwed father!\n");
    shutdown(sock, 2);		/* shut the socket down nicely? */
    close(sock);
    exit( (status&0xff) ? EXITCODE_FAILED_SYSCALL : ((status>>8)&0xff));
  } else {
    int sparefd;
    char *s;
    
    sparefd = dup(fileno(stderr));
#ifdef USE_IOCTL
    ioctl(sparefd,FIOCLEX,NULL);
#else
    fcntl(sparefd,F_SETFD,FD_CLOEXEC);
#endif
    
    dup_n(sock); /* dup the socket onto all the chosen file descriptors */

    close(sock);
    
    if (doflags&DOJAM)
      close (jampipe[1]);

    execvp(cmd[0], cmd);

    s ="exec failed for ";
    write(sparefd,s,strlen(s));
    write(sparefd,cmd[0],strlen(cmd[0]));
    write(sparefd,"\n",1);
    exit(EXITCODE_FAILED_SYSCALL);
  }
  /* NOTREACHED */
}
