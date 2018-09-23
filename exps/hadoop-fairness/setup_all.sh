#!/bin/bash

sudo -u ubuntu -H ./hadoop_setup.sh 10.10.101.2
sudo -u ubuntu -H ./spark_cloudlab_setup.sh

sudo -u ubuntu2 -H ./hadoop2_setup.sh 10.10.102.2
sudo -u ubuntu2 -H ./spark_cloudlab_setup.sh

# Crail Build
#GIT_DIR=$(pwd)
#cd ../../..
#git clone https://github.com/apache/incubator-crail.git
#cd incubator-crail
#mvn -DskipTests install
##CRAIL_TARBALL=../../../incubator-crail/assembly/target/crail-1.1-incubating-SNAPSHOT-bin.tar.gz


#cd ../
#git clone https://github.com/zrlio/crail-spark-io.git
#cd crail-spark-io
#mvn -DskipTests install
#cd $GIT_DIR/code/crail-spark-terasort/
#mvn install
#cd $GIT_DIR

sudo mkdir /logs
sudo chmod 777 /logs
sudo mkdir -p /dev/hugepages/ubuntu/{cache,data}
sudo mkdir -p /dev/hugepages/ubuntu2/{cache,data}
sudo chmod 777 /dev/hugepages/ubuntu
sudo chmod 777 /dev/hugepages/ubuntu2
sudo chown -R ubuntu:ubuntu /dev/hugepages/ubuntu
sudo chown -R ubuntu2:ubuntu2 /dev/hugepages/ubuntu2

sudo -u ubuntu -H ./crail_setup.sh 10.10.101.2
sudo -u ubuntu2 -H ./crail_setup.sh 10.10.102.2
