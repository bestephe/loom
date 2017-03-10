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
sudo apt-get -y install python-yaml

mkdir -p /home/ubuntu/software
mkdir -p /home/ubuntu/storage
mkdir -p /home/ubuntu/workload
mkdir -p /home/ubuntu/logs/apps
mkdir -p /home/ubuntu/logs/hadoop

GIT_DIR=$(pwd)
cd /home/ubuntu
wget "http://pages.cs.wisc.edu/~akella/CS838/F15/assignment1/conf.tar.gz"
wget "http://pages.cs.wisc.edu/~akella/CS838/F15/assignment1/run.sh"
tar -xvzf conf.tar.gz 
cp $GIT_DIR/ubuntu_conf/* conf/

cd conf
sed -i s/MASTER_IP/$MASTER_IP/g core-site.xml 
sed -i s/MASTER_IP/$MASTER_IP/g hdfs-site.xml 
sed -i s/MASTER_IP/$MASTER_IP/g hive-site.xml
sed -i s/MASTER_IP/$MASTER_IP/g mapred-site.xml
sed -i s/MASTER_IP/$MASTER_IP/g yarn-site.xml

cd ..

#sed -i 's/home\/ubuntu\/logs\/hadoop/workspace\/logs\/hadoop/g' run.sh
sed -i 's/java-1.7.0/java-1.8.0/g' run.sh
#sed -i 's/home\/ubuntu/workspace/g' run.sh

cd software
wget "https://archive.apache.org/dist/hadoop/common/hadoop-2.6.0/hadoop-2.6.0.tar.gz"
tar -xvzf hadoop-2.6.0.tar.gz
cd ..

sudo mkfs -t ext4 /dev/sda4
sudo mount /dev/sda4 storage/
sudo chown -R ubuntu:ubuntu storage/

mkdir -p storage/data/local/nm
mkdir -p storage/data/local/tmp
mkdir -p storage/hdfs/hdfs_dn_dirs
mkdir -p storage/hdfs/hdfs_nn_dir

echo "Edit /etc/hosts"
echo "Make the instances file"
echo "Set up password less connection"
