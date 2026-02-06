ifneq ($(KERNELRELEASE),)
# kbuild part of makefile

# Optionally, include config file to allow out of tree kernel modules build
-include $(src)/.config

# Core FBTFT framebuffer subsystem
obj-$(CONFIG_FB_TFT)     += fbtft.o
fbtft-y                  += fbtft-core.o fbtft-sysfs.o fbtft-bus.o fbtft-io.o

# KeDei 6.2 display driver
obj-$(CONFIG_FB_KEDEI62) += fb_kedei62.o

else
# normal makefile
KDIR ?= /lib/modules/`uname -r`/build

default: .config
	$(MAKE) -C $(KDIR) M=$$PWD modules

.config:
	grep config Kconfig | cut -d' ' -f2 | sed 's@^@CONFIG_@; s@$$@=m@' > .config

install:
	$(MAKE) -C $(KDIR) M=$$PWD modules_install


clean:
	rm -rf *.o *~ core .depend .*.cmd *.ko *.mod.c .tmp_versions \
	       modules.order Module.symvers

endif
