#!/bin/bash

ssh-keygen
for node in node-0 node-1 node-2
do
    ssh-copy-id $node
    scp -r ~/.ssh $node:
done
