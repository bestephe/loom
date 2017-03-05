if [ $# -eq 0 ]
  then
    echo "Master ip is required"
    exit 1
fi

MASTER_IP=$1

sudo apt-get update --fix-missing
sudo apt-get -y install vim
sudo apt-get -y install openjdk-7-jdk
sudo apt-get -y install pdsh

mkdir -p /home/ubuntu/software
mkdir -p /home/ubuntu/workload
mkdir -p /home/ubuntu/logs/apps
mkdir -p /home/ubuntu/logs/hadoop

cd /home/ubuntu
wget "http://pages.cs.wisc.edu/~akella/CS838/F15/assignment1/conf.tar.gz"
wget "http://pages.cs.wisc.edu/~akella/CS838/F15/assignment1/run.sh"
tar -xvzf conf.tar.gz 

cd conf
sed -i s/MASTER_IP/$MASTER_IP/g core-site.xml 
sed -i s/MASTER_IP/$MASTER_IP/g hdfs-site.xml 
sed -i s/MASTER_IP/$MASTER_IP/g hive-site.xml
sed -i s/MASTER_IP/$MASTER_IP/g mapred-site.xml
sed -i s/MASTER_IP/$MASTER_IP/g yarn-site.xml

sed -i 's/home\/ubuntu\/storage\/hdfs\/hdfs_dn_dirs/mnt\/storage\/hdfs\/hdfs_dn_dirs/g' hdfs-site.xml
sed -i 's/home\/ubuntu\/storage\/hdfs\/hdfs_nn_dir/mnt\/storage\/hdfs\/hdfs_nn_dir/g' hdfs-site.xml

sed -i 's/home\/ubuntu\/storage\/data\/local\/nm/mnt\/storage\/data\/local\/nm/g' yarn-site.xml
#sed -i 's/home\/ubuntu\/logs\/apps/workspace\/logs\/apps/g' yarn-site.xml

sed -i 's/home\/ubuntu\/storage\/data\/local\/tmp/mnt\/storage\/data\/local\/tmp/g' core-site.xml

sed -i 's/<value>23552<\/value>/<value>102400<\/value>/g' yarn-site.xml
sed -i 's/<value>5<\/value>/<value>50<\/value>/g' yarn-site.xml

cd ..

#sed -i 's/home\/ubuntu\/logs\/hadoop/workspace\/logs\/hadoop/g' run.sh
#sed -i 's/java-1.7.0/java-1.8.0/g' run.sh
#sed -i 's/home\/ubuntu/workspace/g' run.sh

cd software
wget "https://archive.apache.org/dist/hadoop/common/hadoop-2.6.0/hadoop-2.6.0.tar.gz"
tar -xvzf hadoop-2.6.0.tar.gz
cd ..

sudo mkfs -t ext3 /dev/sda4
sudo mount /dev/sda4 /mnt
sudo chown -R ubuntu:ubuntu /mnt

mkdir -p /mnt/storage/data/local/nm
mkdir -p /mnt/storage/data/local/tmp
mkdir -p /mnt/storage/hdfs/hdfs_dn_dirs
mkdir -p /mnt/storage/hdfs/hdfs_nn_dir

echo "Edit /etc/hosts"
echo "Make the instances file"
echo "Set up password less connection"
