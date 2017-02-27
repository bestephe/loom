#!/bin/bash

echo "This is not yet converted into a working script"
#exit 1

#NOTE: This commands should be run on both the client and server.  I think.

# Install memcached
sudo apt-get -y install memcached

# Install Oracle's Java (Could be an OpenJDK instead)
sudo add-apt-repository ppa:webupd8team/java
sudo apt-get update
sudo apt-get -y install oracle-java8-installer

# Install maven
sudo apt-get install maven

# Setup YCSB
# From https://github.com/brianfrankcooper/YCSB/tree/master/memcached :
#
# git clone http://github.com/brianfrankcooper/YCSB.git
# cd YCSB
# mvn -pl com.yahoo.ycsb:memcached-binding -am clean package
