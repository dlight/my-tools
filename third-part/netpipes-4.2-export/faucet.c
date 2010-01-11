/*

    faucet.c, part of
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

static char info[] = "faucet: a network utility for sockets\nWritten 1992-98 by Robert Forsman <thoth@purplefrog.com>\n$Id: faucet.c,v 1.22 1998/08/13 15:01:06 thoth Exp $\n";
#include	<stdio.h>
#include	<errno.h>
extern int errno;		/* I hate the errno header file */
#include	<string.h>
#include	<stdlib.h>
#include	<unistd.h>
#include	<signal.h>
#ifdef hpux
#include	<sgtty.h>
#endif /* defined(hpux) */
#include	<sys/wait.h>
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
#else /* defined(sco) */
#include	<sys/filio.h>
#endif /* defined(sco) */
#endif /* defined(linux) */

#else  /* defined(USE_IOCTL) */
#include	<sys/types.h>
#include	<unistd.h>
#include	<fcntl.h>
#endif /* defined(USE_IOCTL) */
#include	<sys/socket.h>
#ifndef NOUNIXSOCKETS
#include	<sys/un.h>
#endif
#include	<netinet/in.h>
#include	<netdb.h>
/* for getrlimit with -daemon option */
#include <sys/time.h>
#include <sys/resource.h>
#include "common.h"

int	mastersocket;
#define	DOONCE		(1<<0)
#define	DOVERBOSE	(1<<1)
#ifndef NOUNIXSOCKETS
#define	DOUNIX		(1<<2)
#endif
#define DODAEMON	(1<<3)

#define	EXITCODE_CONNECTION	127
#define	EXITCODE_ARGS	126
#define	EXITCODE_FAILED_SYSCALL	125
#define	EXITCODE_PIPE	124

int	doflags=0;
int	running=1;

struct in_addr ** /* addr_array */ convert_hostname();

char	*localhost=NULL;
char	*foreignhost=NULL,*foreignport=NULL;
int	foreignPORT;
int	foreignCOUNT=0;
struct in_addr	**foreignHOST;


int name_to_inet_port();

void usage () {
  fprintf(stderr,"Usage : %s <port> (--in|--out|--err|--fd N)+ [--once] [--verb(|ose)] [--quiet] [--unix] [--foreignport <port>] [--foreignhost <inet-addr>] [--localhost <inet-addr>] [--daemon] [--serial] [--shutdown (r|w)] [--pidfile fname] [--noreuseaddr] [--backlog n] -[i][o][e][#3[,4[,5...]]][v][1][q][u][d][s] [-p <foreign port>] [-h <foreign host>] [-H <local host>] command args\n", progname);
}

void nice_shutdown()
/* This procedure gets called when we are killed with one of the reasonable
   signals (TERM, HUP, that kind of thing).  The main while loop then
   terminates and we get a chance to clean up. */
{
  running = 0;
}


int setup_socket(name, backlog, reuseaddr)
char *name;
int	backlog;
int	reuseaddr;
/* This procedure creates a socket and handles retries on the inet domain.
   Sockets seem to "stick" on my system (SunOS [43].x) */
{
  int	sock;
  int	family;

#ifdef DOUNIX
  if (doflags&DOUNIX)
    family = AF_UNIX;
  else
#endif
    family = AF_INET;

  sock = socket(family, SOCK_STREAM,
#ifdef DOUNIX
		(doflags&DOUNIX)?0:
#endif
		IPPROTO_TCP);
  /* I need a real value for the protocol eventually.  IPPROTO_TCP sounds
     like a good value, but what about AF_UNIX sockets?  It seems to have
     worked so far... */

  if (sock <0) {
      perror("opening stream socket");
      exit(EXITCODE_CONNECTION);
  }

  if (!bindlocal(sock, name, localhost,
		 family,
		 reuseaddr)) {
      fprintf(stderr,"%s: error binding stream socket %s (%s)\n",
	      progname,name,strerror(errno));
      exit(EXITCODE_CONNECTION);
    }

  /* We used to ask for NOFILE (max number of open files) for the size
     of the connect queue.  Linux didn't like it (NOFILE=256) so we
     hardcoded a smaller value. */
  listen(sock, (backlog>0 ? backlog : 5) );

  return(sock);
}


void waitonchild()

{
  int	status;

  int	childpid;
  
  childpid = wait(&status);
}


int
authorize_address(sin)
     struct sockaddr	*sin;
{
#ifdef DOUNIX
  if (doflags&DOUNIX) {
    struct sockaddr_un 	*srv = (struct sockaddr_un*)sin;
    
    if (foreignport != NULL && 0!=strcmp(foreignport, srv->sun_path)) {
      if (doflags&DOVERBOSE) {
	  fprintf(stderr, "%s: refusing connection from port %s\n",
		  progname, srv->sun_path);
      }
      return 0;
    }
  } else
#endif
    {
      struct sockaddr_in	*srv = (struct sockaddr_in*)sin;
      int	i;

      if (foreignhost) {
	  for (i=0; i<foreignCOUNT; i++) {
	      if (0==memcmp(&srv->sin_addr,
			    foreignHOST[i], sizeof(struct in_addr)))
		  break;
	  }
	  if (i>=foreignCOUNT) {
	      if (doflags&DOVERBOSE) {
		  fprintf(stderr, "refusing connection from host ");
		  printhost(stderr, &srv->sin_addr);
		  fprintf(stderr, ".\n");
	      }
	      return 0;
	  }
    }
    
    if (foreignport!=NULL && foreignPORT != srv->sin_port) {
      if (doflags&DOVERBOSE) {
	fprintf(stderr, "refusing connection from port %d.\n",
	       ntohs(srv->sin_port));
      }
      return 0;
    }
  }
  
  return 1;
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
	    fprintf(stderr, "%s: parse error in file descriptor list at '%s'\n", progname, s);
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
  int	rval, i;
  union {
    struct sockaddr_in	in;
#ifdef DOUNIX
    struct sockaddr_un	un;
#endif
  } saddr;
  struct sockaddr_in	*sinp = &saddr.in;
#ifdef DOUNIX
  struct sockaddr_un	*sunp = &saddr.un;
#endif
  char	**cmd;

  char	*pidfilename=0;		/* we'll write our PID in decimal
				   into this file */
  FILE	*pidfp=0;

  int	serialize=0;

  int	backlog=0;		/* parameter to pass to listen(2) */
  int	reuseaddr=1;		/* Shall we set SO_REUSEADDR? */

  /*
   *
   */

  set_progname(argv[0]);
  
  if (argc<3) {
    usage();
    exit(EXITCODE_ARGS);
  }
  
  /* parse trailing args */
  for (i=2; i<argc; i++) {
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
      } else if (strcmp(arg,"once")==0)
	  doflags |= DOONCE;
      else if (strcmp(arg,"verbose")==0 ||
	       strcmp(arg,"verb")==0)
	  doflags |= DOVERBOSE;
      else if (strcmp(arg,"quiet")==0)
	  doflags &= ~DOVERBOSE;
      else if (strcmp(arg,"unix")==0) {
#ifdef DOUNIX
	  doflags |= DOUNIX;
#else
	  fprintf(stderr, "%s: unix-domain sockets are not supported in this binary.\n", progname);
	  exit(EXITCODE_ARGS);
#endif
      } else if (strcmp(arg,"foreignport")==0) {
	  if (i+1<argc)
	      foreignport=argv[++i];
	  else {
	      fprintf(stderr,"%s: foreignport requires port name or number.\n",
		      progname);
	      usage();
	      exit(EXITCODE_ARGS);
	  }
      } else if (strcmp(arg,"foreignhost")==0) {
	  if (i+1<argc)
	      foreignhost=argv[++i];
	  else {
	      fprintf(stderr,"%s: foreignhost requires host name or number.\n",
		      progname);
	      usage();
	      exit(EXITCODE_ARGS);
	  }
      } else if (strcmp(arg,"localhost")==0) {
	  if (i+1<argc)
	      localhost=argv[++i];
	  else {
	      fprintf(stderr,"%s: -localhost requires host name or number.\n",
		      progname);
	      usage();
	      exit(EXITCODE_ARGS);
	  }
      } else if (strcmp(arg,"daemon")==0) {
	  doflags |= DODAEMON;
      } else if (strcmp(arg,"shutdown")==0) {
	  int	err=1;
	  if (i+1<argc) {
	      arg = argv[++i];
	      err=0;
	      if (0==strcmp(arg, "r")) {
		  how_shutdown = 1;
	      } else if (0==strcmp(arg, "w")) {
		  how_shutdown = 0;
	      } else {
		  err = 1;
	      }
	  }
	  if (err) {
	      fprintf(stderr,"%s: -shutdown requires \"r\" or \"w\" string.\n",
		      progname);
	      usage();
	      exit(EXITCODE_ARGS);
	  }
      } else if (strcmp(arg,"serial")==0) {
	  serialize=1;
      } else if (strcmp(arg,"pidfile")==0) {
	  if (i+1<argc) {
	      pidfilename = argv[++i];
	  } else {
	      fprintf(stderr, "%s: -pidfile requires filename argument.\n",
		      progname);
	      usage();
	      exit(EXITCODE_ARGS);
	  }
      } else if (strcmp(arg,"noreuseaddr")==0) {
	  reuseaddr=0;
      } else if (strcmp(arg,"backlog")==0) {
	  if (i+1<argc) {
	      backlog = atoi(argv[++i]);
	  } else {
	      fprintf(stderr, "%s: --%s requires numerical (>0) argument.\n",
		      progname, arg);
	      usage();
	      exit(EXITCODE_ARGS);
	  }
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
	      case '1': doflags |= DOONCE; break;
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
		      foreignport=argv[++i];
		  else
		      fprintf(stderr, "%s: foreignport requires port name or number.\n", progname);
		  break;
	      case 'h':
		  if (i+1<argc)
		      foreignhost=argv[++i];
		  else
		      fprintf(stderr, "%s: foreignhost requires host name or number.\n", progname);
		  break;
	      case 'H':
		  if (i+1<argc)
		      localhost=argv[++i];
		  else {
		      fprintf(stderr,
			      "%s: -localhost requires host name or number.\n",
			      progname);
		      usage();
		      exit(EXITCODE_ARGS);
		  }
		  break;
	      case 'd':
		  doflags |= DODAEMON; break;
	      case 's':
		  serialize=1; break;
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
      emit_version("faucet", 1992);
  }

  if ( nfds==0 ) {
    fprintf(stderr,"%s: Need at least one {--in|--out|--err|--fd #}.\n",progname);
    usage();
    exit(EXITCODE_ARGS);
  }
  
  if (!*cmd) {
    fprintf(stderr, "%s: No subcommand specified.\n", progname);
    usage();
    exit (EXITCODE_ARGS);
  }

#ifdef DOUNIX
  if ( (doflags&DOUNIX) && foreignhost!=NULL ) {
    fprintf(stderr, "%s: foreignhost parameter makes no sense with UNIX domain sockets, ignoring.\n", progname);
    foreignhost = NULL;
  }
#endif
  
  if (!serialize)
      signal(SIGCHLD,waitonchild);

  reserve_fds(0);

  mastersocket = setup_socket(argv[1], backlog, reuseaddr);
  
  signal(SIGHUP, nice_shutdown);
  signal(SIGINT, nice_shutdown);
  signal(SIGPIPE, nice_shutdown);
  signal(SIGALRM, nice_shutdown);
  signal(SIGTERM, nice_shutdown);
  
  if (foreignhost != NULL &&
      0==(foreignHOST = convert_hostname(foreignhost, &foreignCOUNT))) {
    fprintf(stderr, "%s: could not translate %s to a host address\n",
	    progname, foreignhost);
    exit(EXITCODE_CONNECTION);
  }
  
  if (foreignport!=NULL &&
#ifdef DOUNIX
      !(doflags&DOUNIX) &&
#endif
      0 == (foreignPORT = name_to_inet_port(foreignport)) ) {
    fprintf(stderr,"%s: port %s unknown.\n",progname,foreignport);
    exit(EXITCODE_CONNECTION);
  }

  /* we should test-open the pidfile before we ditch our terminal*/
  if (pidfilename!=0) {
      pidfp = fopen(pidfilename, "w");
      if (pidfp==0) {
	  fprintf(stderr,"%s: unable to open pidfile `%s' for write: %s\n",
		  progname, pidfilename, strerror(errno));
	  exit(EXITCODE_FAILED_SYSCALL);
      }
      /* I hope leaving it open across a fork isn't bad.
	 We leave stdin and stderr open across a fork, and there's
	 nothing in the pidfp buffer, so we should be safe. */
  }

  if (doflags&DODAEMON) {
      if (doflags&DOVERBOSE) {
	  fprintf(stderr, "%s: detaching from terminal, bye\n", progname);
      }
#ifdef NO_SETSID
      {
	  struct rlimit	rl;
	  int	count;
	  /* figure out how many file descriptors are possible */
	  rval = getrlimit(RLIMIT_NOFILE, &rl);
	  if (rval!=0 || rl.rlim_cur == RLIM_INFINITY) {
	      count=64;		/* reasonable guess */
	  } else {
	      count = rl.rlim_cur;
	  }
	  /* close them all (except the listening socket) */
	  for (i = 0; i<count; i++) {
	      if (i != mastersocket)
		  close(i);
	  }
      }
      {
	  int rval = open("/dev/tty", O_RDWR);
	  if (rval>=0) {
	      ioctl(rval, TIOCNOTTY, &rval);
	      close(rval);
	  }
      }
      /* it seems printing to a closed FP will kill a process in some OSs. */
      freopen("/dev/null", "w", stderr);
      freopen("/dev/null", "w", stdout);
      freopen("/dev/null", "r", stdin);
#endif
      {
	  int childpid = fork();
	  if (childpid<0) {
	      fprintf(stderr, "%s: WAUGH! fork failed while trying to enter -daemon mode.  This bodes ill.\n", progname);
	  } else if (childpid>0)
	      exit(0);
      }
#ifndef NO_SETSID
      setsid();
#endif
  }

  if (pidfp) {
      fprintf(pidfp, "%ld\n", (long) getpid());
      fclose(pidfp);
  }


  while (running) {

    {
      int	length;
    
      length = sizeof(saddr);
    
      rval = accept(mastersocket,(struct sockaddr*)&saddr,&length);
    }
    
    if (rval<0) {

      if (errno==EWOULDBLOCK) {
	  /* this can't happen, but why take chances? */
	  fprintf(stderr, "%s: No more connections to talk to.\n",progname);
      } else if (errno!=EINTR) {
	fprintf(stderr,"%s: error in accept (%s).",
		progname,strerror(errno));
	exit(EXITCODE_FAILED_SYSCALL);
      }
      continue;
    }
    
    if (!authorize_address(&saddr)) {
      close(rval);
      continue;
    }
    
    if ( doflags&DOVERBOSE ) {
      fprintf(stderr, "%s: Got connection from ",progname);
#ifdef DOUNIX
      if ( doflags&DOUNIX ) {
	puts(sunp->sun_path);
      } else
#endif
	  {
	      printhost(stderr, &sinp->sin_addr);
	      fprintf(stderr, " port %d\n",ntohs(sinp->sin_port));
	  }
    }
    
    fflush(stdout);
    
    if ( doflags&DOONCE || fork()==0 ) { /* XXX should check error return */
      /* child process: frob descriptors and exec */
      char	*s;
      int	duped_stderr;
      
#ifdef DOUNIX
      if ( (doflags&(DOONCE|DOUNIX)) == (DOONCE|DOUNIX) )
	unlink(argv[1]);
      /* We don't want the unix domain socket anymore */
#endif

      /* the child doesn't need the master socket fd */
      close(mastersocket);

      /* put stderr somewhere safe temporarily */
      duped_stderr = dup(fileno(stderr));

      /* but we don't want it to hang around after we exec... */
#ifdef USE_IOCTL
      ioctl(duped_stderr,FIOCLEX,NULL);
#else
      fcntl(duped_stderr,F_SETFD,FD_CLOEXEC);
#endif

      /* We don't need old stderr hanging around after an exec.
	 The mastersocket has been closed by the dup2 */
      
      dup_n(rval); /* dup the socket onto all the chosen file descriptors */
      
      close(rval); /* rval has been properly duplicated */

      execvp(cmd[0], cmd);
      s ="exec failed\n";
      write(duped_stderr,s,strlen(s));
      exit(0);
    } else {
      /* parent: close socket.
	 Signal will arrive upon death of child. */
      close(rval);
      if (serialize) {
	  int	status;
	  pid_t	pid;
	  pid = wait(&status);
	  /* child has exited */
	  if (pid == -1) {
	      fprintf(stderr, "%s: error serializing (waiting on child) ",
		      progname);
	      perror("");
	  }
      }
    }
  }

  if (pidfilename != 0) {
      if (doflags&DOVERBOSE)
	  fprintf(stderr, "%s: removing pid file %s\n",
		  progname, pidfilename);
      unlink(pidfilename);	/* if it fails, we just don't care */
  }

#ifdef DOUNIX
  /* clean up the socket when we're done */
  if (doflags&DOUNIX)
    unlink(argv[1]);
#endif
  close(mastersocket);
  
  return 0;
}
