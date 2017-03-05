#!/bin/bash

echo "This script does not yet work as a script.  Exiting."
exit 1

# Create a user for hadoop
sudo adduser ubuntu

# Give the new user root access
sudo adduser ubuntu root

# Setup ssh keys
sudo -u ubuntu -i
for node in node-0 node-1
do
    ssh-copy-id $node
done

