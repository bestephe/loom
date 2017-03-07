#!/bin/bash

# Install docker
# Steps taken from the following guide: https://docs.docker.com/engine/installation/linux/ubuntu/
sudo apt-get install -y apt-transport-https ca-certificates curl software-properties-common
curl -fsSL https://download.docker.com/linux/ubuntu/gpg | sudo apt-key add -
sudo add-apt-repository "deb [arch=amd64] https://download.docker.com/linux/ubuntu $(lsb_release -cs) stable"
sudo apt-get update --fix-missing
sudo apt-get install -y docker-ce

# Configure the docker storage
sudo mkfs -t ext3 /dev/sda4
sudo mount /dev/sda4 /mnt
sudo chown -R ubuntu:ubuntu /mnt
sudo cp docker_conf /etc/default/docker
sudo rm -rf /var/lib/docker
sudo ln -s /mnt /var/lib/docker

# Restart docker
sudo service docker stop
sudo service docker start
