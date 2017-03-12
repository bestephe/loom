#!/bin/bash

cd
source run.sh

# Args to teragen: <number of 100-byte rows> <output dir>
#XXX: -D doesn't work?
#hadoop -D mapreduce.job.maps 50 jar hadoop-*examples*.jar <...>
time hadoop jar software/hadoop-2.6.0/share/hadoop/mapreduce/hadoop-mapreduce-examples-2.6.0.jar terasort tera_unsorted tera_sorted
