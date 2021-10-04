#!/bin/sh

# Fetch dep repositories
bash deps/fetch-deps.sh

# Apply patch to disable apic (did't work for the new gen intel CPU in our testbed) 
cp apic_disable.patch deps/dune
cd deps/dune
git apply apic_disable.patch
cd ../../

# Build required kernel modules.
sudo make -sj64 -C deps/dune
sudo make -sj64 -C deps/pcidma
sudo make -sj64 -C deps/dpdk config T=x86_64-native-linuxapp-gcc
sudo make -sj64 -C deps/dpdk
sudo make -sj64 -C deps/rocksdb static_lib
sudo make -sj64 -C deps/opnew

# Insert kernel modules
sudo insmod deps/dune/kern/dune.ko
sudo insmod deps/pcidma/pcidma.ko
