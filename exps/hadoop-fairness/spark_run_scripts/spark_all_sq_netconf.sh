#!/bin/bash

TIMEOUT=50
THREADS=2

GIT_DIR=/proj/opennf-PG0/exp/loomtest40g/datastore/bes/git/loom-code/exps/hadoop-fairness

pdsh -R exec -f $THREADS -w ^$HOME/instances ssh -o ConnectTimeout=$TIMEOUT %h "( cd $GIT_DIR; sudo ./spark_network_config.py --config $GIT_DIR/spark_run_scripts/sq.conf;)"
