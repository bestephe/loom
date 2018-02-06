#!/bin/bash

# Setup YCSB
# From https://github.com/brianfrankcooper/YCSB/tree/master/memcached :
#
cd
git clone http://github.com/brianfrankcooper/YCSB.git
cd YCSB
mvn -pl com.yahoo.ycsb:memcached-binding -am clean package

