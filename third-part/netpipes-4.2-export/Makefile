#    faucet and hose: network pipe utilities
#    Copyright (C) 1992,1993 Robert Forsman
#
#    This program is free software; you can redistribute it and/or modify
#    it under the terms of the GNU General Public License as published by
#    the Free Software Foundation; either version 2 of the License, or
#    (at your option) any later version.
#
#    This program is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    GNU General Public License for more details.
#
#    You should have received a copy of the GNU General Public License
#    along with this program; if not, write to the Free Software
#    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

# You probably want to change this:
#INSTROOT = ${HOME}
INSTROOT = /depot/collections0/netpipes4.1
#INSTROOT = /usr/local
INSTBIN = ${INSTROOT}/bin
INSTMAN = ${INSTROOT}/man

# For those who wish to use gcc instead of the vendor's compiler.
#CC=gcc -Wall

# This might be necessary for HPUX
#LDLIBS=-lBSD

# I'm told this is required for Sequent SysV
#LDLIBS=-lsocket -linet -lnsl

# This will be necessary for Sun Solaris (the abomination)
# Also necessary for SCO
#LDLIBS=-lsocket -lnsl

######################################################################
# CFLAGS
######################################################################

# For SunOS, add -DNO_MEMMOVE.  It doesn't have this very handy function.

# If you don't want to use (or don't have) fcntl(2) try the -DUSE_IOCTL.

# Add -DNO_STRERROR if your system doesn't have strerror(3)

# Add -DPOSIX_SIG for POSIX signal handling, or -DSYSV for reliable signals
# under SVR3.

# Add -DNO_SETSID if your system doesn't have the setsid() system call.
# (that system call is used by the -daemon argument to detach faucet from
# the controlling terminal)

# SunOS 4.1.4
#CFLAGS = -DUSE_IOCTL -DNO_MEMMOVE -DNO_STRERROR $(CDEBUGFLAGS)

# GNU libc Linux (i.e. RedHat 5.0)
#CFLAGS = -DPOSIX_SIG -DHAVE_INET_ATON $(CDEBUGFLAGS)

# Linux (developed with RedHat 4.2, libc5)
CFLAGS = -DUSE_IOCTL -DPOSIX_SIG -DHAVE_INET_ATON $(CDEBUGFLAGS)

# SGI
#CFLAGS = -DSYSV $(CDEBUGFLAGS)

# Digital Unix ?
#CFLAGS = -DSYSV $(CDEBUGFLAGS)

# Solaris 2.5
#CFLAGS = -DPOSIX_SIG $(CDEBUGFLAGS)

# FreeBSD
#CFLAGS = -DPOSIX_SIG $(CDEBUGFLAGS)

# AIX 4.1.4 and 3.2.5
#CFLAGS = -DPOSIX_SIG -DAIX $(CDEBUGFLAGS)

# DG/UX R4.11MU04 -- AViiON mc88110
#CFLAGS = -DPOSIX_SIG $(CDEBUGFLAGS)

# GNU Win32, the unix-like environment for Win95,
# but it doesn't have Unix domain sockets.
# Does not work yet (I suspect there are bugs in GNUWin32).
#CFLAGS = -DNOUNIXSOCKETS -DPOSIX_SIG $(CDEBUGFLAGS)

# gcc can handle both -O and -g at once
#CDEBUGFLAGS = -g # -Wall -DDEBUG
CDEBUGFLAGS = -O

######################################################################

FOBJS = faucet.o common.o version.o
HOBJS = hose.o common.o version.o memmove.o
SOBJS = sockdown.o version.o
GOBJS = getpeername.o version.o
TOBJS = timelimit.o version.o
EOBJS = encapsulate.o common.o version.o memmove.o
SSLOBJS = ssl-auth.o ssl-criteria.o common.o version.o memmove.o

SSLDIR = /usr/local/ssl
SSLINC = -I${SSLDIR}/include
#SSLLIB = -L${SSLDIR}/lib -lssl -lcrypto
SSLLIB = -L../SSLeay-0.8.1 -lssl -lcrypto

MANPAGES = netpipes.1 faucet.1 hose.1 \
	sockdown.1 getpeername.1 timelimit.1 encapsulate.1 \
	ssl-auth.1
PROGRAMS = faucet hose sockdown getpeername timelimit encapsulate

all	: ${PROGRAMS}

faucet	: ${FOBJS}
	${CC} ${CFLAGS} -o $@ ${FOBJS} ${LDLIBS}

hose	: ${HOBJS}
	${CC} ${CFLAGS} -o $@ ${HOBJS} ${LDLIBS}

sockdown: ${SOBJS}
	${CC} ${CFLAGS} -o $@ ${SOBJS} ${LDLIBS}

getpeername: ${GOBJS}
	${CC} ${CFLAGS} -o $@ ${GOBJS} ${LDLIBS}

timelimit: ${TOBJS}
	${CC} ${CFLAGS} -o $@ ${TOBJS} ${LDLIBS}

encapsulate: ${EOBJS}
	${CC} ${CFLAGS} -o $@ ${EOBJS} ${LDLIBS}

ssl-auth: ${SSLOBJS}
	${CC} ${CFLAGS} -o $@ ${SSLOBJS} ${LDLIBS} ${SSLLIB}

ssl-auth.o: ssl-auth.c
	${CC} ${CFLAGS} ${SSLINC} -c $<

ssl-criteria.o: ssl-criteria.c
	${CC} ${CFLAGS} ${SSLINC} -c $<

install : all
	test -d ${INSTROOT}  || mkdir ${INSTROOT}
	test -d ${INSTBIN}  || mkdir ${INSTBIN}
	cp ${PROGRAMS} ${INSTBIN}/
	- rm -f ${INSTBIN}/getsockname
	ln -s getpeername ${INSTBIN}/getsockname
	- [ -x ssl-auth ] && cp ssl-auth ${INSTBIN}/
	test -d ${INSTMAN}  || mkdir ${INSTMAN}
	test -d ${INSTMAN}/man1  || mkdir ${INSTMAN}/man1
	cp ${MANPAGES} ${INSTMAN}/man1/

#

clean	:
	rm -f ${FOBJS} ${HOBJS} ${SOBJS} ${GOBJS} ${TOBJS} ${EOBJS} ${SSLOBJS}

spotless: clean
	rm -f *~ core ${PROGRAMS}

#

common.o encapsulate.o faucet.o getpeername.o hose.o sockdown.o \
	ssl-auth.o ssl-criteria.o timelimit.o: common.h
encapsulate.o hose.o ssl-auth.o: memmove.h
ssl-auth.o ssl-critera.o: ssl-criteria.h

#
# These targets aren't for normal builders,
# just for me when I make a distribution.
#

HTML2MAN = perl ./html2man.perl

manpages:
	for i in *.html; do \
		$(HTML2MAN) < $$i > `basename $$i .html`.1; \
	done

#sslexp-readkey: sslexp-readkey.c
#	$(CC) ${CFLAGS} ${SSLINC} -o $@ sslexp-readkey.c ${LDLIBS} ${SSLLIB}

#sslexp-readcert: sslexp-readcert.c
#	$(CC) ${CFLAGS} ${SSLINC} -o $@ sslexp-readcert.c ${LDLIBS} ${SSLLIB}

#

.PHONY: release

release:
	- rm -rf release
	mkdir release
	cd release; \
	ln -s ../RCS .; co -r$(RELEASE) RCS/*,v; rm RCS ; chmod u+w *; \
	ln ../COPYING . ; make HTML2MAN="perl ../html2man.perl" manpages
	cd release; tar cf ../netpipes-$(RELEASE)-noexport.tar *
	cd release; tar cf ../netpipes-$(RELEASE)-export.tar `ls | egrep -v '^ssl.*\.[ch]$$'`
