#!/bin/bash

mkdir -p /home/ubuntu/storage/data/spark/rdds_shuffle
mkdir -p /home/ubuntu/logs/spark
mkdir -p /home/ubuntu/storage/data/spark/worker

cd /home/ubuntu/software
wget "http://mirrors.ocf.berkeley.edu/apache/spark/spark-2.0.2/spark-2.0.2-bin-hadoop2.6.tgz"
tar -xvzf spark-2.0.2-bin-hadoop2.6.tgz
cd ..

cp instances conf/slaves

echo "Make changes to run.sh to change spark version"
