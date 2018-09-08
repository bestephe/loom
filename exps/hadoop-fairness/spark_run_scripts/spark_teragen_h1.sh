#!/bin/bash

cd
. run.sh -q 
spark-submit --class com.github.ehiggs.spark.terasort.TeraGen --executor-memory 20G /home/ubuntu/software/spark-terasort/target/spark-terasort-1.0-jar-with-dependencies.jar 10g hdfs://10.10.101.2:8020/terasort_in
