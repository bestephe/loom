#!/bin/bash

GIT_DIR=$(pwd)

sudo apt-get install -y maven

mkdir -p $HOME/storage/data/spark/rdds_shuffle
mkdir -p $HOME/logs/spark
mkdir -p $HOME/storage/data/spark/worker

cd $HOME/software
#wget "https://archive.apache.org/dist/spark/spark-2.3.1/spark-2.3.1-bin-hadoop2.6.tgz"
tar -xvzf spark-2.3.1-bin-hadoop2.6.tgz
cp -r $GIT_DIR/../../code/spark-terasort/ .
cd spark-terasort/
mvn install
cd ..
cd ..
cd


echo "Make changes to run.sh to change spark version"
