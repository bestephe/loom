#!/bin/bash

TIMEOUT=50
THREADS=2

GIT_DIR=/proj/opennf-PG0/exp/loomtest2/datastore/bes/git/loom-code/exps/multi-priority
NODES="node-0.loomtest2.opennf-pg0.clemson.cloudlab.us,node-1.loomtest2.opennf-pg0.clemson.cloudlab.us"

if [ $# -ne 1 ]
  then
    echo "Usage: $0 <net_config.yaml>"
    exit 1
fi
conf=$1

pdsh -R exec -f $THREADS -w $NODES ssh -o ConnectTimeout=$TIMEOUT %h "( cd $GIT_DIR; sudo ./pri_network_config.py --config $GIT_DIR/$conf;)"
