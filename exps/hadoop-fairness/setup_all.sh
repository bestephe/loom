#!/bin/bash

sudo -u ubuntu -H ./hadoop_setup.sh 10.10.1.2
sudo -u ubuntu -H ./spark_cloudlab_setup.sh

sudo -u ubuntu2 -H ./hadoop2_setup.sh 10.10.1.2
sudo -u ubuntu2 -H ./spark_cloudlab_setup.sh
