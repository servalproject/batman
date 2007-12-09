UNAME:=$(shell uname -r)
KERNEL26:=2.6
KERNELVERSION:=$(findstring $(KERNEL26),$(UNAME))

ifeq ($(KERNELVERSION),$(KERNEL26))
obj-m += mod_batgat.o
mod_batgat-objs := gateway.o hash.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

else
TARGET:=mod_batgat
INCLUDE:=-I/lib/modules/$(UNAME)/build/include -I/usr/src/kernel-headers-$(UNAME)/include
CFLAGS:=-D__KERNEL__ -DMODULE -O2 -Wall $(INCLUDE)
CC:=gcc

all: mod_batgat.o

mod_batgat.o: gateway24.o hash.o
	$(LD) -r $^ -o $@


clean:
	rm -f *.o *~
endif