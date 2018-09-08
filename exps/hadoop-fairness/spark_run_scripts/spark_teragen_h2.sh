#!/bin/bash

cd
. run.sh -q
spark-submit --class com.github.ehiggs.spark.terasort.TeraGen --executor-memory 20G /home/ubuntu2/software/spark-terasort/target/spark-terasort-1.0-jar-with-dependencies.jar 10g hdfs://10.10.102.2:9020/terasort_in
