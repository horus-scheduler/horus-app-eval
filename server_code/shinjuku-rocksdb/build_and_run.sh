#!/bin/sh

# Create RocksDB database
make -C db
cd db
sudo rm -rf my_db || true
./create_db
cd ../

# Create clean copy of DB.
sudo rm -rf /tmp/my_db || true
cp -r db/my_db /tmp/my_db

# Build and run Shinjuku.
make clean
make -sj64
sudo LD_PRELOAD=./deps/opnew/dest/libnew.so ./dp/shinjuku
