obj-m := CavQMSerial_mod.o
CavQMSerial_mod-objs := CavQMSerial.o

KDIR := /lib/modules/$(shell uname -r)/build

all: clean
	$(MAKE) -C $(KDIR) M=$(shell pwd) modules

clean:
	$(MAKE) -C $(KDIR) M=$(shell pwd) clean
