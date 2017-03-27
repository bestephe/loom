#!/bin/bash

RUN_START=200
RUN_END=235


# Not setting priorities
#for qtype in sq mq
#do
#    # Configure the network on all of the servers
#    sudo -u ubuntu -H ./config_all_netconf.sh $qtype.conf
#
#    # Run without spark competing
#    for i in $(seq $RUN_START $RUN_END)
#    do
#        echo $i
#        ./run_memcached_ycsb.py --expname $qtype.nospark.$i
#    done
#
#    # Run with spark competing
#    for i in $(seq $RUN_START $RUN_END)
#    do
#        time sudo -u ubuntu -H ./spark_terasort_h1.sh &> tmp_sort1.out &
#        ./run_memcached_ycsb.py --expname $qtype.spark.$i
#        wait
#    done
#done

#for qtype in sq mq mq-pri
for qtype in mq mq-pri
do
    # Configure the network on all of the servers
    sudo -u ubuntu -H ./config_all_netconf.sh $qtype.conf

    # Run without spark competing
    #for i in $(seq $RUN_START $RUN_END)
    #do
    #    echo $i
    #    ./run_memcached_ycsb.py --expname $qtype.nospark.$i --high-prio
    #done

    # Run with spark competing
    for i in $(seq $RUN_START $RUN_END)
    do
        time sudo -u ubuntu -H ./spark_terasort_h1.sh &> tmp_sort1.out &
        ./run_memcached_ycsb.py --expname $qtype.spark.$i --high-prio
        wait
    done
done
