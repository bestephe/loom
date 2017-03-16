#!/bin/bash

sudo tcpdump -i eno2 -w spark_tcp_flows.pcap -s 64
