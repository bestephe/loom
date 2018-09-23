#!/bin/bash

VER=2.6.0
TIMEOUT=5
THREADS=2

COMMON_VERSION=${COMMON_VERSION:-${VER}}
HDFS_VERSION=${HDFS_VERSION:-${VER}}
YARN_VERSION=${YARN_VERSION:-${VER}}
HIVE_VERSION=${HIVE_VERSION:-1.2.1}
TEZ_VERSION=${TEZ_VERSION:-0.7.1-SNAPSHOT-minimal}
SPARK_HADOOP_VERSION=2.3.1
CRAIL_VERSION=1.1

ENV="JAVA_HOME=/usr/lib/jvm/java-8-oracle/jre/bin/java \
  YARN_CONF_DIR=/home/ubuntu/conf \
  YARN_LOG_DIR=/home/ubuntu/logs/hadoop \
  YARN_HOME=/home/ubuntu/software/hadoop-${YARN_VERSION} \
  HADOOP_LOG_DIR=/home/ubuntu/logs/hadoop \
  HADOOP_CONF_DIR=/home/ubuntu/conf \
  HADOOP_USER_CLASSPATH_FIRST=1 \
  HADOOP_COMMON_HOME=/home/ubuntu/software/hadoop-${COMMON_VERSION} \
  HADOOP_HDFS_HOME=/home/ubuntu/software/hadoop-${HDFS_VERSION} \
  HADOOP_YARN_HOME=/home/ubuntu/software/hadoop-${YARN_VERSION} \
  HADOOP_HOME=/home/ubuntu/software/hadoop-${COMMON_VERSION} \
  HADOOP_BIN_PATH=/home/ubuntu/software/hadoop-${COMMON_VERSION}/bin \
  HADOOP_SBIN=/home/ubuntu/software/hadoop-${COMMON_VERSION}/bin \
  HIVE_HOME=/home/ubuntu/software/hive-1.2.1 \
  TEZ_CONF_DIR=/home/ubuntu/software/conf \
  TEZ_JARS=/home/ubuntu/software/tez-${TEZ_VERSION} \
  SPARK_HOME=/home/ubuntu/software/spark-${SPARK_HADOOP_VERSION}-bin-hadoop2.6 \
  SPARK_CONF_DIR=/home/ubuntu/conf \
  SPARK_LOCAL_DIRS=/home/ubuntu/storage/data/spark/rdds_shuffle \
  SPARK_LOG_DIR=/home/ubuntu/logs/spark \
  SPARK_WORKER_DIR=/home/ubuntu/storage/data/spark/worker \
  SPARK_MASTER_HOST=10.10.101.2 \
  SPARK_MASTER_PORT=7077 \
  CRAIL_HOME=/home/ubuntu/software/crail-${CRAIL_VERSION}"

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

#XXX: probably not necessary because "If using build/mvn with no MAVEN_OPTS
# set, the script will automatically add the above options to the MAVEN_OPTS
# environment variable."
#export MAVEN_OPTS="-Xmx2g -XX:ReservedCodeCacheSize=512m -XX:MaxPermSize=512M"

export PATH=/home/ubuntu/software/hadoop-${COMMON_VERSION}/bin:/home/ubuntu/software/hadoop-${COMMON_VERSION}/sbin:$HIVE_HOME/bin:$SPARK_HOME/bin:$SPARK_HOME/sbin:$CRAIL_HOME/bin:$PATH
export LIBJSORT_PATH=/home/ubuntu/software/crail-spark-terasort/libjsort/libjsort.so
export LD_LIBRARY_PATH=${HADOOP_COMMON_HOME}/share/hadoop/common/lib/native/:${LIBJSORT_PATH}:${LD_LIBRARY_PATH}
export JAVA_LIBRARY_PATH=${LD_LIBRARY_PATH}
export CRAIL_EXTRA_JAVA_OPTIONS="-Xmx24G -Xmn16G"

mount_fs(){
	printf "\n==== Mounting storage ! ====\n"
	sudo mount /dev/sda4 storage/
	pdsh -R exec -f $THREADS -w ^instances ssh -o ConnectTimeout=$TIMEOUT %h '( . /home/ubuntu/run.sh -q ; sudo mount /dev/sda4 storage/;)'
	sleep 2
}

start_hdfs(){
	printf "\n==== START HDFS daemons ! ====\n"
	hadoop-daemon.sh start namenode
	pdsh -R exec -f $THREADS -w ^instances ssh -o ConnectTimeout=$TIMEOUT %h '( . /home/ubuntu/run.sh -q ; hadoop-daemon.sh start datanode;)'
	hadoop dfsadmin -safemode leave
}

stop_hdfs(){
	printf "\n==== STOP HDFS daemons ! ====\n"
	pdsh -R exec -f $THREADS -w ^instances ssh -o ConnectTimeout=$TIMEOUT %h '( . /home/ubuntu/run.sh -q ; hadoop-daemon.sh stop datanode;)'
	hadoop-daemon.sh stop namenode
}

start_yarn(){
	printf "\n===== START YARN daemons ! ====\n"
	yarn-daemon.sh start resourcemanager
	pdsh -R exec -f $THREADS -w ^instances ssh -o ConnectTimeout=$TIMEOUT %h '( . /home/ubuntu/run.sh -q ; yarn-daemon.sh start nodemanager;)'
}
 
stop_yarn(){
	printf "\n==== STOP YARN daemons ! ====\n"
	pdsh -R exec -f $THREADS -w ^instances ssh -o ConnectTimeout=$TIMEOUT %h '( . /home/ubuntu/run.sh -q ; yarn-daemon.sh stop nodemanager;)'
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
	pdsh -R exec -f $THREADS -w ^instances ssh -o ConnectTimeout=$TIMEOUT %h '( . /home/ubuntu/run.sh -q ; $SPARK_HOME/sbin/start-slave.sh spark://$SPARK_MASTER_HOST:$SPARK_MASTER_PORT;)'
}

stop_spark(){
	printf "\n==== STOP SPARK daemons ! ====\n"
	$SPARK_HOME/sbin/stop-all.sh #XXX: Doesn't work
	pdsh -R exec -f $THREADS -w ^instances ssh -o ConnectTimeout=$TIMEOUT %h '( . /home/ubuntu/run.sh -q ; $SPARK_HOME/sbin/stop-slave.sh;)'
	$SPARK_HOME/sbin/stop-master.sh
}

start_crail(){
	printf "\n==== START CRAIL daemons ! ====\n"
        $CRAIL_HOME/bin/start-crail.sh
}

stop_crail(){
	printf "\n==== STOP CRAIL daemons ! ====\n"
        $CRAIL_HOME/bin/stop-crail.sh
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
	#start_hdfs
        start_crail
        #start_spark
	#start_yarn
	#start_timeline_server
	#start_history_mr
}

stop_all(){
	stop_hdfs
        stop_spark
        stop_crail
	stop_yarn
	stop_timeline_server
	stop_history_mr
}

export -f start_hdfs
export -f start_spark
export -f start_crail
export -f start_yarn
export -f start_all
export -f stop_hdfs
export -f stop_spark
export -f stop_crail
export -f stop_yarn
export -f stop_all
export -f start_history_mr
export -f stop_history_mr
export -f start_timeline_server
export -f stop_timeline_server
