# Makefile for the dbf2mysql-utility
# Maarten Boekhold (boekhold@cindy.et.tudelft.nl) 1995

# Set this to your C-compiler
CC=gcc

# set this to your install-program (what does Solaris have
# in /usr/sbin/install? SYSV install?)
INSTALL=/usr/bin/install

#AR=/usr/bin/ar
AR=ar

# Set this to whatever your compiler accepts. Nothing special is needed
#CFLAGS:=-g -Wall -pedantic -include /usr/include/mpatrol.h
CFLAGS:=-g -Wall
#CFLAGS:=-O2 -Wall

# Set this to make smaller binaries
STRIP=
#STRIP=-s

# Set this to your MySQL installation-path
MYSQLINC=-I/usr/include/mysql
MYSQLLIB=-L/usr/lib/mysql

# Set this to where you want the binary (no man-page yet, don't know
# how to write them)
INSTALLDIR=/usr/local/bin

# Set this if your system needs extra libraries
#
# For Solaris use:
#EXTRALIBS= -lmysys -lmystrings -lmysqlclient  -lm
# For mpatrol use (see http://www.cbmamiga.demon.co.uk/mpatrol/)
#EXTRALIBS= -lmpatrol -lbfd -liberty -lmysqlclient -lm
EXTRALIBS= -lmysqlclient -lm

# You should not have to change this unless your system doesn't have gzip
# or doesn't have it in the standard place (/usr/local/bin for ex.).
# Anyways, it is not needed for just a simple compile and install
RM=/bin/rm -f
GZIP=/bin/gzip
TAR=/bin/tar
BZIP2=/usr/bin/bzip2

# defines
VERSION=1.14
USE_OLD_FUNCTIONS=1

OBJS=dbf.o endian.o libdbf.a dbf2mysql.o mysql2dbf.o strtoupperlower.o

all: dbf2mysql mysql2dbf

libdbf.a: dbf.o endian.o strtoupperlower.o
	$(AR) rcs libdbf.a $^

dbf2mysql: dbf2mysql.o libdbf.a
	$(CC) $(CFLAGS) $(STRIP) -L. $(MYSQLLIB) -o $@ $< -ldbf \
		$(EXTRALIBS)

mysql2dbf: mysql2dbf.o libdbf.a
	$(CC) $(CFLAGS) $(STRIP) -L. $(MYSQLLIB) -o $@ $< -ldbf \
		$(EXTRALIBS)

%.o: %.c
	$(CC) $(CFLAGS) -DVERSION=\"$(VERSION)\" $(MYSQLINC) -c -o $@ $<

%.o: %.c %.h
	$(CC) $(CFLAGS) -DVERSION=\"$(VERSION)\" $(MYSQLINC) -c -o $@ $<

install: dbf2mysql
	$(INSTALL) -m 0755 -s dbf2mysql $(INSTALLDIR)
	$(INSTALL) -m 0755 -s mysql2dbf $(INSTALLDIR)

clean:
	$(RM) $(OBJS) dbf2mysql mysql2dbf

# the 'expand' is just for me, I use a tabstop of 4 for my editor, which
# makes lines in the README very long and ugly for people using 8, so
# I just expand them to spaces

dist:
#	-expand -4 README.tab > README
	(cd .. ; $(TAR) cf dbf2mysql-$(VERSION).tar dbf2mysql-$(VERSION)/*.[ch] \
	dbf2mysql-$(VERSION)/Makefile dbf2mysql-$(VERSION)/README \
        dbf2mysql-$(VERSION)/kbl2win.cvt ; \
	$(GZIP) -f9 dbf2mysql-$(VERSION).tar)

distbz2:
#	-expand -4 README.tab > README
	(cd .. ; $(TAR) cf dbf2mysql-$(VERSION).tar dbf2mysql-$(VERSION)/*.[ch] \
	dbf2mysql-$(VERSION)/Makefile dbf2mysql-$(VERSION)/README \
        dbf2mysql-$(VERSION)/kbl2win.cvt ; \
	$(BZIP2) dbf2mysql-$(VERSION).tar)

