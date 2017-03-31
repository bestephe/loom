#!/bin/bash

#
# Time series figures of individual runs
#
./pcap_flows/plot_job_fairness_ts.py --results results/tputs_two_sort_sq.102.yaml --figname results/example_tputs_two_sort_sq.102.pdf
./pcap_flows/plot_job_fairness_ts.py --results results/tputs_two_sort_sq.102.yaml --figname results/example_tputs_two_sort_sq.102.png

./pcap_flows/plot_job_fairness_ts.py --results results/tputs_two_sort_mq.104.yaml --figname results/example_tputs_two_sort_mq.104.pdf
./pcap_flows/plot_job_fairness_ts.py --results results/tputs_two_sort_mq.104.yaml --figname results/example_tputs_two_sort_mq.104.png

./pcap_flows/plot_job_fairness_ts.py --results results/tputs_two_sort_mq.107.yaml --figname results/example_tputs_two_sort_mq.107.pdf
./pcap_flows/plot_job_fairness_ts.py --results results/tputs_two_sort_mq.107.yaml --figname results/example_tputs_two_sort_mq.107.png

#
# CDF of all of the runs
# 
./plot_fairness_cdf.py --sq fm_cdf_sq.yaml --mq fm_cdf_mq.yaml --figname spark_fairness.1jfr.pdf
./plot_fairness_cdf.py --sq fm_cdf_sq.yaml --mq fm_cdf_mq.yaml --figname spark_fairness.1jfr.png
