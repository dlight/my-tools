/*

    version.c, part of
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


#include <stdio.h>
#include <string.h>

/**********************************************************************/

char *progname;

void set_progname(argv0)
     char *argv0;
{
    char	*cp;
    progname = argv0;
    if ((cp = strrchr(progname, '/')))
	progname = ++cp;
}

/**********************************************************************/

void emit_version(progtitle, beginyear)
    char *progtitle;
    int beginyear;
{
    int	thisyear=1998;
    fprintf(stderr, "%s; of netpipes version 4.2, ", progtitle);
    if (beginyear != thisyear) {
	fprintf(stderr, "Copyright (C) %d-%d Robert Forsman\n", beginyear, (thisyear/100 == beginyear/100) ? thisyear%100 : thisyear);
    } else {
	fprintf(stderr, "Copyright (C) %d Robert Forsman\n", thisyear);
    }
    fprintf(stderr, "\
%s comes with ABSOLUTELY NO WARRANTY;\n\
This is free software, and you are welcome to redistribute it\n\
under the GNU General Public License as published by the Free\n\
Software Foundation; either version 2 of the License, or\n\
(at your option) any later version.\n", progtitle);
}
