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
mkdir -p /home/ubuntu2/workload
mkdir -p /home/ubuntu2/logs/apps
mkdir -p /home/ubuntu2/logs/hadoop

cd /home/ubuntu2
wget "http://pages.cs.wisc.edu/~akella/CS838/F15/assignment1/conf.tar.gz"
wget "http://pages.cs.wisc.edu/~akella/CS838/F15/assignment1/run.sh"
tar -xvzf conf.tar.gz 

cd conf
sed -i s/MASTER_IP/$MASTER_IP/g core-site.xml 
sed -i s/MASTER_IP/$MASTER_IP/g hdfs-site.xml 
sed -i s/MASTER_IP/$MASTER_IP/g hive-site.xml
sed -i s/MASTER_IP/$MASTER_IP/g mapred-site.xml
sed -i s/MASTER_IP/$MASTER_IP/g yarn-site.xml

sed -i s/ubuntu/ubuntu2/g core-site.xml 
sed -i s/ubuntu/ubuntu2/g hdfs-site.xml 
sed -i s/ubuntu/ubuntu2/g hive-site.xml
sed -i s/ubuntu/ubuntu2/g mapred-site.xml
sed -i s/ubuntu/ubuntu2/g yarn-site.xml

sed -i 's/home\/ubuntu2\/storage\/hdfs\/hdfs_dn_dirs/mnt\/ubuntu2\/storage\/hdfs\/hdfs_dn_dirs/g' hdfs-site.xml
sed -i 's/home\/ubuntu2\/storage\/hdfs\/hdfs_nn_dir/mnt\/ubuntu2\/storage\/hdfs\/hdfs_nn_dir/g' hdfs-site.xml

sed -i 's/home\/ubuntu2\/storage\/data\/local\/nm/mnt\/ubuntu2\/storage\/data\/local\/nm/g' yarn-site.xml
#sed -i 's/home\/ubuntu2\/logs\/apps/workspace\/logs\/apps/g' yarn-site.xml

sed -i 's/home\/ubuntu2\/storage\/data\/local\/tmp/mnt\/ubuntu2\/storage\/data\/local\/tmp/g' core-site.xml

sed -i 's/<value>23552<\/value>/<value>102400<\/value>/g' yarn-site.xml
sed -i 's/<value>5<\/value>/<value>50<\/value>/g' yarn-site.xml

# Change the ports
sed -i s/50070/50071/g hdfs-site.xml
sed -i s/8088/8089/g yarn-site.xml
sed -i s/19888/19889/g mapred-site.xml
sed -i s/8188/8189/g yarn-site.xml

cd ..

#sed -i 's/home\/ubuntu2\/logs\/hadoop/workspace\/logs\/hadoop/g' run.sh
sed -i 's/java-1.7.0/java-1.8.0/g' run.sh
#sed -i 's/home\/ubuntu2/workspace/g' run.sh

cd software
wget "https://archive.apache.org/dist/hadoop/common/hadoop-2.6.0/hadoop-2.6.0.tar.gz"
tar -xvzf hadoop-2.6.0.tar.gz
cd ..

sudo parted /dev/sdb mklabel msdos
sudo parted -a opt /dev/sdb mkpart primary ext3 0% 100%
sudo mkfs -t ext3 /dev/sdb1
sudo mkdir -p /mnt/ubuntu2
sudo mount /dev/sdb1 /mnt/ubuntu2
sudo chown -R ubuntu2:ubuntu2 /mnt

mkdir -p /mnt/ubuntu2/storage/data/local/nm
mkdir -p /mnt/ubuntu2/storage/data/local/tmp
mkdir -p /mnt/ubuntu2/storage/hdfs/hdfs_dn_dirs
mkdir -p /mnt/ubuntu2/storage/hdfs/hdfs_nn_dir

echo "Edit /etc/hosts"
echo "Make the instances file"
echo "Set up password less connection"
