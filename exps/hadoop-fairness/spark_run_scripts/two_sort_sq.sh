#!/bin/bash

HOSTS='node-0 node-1'
GIT_DIR=$(pwd)
RUNS=1

#for i in {1..10}
for i in {11..30}
do
    sudo -u ubuntu -H ./spark_run_scripts/spark_all_sq_netconf.sh

    echo "Starting TeraSort #1"
    time sudo -u ubuntu -H ./spark_run_scripts/spark_terasort_h1.sh &> tmp_sort1.out &
    echo "Starting TeraSort #2"
    time sudo -u ubuntu2 -H ./spark_run_scripts/spark_terasort_h2.sh &> tmp_sort2.out &
    wait $(jobs -p)
    echo "Finished TeraSorts"

    cat tmp_sort1.out > results/two_sort_sq.$i.out
    echo "" >> results/two_sort_sq.$i.out;
    echo "" >> results/two_sort_sq.$i.out;
    cat tmp_sort2.out >> results/two_sort_sq.$i.out
    rm tmp_sort1.out
    rm tmp_sort2.out
done
