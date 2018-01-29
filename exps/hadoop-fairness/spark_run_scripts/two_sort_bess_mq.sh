#!/bin/bash

RUNS=1

#for i in {2..2}
for i in {100..125}
do
    sudo -u ubuntu -H ./spark_run_scripts/spark_all_bess_netconf.sh bess-mq.conf
    sleep 2
    sudo tcpdump -i loom1 -w /dev/shm/spark_tcp_flows.pcap -s 64 src 10.10.1.1 or src 10.10.101.1 or src 10.10.102.1 &
    #TODO: I could collect a trace from BESS internals as well

    # Note: tcpdump has already been started as part of configuring BESS (fairnes.bess)
    #  However, in order to get this to work, bessctl is run in the background
    #  an may not be finished running yet.
    # For now, just sleep and hope BESS gets configured correctly.
    ping 10.10.102.1 -c 1
    if [ $? -ne 0 ]
    then
        echo "BESS failed to configure correctly!"
        exit 1
    fi

    #DEBUG
    echo 1 | sudo tee /sys/kernel/debug/tracing/tracing_on

    SORT_JOBS=()
    echo "Starting TeraSort #1"
    time sudo -u ubuntu -H ./spark_run_scripts/spark_terasort_h1.sh &> tmp_sort1.out &
    SORT_JOBS+=$!
    echo "Starting TeraSort #2"
    time sudo -u ubuntu2 -H ./spark_run_scripts/spark_terasort_h2.sh &> tmp_sort2.out &
    SORT_JOBS+=" $!"
    wait ${SORT_JOBS[@]}
    echo "Finished TeraSorts"

    cat tmp_sort1.out > results/two_sort_bess_mq.$i.out
    echo "" >> results/two_sort_bess_mq.$i.out;
    echo "" >> results/two_sort_bess_mq.$i.out;
    cat tmp_sort2.out >> results/two_sort_bess_mq.$i.out

    sudo killall tcpdump
    ./pcap_flows/get_job_tput_ts.py --pcap /dev/shm/spark_tcp_flows.pcap --outf results/tputs_two_sort_bess_mq.$i.yaml

    rm tmp_sort1.out
    rm tmp_sort2.out
    sudo rm -f /dev/shm/spark_tcp_flows.pcap
done
