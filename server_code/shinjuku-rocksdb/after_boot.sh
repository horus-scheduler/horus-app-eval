#!/bin/sh

# Remove kernel modules
sudo rmmod pcidma
sudo rmmod dune

# Set huge pages
sudo sh -c 'for i in /sys/devices/system/node/node*/hugepages/hugepages-2048kB/nr_hugepages; do echo 4096 > $i; done'

# Unbind NICs
# Important: Modify the NIC name after -u accordingly
sudo ./deps/dpdk/tools/dpdk_nic_bind.py --force -u 0000:01:00.0
sudo ./deps/dpdk/tools/dpdk_nic_bind.py --force -u 0000:01:00.1

# Insert kernel modules
sudo insmod deps/dune/kern/dune.ko
sudo insmod deps/pcidma/pcidma.ko