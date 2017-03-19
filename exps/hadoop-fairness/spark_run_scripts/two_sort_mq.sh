#!/bin/bash

RUNS=1

#for i in {6..20}
for i in {100..300}
do
    sudo -u ubuntu -H ./spark_run_scripts/spark_all_mq_netconf.sh
    sudo tcpdump -i eno2 -w spark_tcp_flows.pcap -s 64 src 10.10.1.2 &

    SORT_JOBS=()
    echo "Starting TeraSort #1"
    time sudo -u ubuntu -H ./spark_run_scripts/spark_terasort_h1.sh &> tmp_sort1.out &
    SORT_JOBS+=$!
    echo "Starting TeraSort #2"
    time sudo -u ubuntu2 -H ./spark_run_scripts/spark_terasort_h2.sh &> tmp_sort2.out &
    SORT_JOBS+=" $!"
    wait ${SORT_JOBS[@]}
    echo "Finished TeraSorts"

    sudo killall tcpdump
    ./pcap_flows/get_job_tput_ts.py --pcap spark_tcp_flows.pcap --outf results/tputs_two_sort_mq.$i.yaml

    cat tmp_sort1.out > results/two_sort_mq.$i.out
    echo "" >> results/two_sort_mq.$i.out;
    echo "" >> results/two_sort_mq.$i.out;
    cat tmp_sort2.out >> results/two_sort_mq.$i.out

    rm tmp_sort1.out
    rm tmp_sort2.out
    rm -f spark_tcp_flows.pcap
done
