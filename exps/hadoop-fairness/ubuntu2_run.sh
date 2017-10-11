#!/bin/bash

VER=2.6.0
TIMEOUT=5
THREADS=2

COMMON_VERSION=${COMMON_VERSION:-${VER}}
HDFS_VERSION=${HDFS_VERSION:-${VER}}
YARN_VERSION=${YARN_VERSION:-${VER}}
HIVE_VERSION=${HIVE_VERSION:-1.2.1}
TEZ_VERSION=${TEZ_VERSION:-0.7.1-SNAPSHOT-minimal}
SPARK_HADOOP_VERSION=2.0.2

ENV="JAVA_HOME=/usr/lib/jvm/java-1.8.0-openjdk-amd64 \
  YARN_CONF_DIR=/home/ubuntu2/conf \
  YARN_LOG_DIR=/home/ubuntu2/logs/hadoop \
  YARN_HOME=/home/ubuntu2/software/hadoop-${YARN_VERSION} \
  HADOOP_LOG_DIR=/home/ubuntu2/logs/hadoop \
  HADOOP_CONF_DIR=/home/ubuntu2/conf \
  HADOOP_USER_CLASSPATH_FIRST=1 \
  HADOOP_COMMON_HOME=/home/ubuntu2/software/hadoop-${COMMON_VERSION} \
  HADOOP_HDFS_HOME=/home/ubuntu2/software/hadoop-${HDFS_VERSION} \
  HADOOP_YARN_HOME=/home/ubuntu2/software/hadoop-${YARN_VERSION} \
  HADOOP_HOME=/home/ubuntu2/software/hadoop-${COMMON_VERSION} \
  HADOOP_BIN_PATH=/home/ubuntu2/software/hadoop-${COMMON_VERSION}/bin \
  HADOOP_SBIN=/home/ubuntu2/software/hadoop-${COMMON_VERSION}/bin \
  HIVE_HOME=/home/ubuntu2/software/hive-1.2.1 \
  TEZ_CONF_DIR=/home/ubuntu2/software/conf \
  TEZ_JARS=/home/ubuntu2/software/tez-${TEZ_VERSION} \
  SPARK_HOME=/home/ubuntu2/software/spark-${SPARK_HADOOP_VERSION}-bin-hadoop2.6 \
  SPARK_CONF_DIR=/home/ubuntu2/conf \
  SPARK_LOCAL_DIRS=/home/ubuntu2/storage/data/spark/rdds_shuffle \
  SPARK_LOG_DIR=/home/ubuntu2/logs/spark \
  SPARK_WORKER_DIR=/home/ubuntu2/storage/data/spark/worker \
  SPARK_MASTER_HOST=10.10.102.2 \
  SPARK_MASTER_PORT=9077 \
  SPARK_WORKER_PORT=9091"

  #XXX: SPARK_MASTER_HOST and SPARK_MASTER_PORT need to be defined in
  # spark-env.sh and here.

case "$1" in
  (-q|--quiet)
    for i in ${ENV}
    do
      export $i
    done
    ;;
  (*)
    echo "setting variables:"
    for i in $ENV
    do
      echo $i
      export $i
    done
    ;;
esac

export HADOOP_CLASSPATH=$HADOOP_HOME:$HADOOP_CONF_DIR:$HIVE_HOME:$TEZ_JARS/*:$TEZ_JARS/lib/*:
export HADOOP_HEAPSIZE=10240

export PATH=/home/ubuntu2/software/hadoop-${COMMON_VERSION}/bin:/home/ubuntu2/software/hadoop-${COMMON_VERSION}/sbin:$HIVE_HOME/bin:$SPARK_HOME/bin:$SPARK_HOME/sbin:$PATH
export LD_LIBRARY_PATH=${HADOOP_COMMON_HOME}/share/hadoop/common/lib/native/:${LD_LIBRARY_PATH}
export JAVA_LIBRARY_PATH=${LD_LIBRARY_PATH}

mount_fs(){
	printf "\n==== Mounting storage ! ====\n"
        sudo mount /dev/sdb1 storage/
	pdsh -R exec -f $THREADS -w ^instances ssh -o ConnectTimeout=$TIMEOUT %h '( . /home/ubuntu2/run.sh -q ; sudo mount /dev/sdb1 storage/;)'
	sleep 2
}

start_hdfs(){
	printf "\n==== START HDFS daemons ! ====\n"
	hadoop-daemon.sh start namenode
	pdsh -R exec -f $THREADS -w ^instances ssh -o ConnectTimeout=$TIMEOUT %h '( . /home/ubuntu2/run.sh -q ; hadoop-daemon.sh start datanode;)'
	hadoop dfsadmin -safemode leave
}

stop_hdfs(){
	printf "\n==== STOP HDFS daemons ! ====\n"
	pdsh -R exec -f $THREADS -w ^instances ssh -o ConnectTimeout=$TIMEOUT %h '( . /home/ubuntu2/run.sh -q ; hadoop-daemon.sh stop datanode;)'
	hadoop-daemon.sh stop namenode
}

start_yarn(){
	printf "\n===== START YARN daemons ! ====\n"
	yarn-daemon.sh start resourcemanager
	pdsh -R exec -f $THREADS -w ^instances ssh -o ConnectTimeout=$TIMEOUT %h '( . /home/ubuntu2/run.sh -q ; yarn-daemon.sh start nodemanager;)'
}
 
stop_yarn(){
	printf "\n==== STOP YARN daemons ! ====\n"
	pdsh -R exec -f $THREADS -w ^instances ssh -o ConnectTimeout=$TIMEOUT %h '( . /home/ubuntu2/run.sh -q ; yarn-daemon.sh stop nodemanager;)'
	yarn-daemon.sh stop resourcemanager
}

start_history_mr(){
	printf "\n==== START M/R history server ! ====\n"
	mr-jobhistory-daemon.sh	start historyserver
}

stop_history_mr(){
	printf "\n==== STOP M/R history server ! ====\n"
	mr-jobhistory-daemon.sh	stop historyserver
}

start_spark(){
	printf "\n==== START SPARK daemons ! ====\n"
	$SPARK_HOME/sbin/start-master.sh
	pdsh -R exec -f $THREADS -w ^instances ssh -o ConnectTimeout=$TIMEOUT %h '( . /home/ubuntu2/run.sh -q ; $SPARK_HOME/sbin/start-slave.sh --port $SPARK_WORKER_PORT spark://$SPARK_MASTER_HOST:$SPARK_MASTER_PORT;)'
}

stop_spark(){
	printf "\n==== STOP SPARK daemons ! ====\n"
	#$SPARK_HOME/sbin/stop-all.sh #XXX: Doesn't work
	pdsh -R exec -f $THREADS -w ^instances ssh -o ConnectTimeout=$TIMEOUT %h '( . /home/ubuntu2/run.sh -q ; $SPARK_HOME/sbin/stop-slave.sh;)'
	$SPARK_HOME/sbin/stop-master.sh
}

start_timeline_server(){
	printf "\n==== START timelineserver ! ====\n"
	yarn-daemon.sh start timelineserver
}

stop_timeline_server(){
	printf "\n==== STOP timelineserver ! ====\n"
	yarn-daemon.sh stop timelineserver
}

start_all(){
	mount_fs
	start_hdfs
        start_spark
	#start_yarn
	#start_timeline_server
	#start_history_mr
}

stop_all(){
	stop_hdfs
        stop_spark
	stop_yarn
	stop_timeline_server
	stop_history_mr
}

export -f start_hdfs
export -f start_spark
export -f start_yarn
export -f start_all
export -f stop_hdfs
export -f stop_spark
export -f stop_yarn
export -f stop_all
export -f start_history_mr
export -f stop_history_mr
export -f start_timeline_server
export -f stop_timeline_server
