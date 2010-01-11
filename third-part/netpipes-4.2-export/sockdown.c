/*

    $Id: sockdown.c,v 1.6 1998/06/12 19:59:18 thoth Exp $, part of
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

static char info[] = "sockdown: a network utility for sockets\nWritten 1995-98 by Robert Forsman <thoth@cis.ufl.edu>\n";
#include	<stdio.h>
#include	<string.h>
#include	<stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>

#include "common.h"


void				/* this is in version.c */
emit_version(/* char*, int */);


extern char *progname;

int main(argc, argv)
    int	argc;
    char **argv;
{
    int	fd;
    int	how;
    int	verbose=0;

    set_progname(argv[0]);

    if (argc>1&&
	0==strncmp(argv[1], "-verbose", strlen(argv[1]))) {
	verbose=1;
	argc--;
	argv++;
    }
    if (argc<2) {
	fd = 1;
	how = 1;
    } else {
	fd = atoi(argv[1]);
	if (argc<3) {
	    how = 1;
	} else {
	    if (0==strncmp("readonly", argv[2], strlen(argv[2]))) {
		how = 1;
	    } else if (0==strncmp("writeonly", argv[2], strlen(argv[2]))) {
		how = 0;
	    } else if (0==strncmp("totally", argv[2], strlen(argv[2]))) {
		how = 2;
	    } else {
		how = atoi(argv[2]);
	    }
	}
    }

    if (verbose) {
      emit_version("sockdown", 1995);
      fprintf(stderr,
	      "%s: Performing shutdown on descriptor %d with mode %d\n",
	      progname, fd, how);
    }

    if ( 0==shutdown(fd, how) ) {
	exit(0);
    } else {
	fprintf(stderr, "%s: Error %d during shutdown(%d, %d) of socket.  ",
		progname, errno, fd, how);
	perror("");
	exit(1);
    }
}
