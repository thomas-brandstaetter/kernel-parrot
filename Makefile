obj-m += parrot.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

install:
	sudo /sbin/insmod parrot.ko debug=1

uninstall:
	sudo /sbin/rmmod parrot.ko

checkinstalled:
	lsmod |grep parrot
