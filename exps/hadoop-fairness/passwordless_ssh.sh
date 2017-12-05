#!/bin/bash

SSH_DIR=/proj/opennf-PG0/exp/loomtest/tmp/${USER}_ssh
if [ ! -d $SSH_DIR ] ; then
    ssh-keygen -P ""
    touch ~/.ssh/authorized_keys
    chmod 600 ~/.ssh/authorized_keys
    cat ~/.ssh/id_rsa.pub >> ~/.ssh/authorized_keys

    SERVERS="node-0 node-1 10.10.1.1 10.10.1.2 10.10.101.1 10.10.101.2 10.10.102.1 10.10.102.2 node-0.loomtest.opennf-pg0.clemson.cloudlab.us node-1.loomtest.opennf-pg0.clemson.cloudlab.us"
    for h in $SERVERS; do
        ssh-keyscan -H $h >> ~/.ssh/known_hosts
    done

    cp -r ~/.ssh $SSH_DIR
fi


rm -rf ~/ssh_bak
mv ~/.ssh ~/ssh_bak
cp -r $SSH_DIR ~/.ssh 

##
## This version doesn't quite work right.
##
#ssh-keygen
#for node in node-0 node-1 node-2
#do
#    ssh-copy-id $node
#    scp -r ~/.ssh $node:
#done
