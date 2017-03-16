#!/bin/bash

TIMEOUT=50
THREADS=2

GIT_DIR=/proj/opennf-PG0/exp/Loom-Terasort/datastore/git/loom-code/exps/hadoop-fairness

pdsh -R exec -f $THREADS -w ^$HOME/instances ssh -o ConnectTimeout=$TIMEOUT %h "( sudo $GIT_DIR/spark_network_config.py --config $GIT_DIR/spark_run_scripts/mq.conf;)"
