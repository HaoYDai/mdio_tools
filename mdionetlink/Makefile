obj-m := mdio-netlink.o
ccflags-y := -Iinclude

KDIR ?= /home/darren/seer-group/rz_g2l/kernel-5.10

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) ARCH=arm64 CROSS_COMPILE=aarch64-poky-linux- modules

install: all
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules_install

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean
