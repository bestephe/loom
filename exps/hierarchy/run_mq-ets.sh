#!/bin/bash

RUN_START=200
RUN_END=250


# MQ-Pri requires restarting Spark for the ubuntu user.  For now, its a separate script
for qtype in mq-ets
do
    # Configure the network on all of the servers
    sudo -u ubuntu -H ./config_all_netconf.sh $qtype.conf

    # Run memcached with two spark instances competing
    for i in $(seq $RUN_START $RUN_END)
    do
        sudo tcpdump -i eno2 -w hier_tcp_flows.pcap -s 64 dst 10.10.1.2 &

        JOBS=()
        time sudo -u ubuntu -H ./spark_terasort_h1.sh &> tmp_sort1.out &
        JOBS+=$!
        time sudo -u ubuntu2 -H ./spark_terasort_h2.sh &> tmp_sort2.out &
        JOBS+=" $!"
        ./run_memcached_ycsb.py --expname $qtype.2spark.$i --high-prio
        wait ${JOBS[@]}

        sudo killall tcpdump
        ./results_scripts/get_tenant_tput_ts.py --pcap hier_tcp_flows.pcap --outf results/tputs.$qtype.$i.yaml

        rm tmp_sort1.out
        rm tmp_sort2.out
        rm -f hier_tcp_flows.pcap
    done
done
