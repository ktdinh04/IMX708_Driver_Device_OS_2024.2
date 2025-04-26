
obj-m := imx708_v4l2.o

imx708_v4l2-objs := imx708_v4l2.c


KERNEL_BUILD := /lib/modules/$(shell uname -r)/build


all:
	make -C $(KERNEL_BUILD) M=$(shell pwd) modules
clean:
	make -C $(KERNEL_BUILD) M=$(shell pwd) clean