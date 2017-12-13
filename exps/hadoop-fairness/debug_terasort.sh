#!/bin/bash

SORT_JOBS=()
echo "Starting TeraSort #1"
time sudo -u ubuntu -H ./spark_run_scripts/spark_terasort_h1.sh &> tmp_sort1.out &
SORT_JOBS+=$!
echo "Starting TeraSort #2"
time sudo -u ubuntu2 -H ./spark_run_scripts/spark_terasort_h2.sh &> tmp_sort2.out &
SORT_JOBS+=" $!"
wait ${SORT_JOBS[@]}
echo "Finished TeraSorts"
