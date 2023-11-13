ARCH := $(shell uname -m)
ifeq ("$(ARCH)", "x86_64")
	ARCH := x86
endif

obj-m += mwait.o 
mwait-y := measure.o sysfs-helper.o arch/$(ARCH)/measure.o arch/$(ARCH)/sysfs.o

PWD := $(CURDIR) 
ifdef PROJECT_BUILD_DIR
	BUILD_DIR := ../linuxMWAIT
else
	BUILD_DIR := /lib/modules/$(shell uname -r)/build
endif

all: 
	make -C $(BUILD_DIR) M=$(PWD) modules 

clean: 
	make -C $(BUILD_DIR) M=$(PWD) clean