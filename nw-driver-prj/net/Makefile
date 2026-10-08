ifeq ($(KSRC),)
KSRC := /lib/modules/$(shell uname -r)/build
endif

net-y += emac.o phy.o emac_hw.o
obj-m += net.o

.PHONY: all clean

all:
	$(MAKE) -C $(KSRC) M=$(CURDIR) modules

clean:
	$(MAKE) -C $(KSRC) M=$(CURDIR) clean
	rm -f *.o *~

