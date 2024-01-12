ARCH := $(shell uname -m)
ifeq ("$(ARCH)", "x86_64")
	ARCH := x86
endif
ifeq ("$(ARCH)", "aarch64")
	ARCH := arm
endif

obj-m += mwait.o 
mwait-y := measure.o sysfs.o arch/$(ARCH)/measure.o arch/$(ARCH)/sysfs.o
ccflags-y := -I$(src)/include -I$(src)/arch/$(ARCH)/include

PWD := $(CURDIR)

ifdef PROJECT_BUILD_DIR
	BUILD_DIR := $(PWD)/../linuxMWAIT
else
	BUILD_DIR := /lib/modules/$(shell uname -r)/build
endif

all: 
	$(MAKE) -C $(BUILD_DIR) M=$(PWD) modules

clean: 
	$(MAKE) -C $(BUILD_DIR) M=$(PWD) clean
