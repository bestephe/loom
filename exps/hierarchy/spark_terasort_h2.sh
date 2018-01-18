#!/bin/bash

cd
. run.sh -q
time spark-submit --class com.github.ehiggs.spark.terasort.TeraSort --executor-memory 75G /home/ubuntu2/software/spark-terasort/target/spark-terasort-1.0-jar-with-dependencies.jar hdfs://10.10.102.2:9020/terasort_in
