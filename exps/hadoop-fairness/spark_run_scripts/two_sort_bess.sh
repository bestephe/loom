#!/bin/bash

RUNS=1

for i in {1..1}
do
    sudo -u ubuntu -H ./spark_run_scripts/spark_all_bess_netconf.sh
    # Note: tcpdump has already been started as part of configuring BESS (fairnes.bess)
    #  However, in order to get this to work, bessctl is run in the background
    #  an may not be finished running yet.
    # For now, just sleep and hope BESS gets configured correctly.
    sleep 20
    ping 10.10.102.1 -c 1
    if [ $? -ne 0 ]
    then
        echo "BESS failed to configure correctly!"
        exit 1
    fi

    SORT_JOBS=()
    echo "Starting TeraSort #1"
    time sudo -u ubuntu -H ./spark_run_scripts/spark_terasort_h1.sh &> tmp_sort1.out &
    SORT_JOBS+=$!
    echo "Starting TeraSort #2"
    time sudo -u ubuntu2 -H ./spark_run_scripts/spark_terasort_h2.sh &> tmp_sort2.out &
    SORT_JOBS+=" $!"
    wait ${SORT_JOBS[@]}
    echo "Finished TeraSorts"

    cat tmp_sort1.out > results/two_sort_bess.$i.out
    echo "" >> results/two_sort_bess.$i.out;
    echo "" >> results/two_sort_bess.$i.out;
    cat tmp_sort2.out >> results/two_sort_bess.$i.out

    sudo killall tcpdump
    ./pcap_flows/get_job_tput_ts.py --pcap /dev/shm/spark_tcp_flows.pcap --outf results/tputs_two_sort_bess.$i.yaml

    rm tmp_sort1.out
    rm tmp_sort2.out
    #rm -f spark_tcp_flows.pcap
    #sudo rm -f /dev/shm/pout.pcap
done
