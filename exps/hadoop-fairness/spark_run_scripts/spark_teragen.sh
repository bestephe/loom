#!/bin/bash


sudo -u ubuntu -H ./spark_teragen_h1.sh &
sudo -u ubuntu2 -H ./spark_teragen_h2.sh &
wait
