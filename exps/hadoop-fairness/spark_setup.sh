#!/bin/bash

#TODO: arguments?

GIT_DIR=$(pwd)
cd /home/ubuntu

cd software
wget "http://d3kbcqa49mib13.cloudfront.net/spark-2.1.0.tgz"
tar -xvzf spark-2.1.0.tar.gz
cd ..
