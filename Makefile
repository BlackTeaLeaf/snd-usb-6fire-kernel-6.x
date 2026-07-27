obj-m += snd-usb-6fire-fixed.o
snd-usb-6fire-fixed-y := chip.o comm.o control.o firmware.o midi.o pcm.o substream.o urbs.o

KERNELRELEASE ?= $(shell uname -r)
KDIR ?= /lib/modules/$(KERNELRELEASE)/build
PWD ?= $(shell pwd)

default: modules
install: modules_install
	
modules modules_install clean:
	$(MAKE) -C $(KDIR) M=$(PWD) $@

kremove:
	for x in $(shell find /lib/modules/$(shell uname -r) -iname snd-usb-6fire-fixed.ko\*); do \
	rm -f $$x; \
	done
