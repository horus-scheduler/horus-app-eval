
This repository contains the code for programs that run on end-hosts in in-network scheduling experiments.
The code is based on [Racksched repo](https://github.com/netx-repo/RackSched).

------
# Guide
## Downgrading Kernel Version to 4.4

### Download and install old kernel
> General note:  after reboot the 10G interface might get precedence and system could boot using that. In that case we get locked out!
> To fix this, make sure that only the primary interface is up in interface defult setting: /etc/network/interfaces.
Based on these sources: 
https://serverascode.com/2019/05/17/install-and-boot-older-kernel-ubuntu.html

https://unix.stackexchange.com/questions/198003/set-default-kernel-in-grub

To install old kernel:

```
sudo apt install linux-image-4.4.0-142-generic
```

Hold:
Since Ubuntu regularly wants to upgrade you to a newer kernel, you can apt-mark the package so it doesn't get removed again
```
sudo apt-mark hold linux-image-4.4.0-142-generic
```
Based on this link, need to install headers and extras (had ethernet problem before this, not sure if necessary):
https://askubuntu.com/questions/798975/no-network-no-usb-after-kernel-update-in-14-04

```
sudo apt-get install linux-headers-4.4.0-142-generic
sudo apt-get install linux-image-extra-4.4.0-142-generic
```
### Update GRUB

Comment out your current default grub in `/etc/default/grub` and replace it with the sub-menu's `$menuentry_id_option` from output of this command:
`grep submenu /boot/grub/grub.cfg`
followed by '>', followed by the selected kernel's `$menuentry_id_option`. (output of this command: 
`grep gnulinux /boot/grub/grub.cfg`).

Example GRUB_DEFAULT:
```
"gnulinux-advanced-b9283994-ad47-412a-8662-81957a75ab4d>gnulinux-4.4.0-142-generic-advanced-b9283994-ad47-412a-8662-81957a75ab4d"
```
Update grub to make the changes. For Debian this is done like so:

```
$ sudo update-grub
```

> **Reverting to latest version:**
To revert to latest kernel just need to uncomment the line 
#GRUB_DEFAULT=0 in /etc/default/grub again.

Then reboot the system.

### Install the default NIC  drivers
Without this step, the default ethernet did not show up after reboot. 

Follow these instructions to install the driver manually:
https://askubuntu.com/questions/1067564/intel-ethernet-i219-lm-driver-in-ubuntu-16-04

It takes a couple of minuets for system to get DNS settings. Manually Add these lines to `/etc/resolv.conf` (with sudo access):
```
nameserver 142.58.233.11

nameserver 142.58.200.2

search cmpt.sfu.ca
```

> If keyboard is connected, the system won't boot until you press F1! That is because of the Dell's Cover openned alert! (not resolved)
>https://www.dell.com/support/kbdoc/en-ca/000150875/how-to-reset-or-remove-an-alert-cover-was-previously-removed-message-that-appears-when-starting-a-dell-optiplex-computer

## Dependencies and Environment
```
apt-get install libconfig-dev libnuma-dev
apt-get install liblz4-dev libsnappy-dev libtool-bin

```
```
cd ./server_code/shinjuku-rocksdb/deps/
./fetch-deps.sh
```

### Fix Dune hardware compatibility issue:

Sumamry of the issue and root cause (as far as we know) are below. TD;LR To run our nsl-5* machines Intel(R) Xeon(R) E-2186G CPU, we disable Dune's APIC Interrupt calls. Also, we disable the preemption mechanism in Shinjuku (it's by design;  we did not intend to evaluate the server scheduler).

Apply the patch "apic_disable.patch" in the deps/dune folder.
Other modifications are already done in shinjuku's dp folder.

>When running Shinjuku it freezes when calling the init function of Dune.  
It freezes just after  [this line](https://github.com/kkaffes/dune/blob/78c6679a993b9e014d0f7deb030dc5bbd0abe0b8/libdune/entry.c#L481).
CPU gets stock after creating vCPU.

>For Dune, [this repo](https://github.com/ix-project/dune) works fine on our machine but the one Shinjuku uses is from this repo [this repo](https://github.com/kkaffes/dune). The problem is that the second repo adds some new APIs and functionalities to Dune that are necessary for Shinjuku so it is not possible to simply use the first Dune. 

>The issue was related to the usage of Advanced Programmable Interrupt Controllers (APIC) in Dune. It seems like there are multiple operations on the "hardware-specific register" and the address of these registers and values were hardcoded in Dune codes. I think there might be changes in these hardware-specific registers in the next-gen Intel CPUs and that's why it worked on Ramses but didn't work on nsl-55 (the CPUs are from different year/generations).


### Disable KASLR

Dune does not support kernel address space layout randomization (KASLR). For newer kernels, you must specify the nokaslr parameter in the kernel command line. Check before inserting the module by executing  `cat /proc/cmdline`. If the command line does not include the nokaslr parameter, then you must add it. In order to add it in Ubuntu-based distributions, you must:

-   edit the file  `/etc/default/grub`,
-   append  `nokaslr`  in the  `GRUB_CMDLINE_LINUX_DEFAULT`  option,
-   execute  `sudo grub-mkconfig -o /boot/grub/grub.cfg`, and
-   reboot the system.

Execute the following scripts:
```
#!/bin/sh

# Remove kernel modules
sudo rmmod pcidma
sudo rmmod dune

# Set huge pages
sudo sh -c 'for i in /sys/devices/system/node/node*/hugepages/hugepages-2048kB/nr_hugepages; do echo 4096 > $i; done'

# Unbind NICs
sudo ./deps/dpdk/tools/dpdk_nic_bind.py --force -u <NIC>

make -sj64 -C deps/dune
make -sj64 -C deps/pcidma
make -sj64 -C deps/dpdk config T=x86_64-native-linuxapp-gcc
make -sj64 -C deps/dpdk
make -sj64 -C deps/rocksdb static_lib
make -sj64 -C deps/opnew
# Insert kernel modules
sudo insmod deps/dune/kern/dune.ko
sudo insmod deps/pcidma/pcidma.ko

# Create RocksDB database
make -C db
cd db
#rm -r my_db
./create_db
cd ../

# Create clean copy of DB.
rm -r /tmp/my_db
cp -r db/my_db /tmp/my_db

```

### Dependencies for client machine 
> This is only needed for the machine that runs DPDK client.

Setup the DPDK environments variables:
```
export RTE_SDK=/home/pyassini/dpdk/dpdk-stable-16.11.1

export RTE_TARGET=x86_64-native-linuxapp-gcc
```

```
cd client_code/client/tools
./tools.sh install_dpdk
```



## Build and run
> Note that we made changes to DPDK client and Server's checksum checking inorder to make it work without switch. Modify hardcoded MAC in "dpdk_client.c" or simply remove when using the hardware switch.

### For server machines:
```
cd ./server_code/shinjuku-rocksdb/
make clean
make -sj64
LD_PRELOAD=./deps/opnew/dest/libnew.so ./dp/shinjuku
```
### For client machines:
```
sudo sh -c 'for i in /sys/devices/system/node/node*/hugepages/hugepages-2048kB/nr_hugepages; do echo 4096 > $i; done'

#insert kernel module for DPDK NIC driver
sudo insmod $RTE_SDK/$RTE_TARGET/kmod/igb_uio.ko

# Bind NIC 

sudo ~/dpdk/dpdk-stable-16.11.1/tools/dpdk-devbind.py -b igb_uio <NIC>

cd ./client_code/client/
make
sudo ./build/dpdk_client -l 0, 1, 2
```
> -l option tells the DPDK wich lcores to use. Using all cores resuled in super fast memory fill and segmentation error.
