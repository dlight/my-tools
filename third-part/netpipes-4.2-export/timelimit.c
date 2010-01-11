/*

    timelimit.c, part of
    netpipes: network pipe utilities
    Copyright (C) 1996-98 Robert Forsman

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

static char info[] = "timelimit: a  utility for sockets\nWritten 1996 by Robert Forsman <thoth@purplefrog.com>\n";

#include <stdio.h>
#include <signal.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
extern int errno;
#include "common.h"

int verbose = 0;
int nokill = 0;

char *childprogname=0;

int deadchild = 0;

void notice_child()

{
    deadchild = 1;
}

void usage()
{
    fprintf(stderr,"Usage : %s [ -v ] [ -nokill ] <time limit> <command> <args>\n",
	    progname);
}

void kill_child(childpid)
     int	childpid;
{
    if (verbose)
	fprintf(stderr, "%s: child time limit exceeded; killing %s.\n",
		progname, childprogname);

    kill (childpid, SIGTERM);
    if (!deadchild)
	sleep(2);
    kill (childpid, SIGHUP);
    if (!deadchild)
	sleep(2);
    kill (childpid, SIGKILL);

    {
      int i=0;
      while (!deadchild) {
	int rval;
	sleep(1);
	rval = kill(childpid, SIGKILL);
	if (rval != 0 && errno == ESRCH)
	  break;
	if (i==5 && verbose) {
	  fprintf(stderr, "%s: child %s refuses to die [%d].  I refuse to stop killing it.", progname, childprogname, rval);
	}
	i++;
      }
    }
    /* it better be dead now */
}

int main (argc,argv)
     int argc;
     char ** argv;
     
{
    int	childpid, timelimit;
    time_t	start_time;

    set_progname(argv[0]);

    while (argc>1) {
	if (0==strcmp(argv[1], "-v")) {
	    verbose = 1;
	    argv++;
	    argc--;
	} else if (0==strcmp(argv[1], "-nokill")) {
	    nokill = 1;
	    argv++;
	    argc--;
	} else {
	    break;
	}
    }

    if (argc<3) {
	usage();
	exit(1); 
    }

    {
	int	factor=1;
	char *ch = argv[1];
	ch = ch + strlen(ch)-1;
	switch (*ch) {
	case 'Y': factor = 365*24*60*60; break;
	case 'w': factor =   7*24*60*60; break;

	case 'M': factor *= 30;	/* fall through */
	case 'd': factor *= 24;
	case 'h': factor *= 60;
	case 'm': factor *= 60; break;
	default:
	    ch = 0;
	}
	if (ch)
	    *ch = 0;
	timelimit = strtol(argv[1], &ch, 0);
	if (*ch != 0) {
	    fprintf(stderr, "%s: time spec must be mostly numeric.  %s is bogus.\n",
		    progname, argv[1]);
	    exit(1);
	}
	timelimit *= factor;
    }

    if (verbose)
        emit_version("timelimit", 1996);

    start_time = time((time_t*)0);

    childprogname = argv[2];

    childpid = fork();

    if (childpid<0) {
	fprintf(stderr, "%s: unable to fork (%s).\n", progname, strerror(errno));
	exit (1);
    }

    if (childpid==0) {
	/* child */
	execvp(argv[2], argv+2);
	fprintf(stderr, "%s: Unable to exec %s (%s)",
		progname, argv[2], strerror(errno));
	exit (1);
    }

    signal(SIGCHLD, notice_child);

    while (!deadchild) {
	time_t	now = time((time_t*)0);

	if (now >= start_time + timelimit) {
	    if (nokill) {
		if (verbose) {
		    fprintf(stderr, "%s: I've waited more than %s for %s.  It must finish in the background.",
			    progname, argv[1], argv[2]);
		}
		exit(127);
	    } else {
		kill_child(childpid);
	    }
	    break;
	}

	sleep (start_time + timelimit - now);
    }

    {
	int	status;
	if (0>waitpid(childpid, &status, 0)) {
	    /* the child should already be dead.  What's the deal? */
	    fprintf(stderr, "%s: very strange.  Error while waiting for child process\n", progname);
	    exit (1);
	} else {
	    if (verbose)
		fprintf(stderr, "%s: child `%s' exit status 0x%x\n",
			progname, childprogname, status);
	    if (WIFEXITED(status))
		exit(WEXITSTATUS(status));
	    else
		exit(1);		/* what, am I going to signal myself?! */
	}
    }
}
