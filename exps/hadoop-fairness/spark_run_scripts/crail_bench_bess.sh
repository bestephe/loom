#!/bin/bash

IFACE=enp130s0

for i in {1..1}
do
    #for conf in bess
    for conf in debug
    do
        #TODO
        #sudo -u ubuntu -H ./spark_run_scripts/spark_all_bess_netconf.sh $conf.conf

        sudo tcpdump -i $IFACE -w /dev/shm/crail_bench_flows.pcap -s 64 src 10.10.1.1 or src 10.10.101.1 or src 10.10.102.1 &
        #sudo tcpdump -i $IFACE -w /dev/shm/crail_bench_flows_outgoing.pcap -s 64 src 10.10.1.2 or src 10.10.101.2 or src 10.10.102.2 &

        # Note: tcpdump has already been started as part of configuring BESS (fairnes.bess)
        #  However, in order to get this to work, bessctl is run in the background
        #  an may not be finished running yet.
        # For now, just sleep and hope BESS gets configured correctly.
        sleep 3
        ping 10.10.102.1 -c 1
        if [ $? -ne 0 ]
        then
            echo "BESS failed to configure correctly!"
            exit 1
        fi

        # Cleanup
        sudo -u ubuntu -H ./spark_run_scripts/crail_clean_h1.sh
        sudo -u ubuntu2 -H ./spark_run_scripts/crail_clean_h1.sh

        #CRAIL_JOBS=()
        echo "Starting CRAIL IOBENCH #1"
        #time sudo -u ubuntu -H ./spark_run_scripts/crail_iobench_h1.sh &> tmp_crail1.out &
        time sudo -u ubuntu -H ./spark_run_scripts/crail_iobench_h1.sh &
        CRAIL_JOBS+=" $!"
        #echo "Starting CRAIL IOBENCH #2"
        ##time sudo -u ubuntu2 -H ./spark_run_scripts/crail_iobench_h1.sh &> tmp_crail2.out &
        #time sudo -u ubuntu2 -H ./spark_run_scripts/crail_iobench_h1.sh &
        wait ${CRAIL_JOBS[@]}
        echo "Finished CRAIL Benchmarks"


        sudo killall tcpdump
        ./pcap_flows/get_crail_tput_ts.py --pcap /dev/shm/crail_bench_flows.pcap --outf results/tputs_crail_bench.$conf.$i.yaml
        #./pcap_flows/get_crail_tput_ts.py --pcap /dev/shm/crail_bench_flows_outgoing.pcap --outf results/tputs_crail_bench_outgoing.$conf.$i.yaml

        #rm tmp_sort1.out
        #rm tmp_sort2.out
        sudo rm -f /dev/shm/*.pcap
    done
done
