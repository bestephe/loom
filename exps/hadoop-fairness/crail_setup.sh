#!/bin/bash

if [ $# -eq 0 ]
  then
    echo "Master ip is required"
    exit 1
fi

IFACE=enp130s0
MASTER_IP=$1

IP=$(/sbin/ifconfig $IFACE | grep 'inet addr:' | cut -d: -f2 | awk '{ print $1}')
TAP_IP_PREFIX=$(echo $IP | cut -d. -f1,2,3)
TAP_IP_SUFFIX=$(echo $IP | cut -d. -f4)
#TAP_IP=$TAP_IP_PREFIX.$TAP_IP_SUFFIX
TAP_IP=10.10.101.$TAP_IP_SUFFIX

GIT_DIR=$(pwd)

# Crail Setup
CRAIL_DIR=$GIT_DIR/../../../incubator-crail
cd $HOME/software
#cp -r $CRAIL_DIR .
cd incubator-crail
mvn -DskipTests install

# Crail Install
cd ..
mkdir crail-1.1
cd crail-1.1
CRAIL_TARBALL=../incubator-crail/assembly/target/crail-1.1-incubating-SNAPSHOT-bin.tar.gz
cp $CRAIL_TARBALL .
tar -xvzf crail-1.1-incubating-SNAPSHOT-bin.tar.gz
cp ~/conf/crail* conf/
cp ~/conf/slaves conf/

# Crail Config
cd conf
mv crail-core-site.xml core-site.xml
sed -i s/MASTER_IP/$MASTER_IP/g core-site.xml
sed -i s/MASTER_IP/$MASTER_IP/g crail-site.conf
sed -i s/CHANGE_IFACE/$IFACE/g crail-site.conf
cd ../..

# crail-spark-io setup
#cp -r $GIT_DIR/../../../crail-spark-io/ .
cd crail-spark-io
mvn -DskipTests install
cd ..

# crail-spark-terasort
#cp -r $GIT_DIR/../../code/crail-spark-terasort/ .
cd crail-spark-terasort/
mvn -DskipTests install
cd libjsort
cmake .
make
cd ../..

#cd ..
#cd

