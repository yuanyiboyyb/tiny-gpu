obj-m += tinygpu.o
tinygpu-y := tinygpu_pci.o

ccflags-y += -I$(src)/../common

KDIR ?= /lib/modules/$(shell uname -r)/build
BUILD_DIR ?= $(abspath $(CURDIR)/../build/driver)
TINYGPU_COMMON := $(abspath $(CURDIR)/../common)

.PHONY: all clean prepare

all: prepare
	$(MAKE) -C $(KDIR) M=$(BUILD_DIR) modules

prepare:
	mkdir -p $(BUILD_DIR)
	ln -sfn $(CURDIR)/tinygpu_pci.c $(BUILD_DIR)/tinygpu_pci.c
	ln -sfn $(CURDIR)/tinygpu_ioctl.h $(BUILD_DIR)/tinygpu_ioctl.h
	{ \
		echo 'obj-m += tinygpu.o'; \
		echo 'tinygpu-y := tinygpu_pci.o'; \
		echo 'ccflags-y += -I$(CURDIR)'; \
		echo 'ccflags-y += -I$(TINYGPU_COMMON)'; \
	} > $(BUILD_DIR)/Makefile

clean:
	@if [ -f $(BUILD_DIR)/Makefile ]; then \
		$(MAKE) -C $(KDIR) M=$(BUILD_DIR) clean; \
	fi
