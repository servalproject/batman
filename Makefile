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

CC_MIPS_KK_PATH =	/usr/src/openWrt/build/buildroot-ng/openwrt/staging_dir_mipsel/bin
CC_MIPS_KK =		$(CC_MIPS_KK_PATH)/mipsel-linux-uclibc-gcc
STRIP_MIPS_KK =		$(CC_MIPS_KK_PATH)/sstrip

CC_MIPS_WR_PATH =	/usr/src/openWrt/build/whiterussian/openwrt/staging_dir_mipsel/bin
CC_MIPS_WR =		$(CC_MIPS_WR_PATH)/mipsel-linux-uclibc-gcc
STRIP_MIPS_WR =		$(CC_MIPS_WR_PATH)/sstrip

CFLAGS_MIPS =	-Wall -O0 -g3
LDFLAGS_MIPS =	-lpthread

CC =			gcc
#CC =			mipsel-linux-uclibc-gcc
CFLAGS =		-Wall -O0 -g3
LDFLAGS =		-lpthread
#LDFLAGS =		-static -lpthread

UNAME=$(shell uname)

ifeq ($(UNAME),Linux)
OS_OBJ=	originator.o schedule.o posix-specific.o posix.o linux-specific.o linux.o allocate.o bitarray.o hash.o profile.o
endif

ifeq ($(UNAME),Darwin)
OS_OBJ=	originator.o schedule.o posix-specific.o posix.o bsd.o allocate.o bitarray.o hash.o profile.o
endif

ifeq ($(UNAME),FreeBSD)
OS_OBJ=	originator.o schedule.o posix-specific.o posix.o bsd.o allocate.o bitarray.o hash.o profile.o
endif

ifeq ($(UNAME),OpenBSD)
OS_OBJ=	originator.o schedule.o posix-specific.o posix.o bsd.o allocate.o bitarray.o hash.o profile.o
endif

LINUX_SRC_C= batman.c originator.c schedule.c posix-specific.c posix.c linux-specific.c linux.c allocate.c bitarray.c hash.c profile.c
LINUX_SRC_H= batman.h originator.h schedule.h batman-specific.h list-batman.h os.h allocate.h bitarray.h hash.h profile.h

all:	batmand

batmand:	batman.o $(OS_OBJ) Makefile batman.h
	$(CC) -o $@ batman.o $(OS_OBJ) $(LDFLAGS)


mips-kk:	batmand-mips-kk-static batmand-mips-kk-dynamic

batmand-mips-kk-static:		$(LINUX_SRC_C) $(LINUX_SRC_H) Makefile
	$(CC_MIPS_KK) $(CFLAGS_MIPS) -o $@ $(LINUX_SRC_C) $(LDFLAGS_MIPS) -static
	$(STRIP_MIPS_KK) $@

batmand-mips-kk-dynamic:	$(LINUX_SRC_C) $(LINUX_SRC_H) Makefile
	$(CC_MIPS_KK) $(CFLAGS_MIPS) -o $@ $(LINUX_SRC_C) $(LDFLAGS_MIPS)
	$(STRIP_MIPS_KK) $@

mips-wr:	batmand-mips-wr-static batmand-mips-wr-dynamic


batmand-mips-wr-static:		$(LINUX_SRC_C) $(LINUX_SRC_H) Makefile
	$(CC_MIPS_WR) $(CFLAGS_MIPS) -o $@ $(LINUX_SRC_C) $(LDFLAGS_MIPS) -static
	$(STRIP_MIPS_WR) $@

batmand-mips-wr-dynamic:	$(LINUX_SRC_C) $(LINUX_SRC_H) Makefile
	$(CC_MIPS_WR) $(CFLAGS_MIPS) -o $@ $(LINUX_SRC_C) $(LDFLAGS_MIPS)
	$(STRIP_MIPS_WR) $@

clean:
		rm -f batmand batmand-mips* *.o *~
