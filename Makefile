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
OS_C=	 linux-specific.c linux.c
endif

ifeq ($(UNAME),Darwin)
OS_C=	bsd.c
endif

ifeq ($(UNAME),FreeBSD)
OS_C=	bsd.c
endif

ifeq ($(UNAME),OpenBSD)
OS_C=	bsd.c
endif

LINUX_SRC_C= batman.c originator.c schedule.c posix-specific.c posix.c allocate.c bitarray.c hash.c profile.c $(OS_C)
LINUX_SRC_H= batman.h originator.h schedule.h batman-specific.h list.h os.h allocate.h bitarray.h hash.h profile.h 


CC_MIPS_KK_BC_PATH =	/usr/src/openWrt/build/kamikaze-brcm63xx-2.6/kamikaze/staging_dir_mipsel/bin
CC_MIPS_KK_BC =		$(CC_MIPS_KK_BC_PATH)/mipsel-linux-uclibc-gcc
STRIP_MIPS_KK_BC =	$(CC_MIPS_KK_BC_PATH)/sstrip

CC_MIPS_KK_AT_PATH =	/usr/src/openWrt/build/kamikaze-atheros-2.6/kamikaze/staging_dir_mips/bin
CC_MIPS_KK_AT =		$(CC_MIPS_KK_AT_PATH)/mips-linux-uclibc-gcc
STRIP_MIPS_KK_AT =	$(CC_MIPS_KK_AT_PATH)/sstrip

CC_MIPS_WR_PATH =	/usr/src/openWrt/build/whiterussian/openwrt/staging_dir_mipsel/bin
CC_MIPS_WR =		$(CC_MIPS_WR_PATH)/mipsel-linux-uclibc-gcc
STRIP_MIPS_WR =		$(CC_MIPS_WR_PATH)/sstrip

CC_ARM_OE_PATH =	/usr/src/openEmbedded/stuff/build/akita/tmp/cross/bin
CC_ARM_OE =		$(CC_ARM_OE_PATH)/arm-linux-gcc
STRIP_ARM_OE =		$(CC_ARM_OE_PATH)/arm-linux-strip


REVISION=		$(shell svn info | grep Revision | awk '{print $$2}')
REVISION_VERSION=	\"\ rv$(REVISION)\"
IPKG_BUILD_PATH=	/home/batman/build/ipkg-build

BATMAN_GENERATION=	$(shell grep "^\#define SOURCE_VERSION " batman.h | sed -e '1p' -n | awk -F '"' '{print $$2}' | awk '{print $$1}')
BATMAN_VERSION=		$(shell grep "^\#define SOURCE_VERSION " batman.h | sed -e '1p' -n | awk -F '"' '{print $$2}' | awk '{print $$2}')
BATMAN_RELEASE=		$(shell grep "^\#define SOURCE_VERSION " batman.h | sed -e '1p' -n | awk -F '"' '{print $$2}' | awk '{print $$3}')
BATMAN_TRAILER=		$(shell grep "^\#define SOURCE_VERSION " batman.h | sed -e '1p' -n | awk -F '"' '{print $$2}' | awk '{print $$4}')
BATMAN_STRING=		begin:$(BATMAN_GENERATION):$(BATMAN_VERSION):$(BATMAN_RELEASE):$(BATMAN_TRAILER):end

IPKG_VERSION=		$(BATMAN_VERSION)$(BATMAN_RELEASE)-rv$(REVISION)

FILE_NAME=		batmand_$(BATMAN_VERSION)$(BATMAN_RELEASE)-rv$(REVISION)_$@
CURRENT=		batmand_$(BATMAN_VERSION)$(BATMAN_RELEASE)-current_$@

IPKG_DEPENDS=		"kmod-tun libpthread"

IPKG_BUILD=		ln -f $(FILE_NAME) $(IPKG_BUILD_PATH)/ipkg-target/usr/sbin/batmand && \
			$(IPKG_BUILD_PATH)/ipkg-make-control.sh   $(IPKG_BUILD_PATH)/ipkg-target $(FILE_NAME).ipk  batmand  $(IPKG_VERSION)


LINK_AND_TAR=		tar czvf $(FILE_NAME).tgz $(FILE_NAME) && \
			mkdir -p dl/misc && \
			ln -f $(FILE_NAME)* dl/misc/


all:	batmand


batmand:	$(LINUX_SRC_C) $(LINUX_SRC_H) Makefile
	$(CC) $(CFLAGS) -DREVISION_VERSION=$(REVISION_VERSION) -o $@ $(LINUX_SRC_C) $(LDFLAGS) -static



test:
	$(HELLO_WORLD)
	echo $(BATMAN_VERSION)
	echo $(BATMAN_STRING)
	echo IPKG_VERSION: $(IPKG_VERSION)

long:	sources i386 mipsel-kk-bc mips-kk-at mipsel-wr arm-oe

sources:
	mkdir -p $(FILE_NAME)
	cp $(LINUX_SRC_H) $(LINUX_SRC_C) Makefile $(FILE_NAME)/
	tar czvf $(FILE_NAME).tgz $(FILE_NAME)

	mkdir -p dl/misc
	ln -f $(FILE_NAME).tgz dl/misc/

	mkdir -p dl/sources
	ln -f $(FILE_NAME).tgz dl/sources/


i386: i386-gc-elf-32-lsb-static i386-gc-elf-32-lsb-dynamic

i386-gc-elf-32-lsb-static:	$(LINUX_SRC_C) $(LINUX_SRC_H) Makefile
	$(CC) $(CFLAGS) -DREVISION_VERSION=$(REVISION_VERSION) -o $(FILE_NAME) $(LINUX_SRC_C) $(LDFLAGS) -static
	$(STRIP) $(FILE_NAME)
	$(IPKG_BUILD) i386
	$(LINK_AND_TAR)

	mkdir -p dl/i386
	ln -f $(FILE_NAME).tgz dl/i386/


i386-gc-elf-32-lsb-dynamic:	$(LINUX_SRC_C) $(LINUX_SRC_H) Makefile
	$(CC) $(CFLAGS) -DREVISION_VERSION=$(REVISION_VERSION) -o $(FILE_NAME) $(LINUX_SRC_C) $(LDFLAGS)
	$(STRIP) $(FILE_NAME)
	$(IPKG_BUILD) i386 $(IPKG_DEPENDS)
	$(LINK_AND_TAR)


mipsel-kk-bc:	mipsel-kk-elf-32-lsb-static mipsel-kk-elf-32-lsb-dynamic

mipsel-kk-elf-32-lsb-static:	$(LINUX_SRC_C) $(LINUX_SRC_H) Makefile
	$(CC_MIPS_KK_BC) $(CFLAGS_MIPS) -DREVISION_VERSION=$(REVISION_VERSION) -o $(FILE_NAME) $(LINUX_SRC_C) $(LDFLAGS_MIPS) -static
	$(STRIP_MIPS_KK_BC) $(FILE_NAME)
	$(IPKG_BUILD) mipsel
	$(LINK_AND_TAR)

	mkdir -p dl/meshcube
	ln -f $(FILE_NAME).ipk dl/meshcube/


mipsel-kk-elf-32-lsb-dynamic:	$(LINUX_SRC_C) $(LINUX_SRC_H) Makefile
	$(CC_MIPS_KK_BC) $(CFLAGS_MIPS) -DREVISION_VERSION=$(REVISION_VERSION) -o $(FILE_NAME) $(LINUX_SRC_C) $(LDFLAGS_MIPS)
	$(STRIP_MIPS_KK_BC) $(FILE_NAME)
	$(IPKG_BUILD) mipsel $(IPKG_DEPENDS)
	$(LINK_AND_TAR)

	mkdir -p dl/netgear
	ln -f $(FILE_NAME).ipk dl/netgear/


mips-kk-at:	mips-kk-elf-32-msb-static mips-kk-elf-32-msb-dynamic

mips-kk-elf-32-msb-static:	$(LINUX_SRC_C) $(LINUX_SRC_H) Makefile
	$(CC_MIPS_KK_AT) $(CFLAGS_MIPS) -DREVISION_VERSION=$(REVISION_VERSION) -o $(FILE_NAME) $(LINUX_SRC_C) $(LDFLAGS_MIPS) -static
	$(STRIP_MIPS_KK_AT) $(FILE_NAME)
	$(IPKG_BUILD) mips
	$(LINK_AND_TAR)

	mkdir -p dl/fonera-freifunk
	ln -f $(FILE_NAME).tgz dl/fonera-freifunk/


mips-kk-elf-32-msb-dynamic:	$(LINUX_SRC_C) $(LINUX_SRC_H) Makefile
	$(CC_MIPS_KK_AT) $(CFLAGS_MIPS) -DREVISION_VERSION=$(REVISION_VERSION) -o $(FILE_NAME) $(LINUX_SRC_C) $(LDFLAGS_MIPS)
	$(STRIP_MIPS_KK_AT) $(FILE_NAME)
	$(IPKG_BUILD) mips $(IPKG_DEPENDS)
	$(LINK_AND_TAR)

	mkdir -p dl/fonera-kamikaze
	ln -f $(FILE_NAME).ipk dl/fonera-kamikaze/


mipsel-wr:	mipsel-wr-elf-32-lsb-static mipsel-wr-elf-32-lsb-dynamic

mipsel-wr-elf-32-lsb-static:	$(LINUX_SRC_C) $(LINUX_SRC_H) Makefile
	$(CC_MIPS_WR) $(CFLAGS_MIPS) -DREVISION_VERSION=$(REVISION_VERSION) -o $(FILE_NAME) $(LINUX_SRC_C) $(LDFLAGS_MIPS) -static
	$(STRIP_MIPS_WR) $(FILE_NAME)
	$(IPKG_BUILD) mipsel
	$(LINK_AND_TAR)

mipsel-wr-elf-32-lsb-dynamic:	$(LINUX_SRC_C) $(LINUX_SRC_H) Makefile
	$(CC_MIPS_WR) $(CFLAGS_MIPS) -DREVISION_VERSION=$(REVISION_VERSION) -o $(FILE_NAME) $(LINUX_SRC_C) $(LDFLAGS_MIPS)
	$(STRIP_MIPS_WR) $(FILE_NAME)
	$(IPKG_BUILD) mipsel $(IPKG_DEPENDS)
	$(LINK_AND_TAR)

	mkdir -p dl/wrt
	ln -f $(FILE_NAME).ipk dl/wrt/
	mkdir -p dl/buffalo
	ln -f $(FILE_NAME).ipk dl/buffalo/

arm-oe:		arm-oe-elf-32-lsb-static arm-oe-elf-32-lsb-dynamic

arm-oe-elf-32-lsb-static:	$(LINUX_SRC_C) $(LINUX_SRC_H) Makefile
	$(CC_ARM_OE) $(CFLAGS_MIPS) -DREVISION_VERSION=$(REVISION_VERSION) -o $(FILE_NAME) $(LINUX_SRC_C) $(LDFLAGS_MIPS) -static
	$(STRIP_ARM_OE) $(FILE_NAME)
	$(IPKG_BUILD) arm
	$(LINK_AND_TAR)	

	mkdir -p dl/arm
	ln -f $(FILE_NAME).ipk dl/arm/
	ln -f $(FILE_NAME).tgz dl/arm/

arm-oe-elf-32-lsb-dynamic:	$(LINUX_SRC_C) $(LINUX_SRC_H) Makefile
	$(CC_ARM_OE) $(CFLAGS_MIPS) -DREVISION_VERSION=$(REVISION_VERSION) -o $(FILE_NAME) $(LINUX_SRC_C) $(LDFLAGS_MIPS)
	$(STRIP_ARM_OE) $(FILE_NAME)
	$(IPKG_BUILD) arm kernel-module-tun
	$(LINK_AND_TAR)	

	mkdir -p dl/zaurus-akita
	ln -f $(FILE_NAME).ipk dl/zaurus-akita/

clean:
		rm -f batmand *.o


clean-long:
		rm -rf batmand_* batmand *.o
