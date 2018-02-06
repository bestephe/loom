#!/bin/bash

RUN_START=1
RUN_END=1


for i in $(seq $RUN_START $RUN_END)
do
    #for qtype in sq
    #for qtype in sq mq
    for qtype in mq
    do
        #for config in configs/tctest_large_sockperf_*
        #for config in configs/tctest_large_sockperf_32ten_16conn.yaml
        for config in configs/tctest_large_sockperf_128ten_256conn.yaml
        do
            # Configure the network on all of the servers
            sudo -u ubuntu -H ./config_all_large_netconf.sh $qtype.conf

            # Note: tcpdump has already been started as part of configuring BESS (fairnes.bess)
            #  However, in order to get this to work, bessctl is run in the background
            #  an may not be finished running yet.
            # For now, just sleep and hope BESS gets configured correctly.
            sleep 5
            ping 10.10.102.1 -c 1
            if [ $? -ne 0 ]
            then
                echo "ixgbe failed to configure correctly!"
                #exit 1
                continue
            fi

            time ./run_tc_test.py --configs $config --extra-name $qtype.$i --runs 1

            echo "After wait..."

            sudo killall tcpdump
        done
    done

    #TODO: better waiting for all jobs to finish
done
