#!/bin/sh

# Remove kernel modules
sudo rmmod pcidma
sudo rmmod dune

# Set huge pages
sudo sh -c 'for i in /sys/devices/system/node/node*/hugepages/hugepages-2048kB/nr_hugepages; do echo 4096 > $i; done'

#insert kernel module for DPDK NIC driver
sudo modprobe uio
sudo insmod $RTE_SDK/$RTE_TARGET/kmod/igb_uio.ko

# Unbind NICs
# Important: Modify the NIC name after -u and after igb_uio accordingly
sudo ./deps/dpdk/tools/dpdk_nic_bind.py --force -u 0000:01:00.0
sudo ./deps/dpdk/tools/dpdk_nic_bind.py --force -u 0000:01:00.1
sudo ~/dpdk/dpdk-stable-16.11.1/tools/dpdk-devbind.py -b igb_uio 0000:01:00.0
sudo ~/dpdk/dpdk-stable-16.11.1/tools/dpdk-devbind.py -b igb_uio 0000:01:00.1

# Insert kernel modules
make
