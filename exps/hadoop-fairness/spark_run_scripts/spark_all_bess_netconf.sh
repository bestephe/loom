#!/bin/bash

TIMEOUT=50
THREADS=2

GIT_DIR=/proj/opennf-PG0/exp/loomtest/datastore/bes/git/loom-code/exps/hadoop-fairness
NODES="node-0.loomtest.opennf-pg0.clemson.cloudlab.us,node-1.loomtest.opennf-pg0.clemson.cloudlab.us"

if [ "$#" -ne 1 ]; then
    echo "Illegal number of parameters"
fi
conf=$1

pdsh -R exec -f $THREADS -w $NODES ssh -o ConnectTimeout=$TIMEOUT %h "( cd $GIT_DIR; sudo .//spark_network_config.py --config $GIT_DIR/spark_run_scripts/$conf;)"
