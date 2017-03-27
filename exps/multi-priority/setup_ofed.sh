#!/bin/bash

cd
wget "http://content.mellanox.com/ofed/MLNX_OFED-4.0-1.0.1.0/MLNX_OFED_LINUX-4.0-1.0.1.0-ubuntu16.04-x86_64.tgz"
tar xfz MLNX_OFED_LINUX-4.0-1.0.1.0-ubuntu16.04-x86_64.tgz
cd MLNX_OFED_LINUX-4.0-1.0.1.0-ubuntu16.04-x86_64
sudo ./mlnxofedinstall --force
