#!/bin/bash

HOSTS='node-0 node-1'
GIT_DIR=$(pwd)
RUNS=1

for i in {1..1}
do
    for h in $HOSTS
    do
        sudo $GIT_DIR/network_config.py --config $GIT_DIR/run_scripts/sq.conf
    done

    time sudo -u ubuntu -H ./terasort_run.sh &> tmp_sort1.out
    cat tmp_sort1.out > results/one_sort_sq.$i.out
    rm tmp_sort1.out
done
