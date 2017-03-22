#!/bin/bash

TIMEOUT=50
THREADS=2

GIT_DIR=/proj/opennf-PG0/exp/Loom-Terasort/datastore/git/loom-code/exps/multi-priority

if [ $# -ne 1 ]
  then
    echo "Usage: $0 <net_config.yaml>"
    exit 1
fi

pdsh -R exec -f $THREADS -w ^$HOME/instances ssh -o ConnectTimeout=$TIMEOUT %h "( sudo $GIT_DIR/pri_network_config.py --config $GIT_DIR/$1;)"
