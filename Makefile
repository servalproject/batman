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

CC_MIPS_KK_BC_PATH =	/usr/src/openWrt/build/kamikaze-brcm63xx-2.6/kamikaze/staging_dir_mipsel/bin
CC_MIPS_KK_BC =		$(CC_MIPS_KK_BC_PATH)/mipsel-linux-uclibc-gcc
STRIP_MIPS_KK_BC =	$(CC_MIPS_KK_BC_PATH)/sstrip

CC_MIPS_KK_AT_PATH =	/usr/src/openWrt/build/kamikaze-atheros-2.6/kamikaze/staging_dir_mips/bin
CC_MIPS_KK_AT =		$(CC_MIPS_KK_AT_PATH)/mips-linux-uclibc-gcc
STRIP_MIPS_KK_AT =	$(CC_MIPS_KK_AT_PATH)/sstrip

#this was intended for meshcubes but turned out to do the same as CC_MIPS_KK_BC
#CC_MIPS_KK_AU_PATH =	/usr/src/openWrt/build/kamikaze-au1000-2.6/kamikaze/staging_dir_mipsel/bin
#CC_MIPS_KK_AU =		$(CC_MIPS_KK_AU_PATH)/mipsel-linux-uclibc-gcc
#STRIP_MIPS_KK_AU =	$(CC_MIPS_KK_AU_PATH)/sstrip

CC_MIPS_WR_PATH =	/usr/src/openWrt/build/whiterussian/openwrt/staging_dir_mipsel/bin
CC_MIPS_WR =		$(CC_MIPS_WR_PATH)/mipsel-linux-uclibc-gcc
STRIP_MIPS_WR =		$(CC_MIPS_WR_PATH)/sstrip

CFLAGS_MIPS =	-Wall -O0 -g3
LDFLAGS_MIPS =	-lpthread

CC =			gcc
STRIP=			strip
#CC =			mipsel-linux-uclibc-gcc
CFLAGS =		-Wall -O0 -g3
LDFLAGS =		-lpthread
#LDFLAGS =		-static -lpthread

UNAME=			$(shell uname)




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

REVISION=		$(shell svn info | grep Revision | awk '{print $$2}')

IPKG_BUILD_PATH=	/home/batman/build/ipkg-build

BATMAN_GENERATION=	$(shell grep "^\#define SOURCE_VERSION " batman.h | sed -e '1p' -n | awk -F '"' '{print $$2}' | awk '{print $$1}')
BATMAN_VERSION=		$(shell grep "^\#define SOURCE_VERSION " batman.h | sed -e '1p' -n | awk -F '"' '{print $$2}' | awk '{print $$2}')
BATMAN_RELEASE=		$(shell grep "^\#define SOURCE_VERSION " batman.h | sed -e '1p' -n | awk -F '"' '{print $$2}' | awk '{print $$3}')
BATMAN_TRAILER=		$(shell grep "^\#define SOURCE_VERSION " batman.h | sed -e '1p' -n | awk -F '"' '{print $$2}' | awk '{print $$4}')
BATMAN_STRING=		begin:$(BATMAN_GENERATION):$(BATMAN_VERSION):$(BATMAN_RELEASE):$(BATMAN_TRAILER):end

IPKG_VERSION=		$(BATMAN_GENERATION)-$(BATMAN_VERSION)$(BATMAN_RELEASE)-rv$(REVISION)
FILE_NAME=		batmand_$(BATMAN_GENERATION)-$(BATMAN_VERSION)$(BATMAN_RELEASE)-rv$(REVISION)_$@


IPKG_BUILD=		ln -f $(FILE_NAME) $(IPKG_BUILD_PATH)/ipkg-target/usr/sbin/batmand && \
			$(IPKG_BUILD_PATH)/ipkg-make-control.sh   $(IPKG_BUILD_PATH)/ipkg-target $(FILE_NAME).ipk  batmand  $(IPKG_VERSION) 


LINK_AND_TAR=		tar czvf $(FILE_NAME).tgz $(FILE_NAME) 

# && \
# 			ln -f batmand-$@ $(REVISION).batmand-$@ && \
# 			ln -f batmand-$@.tgz $(REVISION).batmand-$@.tgz && \
# 			ln -f $(REVISION).batmand-$@.ipk batmand-$@.ipk


all:	batmand


batmand:	batman.o $(OS_OBJ) $(LINUX_SRC_C) $(LINUX_SRC_H) Makefile
	$(CC) -o $@ batman.o $(OS_OBJ) $(LDFLAGS)


test:
	$(HELLO_WORLD)
	echo $(BATMAN_VERSION)
	echo $(BATMAN_STRING)
	echo IPKG_VERSION: $(IPKG_VERSION)

long:	i386 mipsel-kk-bc mips-kk-at mipsel-wr


i386: i386-gc-elf-32-lsb-static i386-gc-elf-32-lsb-dynamic

i386-gc-elf-32-lsb-static:	$(LINUX_SRC_C) $(LINUX_SRC_H) Makefile
	$(CC) $(CFLAGS) -o $(FILE_NAME) $(LINUX_SRC_C) $(LDFLAGS) -static
	$(STRIP) $(FILE_NAME) 
	$(IPKG_BUILD) i386 
	$(LINK_AND_TAR)	

i386-gc-elf-32-lsb-dynamic:	$(LINUX_SRC_C) $(LINUX_SRC_H) Makefile
	$(CC) $(CFLAGS) -o $(FILE_NAME) $(LINUX_SRC_C) $(LDFLAGS)
	$(STRIP) $(FILE_NAME)
	$(IPKG_BUILD) i386 
	$(LINK_AND_TAR)	

	
mipsel-kk-bc:	mipsel-kk-elf-32-lsb-static mipsel-kk-elf-32-lsb-dynamic

mipsel-kk-elf-32-lsb-static:	$(LINUX_SRC_C) $(LINUX_SRC_H) Makefile
	$(CC_MIPS_KK_BC) $(CFLAGS_MIPS) -o $(FILE_NAME) $(LINUX_SRC_C) $(LDFLAGS_MIPS) -static
	$(STRIP_MIPS_KK_BC) $(FILE_NAME)
	$(IPKG_BUILD) mipsel
	$(LINK_AND_TAR)	


mipsel-kk-elf-32-lsb-dynamic:	$(LINUX_SRC_C) $(LINUX_SRC_H) Makefile
	$(CC_MIPS_KK_BC) $(CFLAGS_MIPS) -o $(FILE_NAME) $(LINUX_SRC_C) $(LDFLAGS_MIPS)
	$(STRIP_MIPS_KK_BC) $(FILE_NAME)
	$(IPKG_BUILD) mipsel
	$(LINK_AND_TAR)	


mips-kk-at:	mips-kk-elf-32-msb-static mips-kk-elf-32-msb-dynamic

mips-kk-elf-32-msb-static:	$(LINUX_SRC_C) $(LINUX_SRC_H) Makefile
	$(CC_MIPS_KK_AT) $(CFLAGS_MIPS) -o $(FILE_NAME) $(LINUX_SRC_C) $(LDFLAGS_MIPS) -static
	$(STRIP_MIPS_KK_AT) $(FILE_NAME)
	$(IPKG_BUILD) mips
	$(LINK_AND_TAR)	
	


mips-kk-elf-32-msb-dynamic:	$(LINUX_SRC_C) $(LINUX_SRC_H) Makefile
	$(CC_MIPS_KK_AT) $(CFLAGS_MIPS) -o $(FILE_NAME) $(LINUX_SRC_C) $(LDFLAGS_MIPS)
	$(STRIP_MIPS_KK_AT) $(FILE_NAME)
	$(IPKG_BUILD) mips
	$(LINK_AND_TAR)	


mipsel-wr:	mipsel-wr-elf-32-lsb-static mipsel-wr-elf-32-lsb-dynamic

mipsel-wr-elf-32-lsb-static:	$(LINUX_SRC_C) $(LINUX_SRC_H) Makefile
	$(CC_MIPS_WR) $(CFLAGS_MIPS) -o $(FILE_NAME) $(LINUX_SRC_C) $(LDFLAGS_MIPS) -static
	$(STRIP_MIPS_WR) $(FILE_NAME)
	$(IPKG_BUILD) mipsel
	$(LINK_AND_TAR)	

mipsel-wr-elf-32-lsb-dynamic:	$(LINUX_SRC_C) $(LINUX_SRC_H) Makefile
	$(CC_MIPS_WR) $(CFLAGS_MIPS) -o $(FILE_NAME) $(LINUX_SRC_C) $(LDFLAGS_MIPS)
	$(STRIP_MIPS_WR) $(FILE_NAME)
	$(IPKG_BUILD) mipsel
	$(LINK_AND_TAR)	

clean:
		rm -f batmand *.o *~


clean-long:
		rm -f $(FILE_NAME)*

clean-long-all:
		rm -f batmand_*
