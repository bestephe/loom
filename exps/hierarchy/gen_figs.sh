#!/bin/bash

./results_scripts/plot_tenant_fairness_cdf.py --results tenant_fairness_cdf.yaml --figname tenant_fairness_cdf.pdf
./results_scripts/plot_tenant_fairness_cdf.py --results tenant_fairness_cdf.yaml --figname tenant_fairness_cdf.png

./results_scripts/plot_latency_cdf.py --results memcached_latency_cdf.yaml --figname memcached_latency_cdf.pdf
./results_scripts/plot_latency_cdf.py --results memcached_latency_cdf.yaml --figname memcached_latency_cdf.png
