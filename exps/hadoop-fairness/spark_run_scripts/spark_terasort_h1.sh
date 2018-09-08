#!/bin/bash

cd
. run.sh -q 
time spark-submit --class com.github.ehiggs.spark.terasort.TeraSort --executor-memory 20G /home/ubuntu/software/spark-terasort/target/spark-terasort-1.0-jar-with-dependencies.jar hdfs://10.10.101.2:8020/terasort_in
