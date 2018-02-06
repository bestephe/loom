#!/bin/bash

sudo ./tc_test_network_config.py --config bess-tc.conf
#sleep 0.5
./tc_test_start_progs.py --dir src --configf configs/8iperf-mtc-long.yaml

echo "Bessd PIDs:"
ps aux | grep bessd

sudo ../../code/bess/bessctl/bessctl -- daemon stop
