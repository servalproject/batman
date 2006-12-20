#
# Copyright (C) 2006 BATMAN contributors
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of version 2 of the GNU General Public
# License as published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
# 02110-1301, USA
#

CC =		gcc
# CC =		mipsel-linux-uclibc-gcc
CFLAGS =	-Wall -O0 -g3
LDFLAGS =	-lpthread
#LDFLAGS =	-static -lpthread

UNAME=$(shell uname)

ifeq ($(UNAME),Linux)
OS_OBJ=	posix.o linux.o allocate.o
endif

ifeq ($(UNAME),Darwin)
OS_OBJ=	posix.o bsd.o allocate.o
endif

ifeq ($(UNAME),FreeBSD)
OS_OBJ=	posix.o bsd.o allocate.o
endif

ifeq ($(UNAME),OpenBSD)
OS_OBJ=	posix.o bsd.o allocate.o
endif

batmand:	batman.o $(OS_OBJ)
	$(CC) -o $@ batman.o $(OS_OBJ) $(LDFLAGS)

clean:
		rm -f batmand *.o *~
