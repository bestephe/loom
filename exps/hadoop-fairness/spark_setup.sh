#!/bin/bash

#TODO: arguments?

GIT_DIR=$(pwd)
cd

cd software
#wget "http://d3kbcqa49mib13.cloudfront.net/spark-2.1.0.tgz"
tar -xvzf spark-2.1.0.tgz

cd spark-2.1.0
./build/mvn -Pyarn -Phadoop-2.6 -Dhadoop.version=2.6.0 -DskipTests clean package
cd ..
cd ..
