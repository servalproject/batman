UNAME:=$(shell uname -r)
LINUX26=2.6
LINUX_VERSION:=$(findstring $(LINUX26),$(UNAME))
REVISION :=	$(shell if [ -d .svn ]; then svn info | grep "Rev:" | sed -e '1p' -n | awk '{print $$4}'; else if [ -d ~/.svk ]; then echo $$(svk info | grep "Mirrored From" | awk '{print $$5}'); fi; fi)

include $(PWD)/Makefile.kbuild

ifneq ($(LINUX_VERSION),$(LINUX26))
TARGET:=batgat
INCLUDE:=-I/lib/modules/$(UNAME)/build/include -I/usr/src/kernel-headers-$(UNAME)/include
EXTRA_CFLAGS+=-D__KERNEL__ -DMODULE -O2 -Wall $(INCLUDE)
CC:=gcc
endif

ifeq ($(LINUX_VERSION),$(LINUX26))

all:
	make -C /lib/modules/$(shell uname -r)/build REVISION=$(REVISION) M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
else

clean:
	rm -f *.o *~
endif
