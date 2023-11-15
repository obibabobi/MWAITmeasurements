ARCH := $(shell uname -m)
ifeq ("$(ARCH)", "x86_64")
	ARCH := x86
endif

obj-m += mwait.o 
mwait-y := measure.o sysfs.o arch/$(ARCH)/measure.o arch/$(ARCH)/sysfs.o
ccflags-y=-I$(PWD)/include -I$(PWD)/arch/$(ARCH)/include

ifdef PROJECT_BUILD_DIR
	BUILD_DIR := ../linuxMWAIT
else
	BUILD_DIR := /lib/modules/$(shell uname -r)/build
endif

all: 
	$(MAKE) -C $(BUILD_DIR) M=$(CURDIR) modules

clean: 
	$(MAKE) -C $(BUILD_DIR) M=$(CURDIR) clean
