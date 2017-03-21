#!/bin/bash

for user in ubuntu ubuntu2
do
    # Create a user for hadoop
    sudo adduser $user -gecos "First Last,RoomNumber,WorkPhone,HomePhone" --disabled-password
    echo "$user:loomtest" | sudo chpasswd

    # Give the new user root access
    sudo adduser $user root
    sudo adduser $user opennf-PG0

    # Setup ssh keys
    #XXX: This requires the user to be setup on all machines first, so this
    # won't work.
    #sudo -H -u $user passwordless_ssh.sh
done
