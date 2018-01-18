#!/bin/bash

./results_scripts/gen_latency_cdf.py --sq results/memcached_latency.bess-sq.2spark.* --mq results/memcached_latency.bess-mq.2spark.* --loom results/memcached_latency.bess.2spark.* --outf memcached_latency_cdf.yaml
./results_scripts/plot_latency_cdf.py --results memcached_latency_cdf.yaml --figname hier_latency_cdf.pdf

./results_scripts/gen_fairness_cdf.py --mq results/tputs.bess-mq.* --loom results/tputs.bess.* --outf tenant_fairness_cdf.yaml
./results_scripts/plot_tenant_fairness_cdf.py --results tenant_fairness_cdf.yaml --figname hier_fairness_cdf.pdf

./results_scripts/plot_tenant_fairness_cdf.py --results tenant_fairness_cdf.yaml --figname tenant_fairness_cdf.pdf
./results_scripts/plot_tenant_fairness_cdf.py --results tenant_fairness_cdf.yaml --figname tenant_fairness_cdf.png

./results_scripts/plot_latency_cdf.py --results memcached_latency_cdf.yaml --figname memcached_latency_cdf.pdf
./results_scripts/plot_latency_cdf.py --results memcached_latency_cdf.yaml --figname memcached_latency_cdf.png
