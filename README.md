# kernel-parrot

This kernel module has two parts. 
* A character device for read messages stored in the kernel module. The module param one_shot controls whether a single or multiple messages are read.
* A sysfs entry located at /sys/devices/virtual/parrot/parrot/fifo to store a message in the module.

# Testing

First build, install and check for loaded kernel module:
```
make 
sudo make install
make checkinstall
  ```
Then fill the kernel-fifo with messages
```
echo "Today is a good day to develop!" > /sys/devices/virtual/parrot/parrot/fifo
```
After filling the modules fifo we can read (pop) messages from the module. 
```
cat /dev/parrots
```
# Known issues

* There is a compile warning because the show param of DRIVER_ATTR is NULL. But this does not matter, since we use sysfs for writing only.
* Documentation follows doxygen format, but for kernel modules it is adviced to use kernel-doc format
