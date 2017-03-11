if [ $# -eq 0 ]
  then
    echo "Master ip is required"
    exit 1
fi

MASTER_IP=$1

sudo apt-get update --fix-missing
sudo apt-get -y install vim
sudo apt-get -y install openjdk-8-jdk
sudo apt-get -y install pdsh

mkdir -p /home/ubuntu2/software
mkdir -p /home/ubuntu2/storage
mkdir -p /home/ubuntu2/workload
mkdir -p /home/ubuntu2/logs/apps
mkdir -p /home/ubuntu2/logs/hadoop

GIT_DIR=$(pwd)
cd /home/ubuntu2
cp $GIT_DIR/ubuntu2_run.sh run.sh
cp -r $GIT_DIR/ubuntu2_conf conf/

#XXX: Not needed now that we use ubuntu2_run.sh
#sed -i s/ubuntu/ubuntu2/g run.sh

cd conf
sed -i s/MASTER_IP/$MASTER_IP/g core-site.xml 
sed -i s/MASTER_IP/$MASTER_IP/g hdfs-site.xml 
sed -i s/MASTER_IP/$MASTER_IP/g hive-site.xml
sed -i s/MASTER_IP/$MASTER_IP/g mapred-site.xml
sed -i s/MASTER_IP/$MASTER_IP/g yarn-site.xml

cd ..

#sed -i 's/home\/ubuntu2\/logs\/hadoop/workspace\/logs\/hadoop/g' run.sh
sed -i 's/java-1.7.0/java-1.8.0/g' run.sh
#sed -i 's/home\/ubuntu2/workspace/g' run.sh

cd software
wget "https://archive.apache.org/dist/hadoop/common/hadoop-2.6.0/hadoop-2.6.0.tar.gz"
tar -xvzf hadoop-2.6.0.tar.gz
cd ..

sudo parted /dev/sdb mklabel msdos
sudo parted -a opt /dev/sdb mkpart primary ext4 0% 100%
sudo mkfs -t ext4 /dev/sdb1
sudo mount /dev/sdb1 storage/
sudo chown -R ubuntu2:ubuntu2 storage/

sudo mkdir -p storage/data/local/nm
sudo mkdir -p storage/data/local/tmp
sudo mkdir -p storage/hdfs/hdfs_dn_dirs
sudo mkdir -p storage/hdfs/hdfs_nn_dir
sudo chown -R ubuntu2:ubuntu2 storage/

echo "Edit /etc/hosts"
echo "Make the instances file"
echo "Set up password less connection"
