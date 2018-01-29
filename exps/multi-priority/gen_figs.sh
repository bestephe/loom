#!/bin/bash

#
# Current figures
#
./pri_gen_cdf.py --sq results/memcached_latency.bess-sq.spark.* --mq results/memcached_latency.bess-mq.spark.* --loom results/memcached_latency.bess.spark.* --outf memcached_latency.lines.yaml
./pri_plot_latency_cdf.py --results memcached_latency.lines.yaml --figname latency.spark.90p.pdf

#
# Old invocations
#
# ./pri_gen_cdf.py --sq results/memcached_latency.sq.spark.*.yaml --mq results/memcached_latency.mq.spark.*.yaml --mq-pri results/memcached_latency.mq-pri.spark.*.yaml --outf cdf_latency.spark.90p.yaml
# ./pri_gen_cdf.py --sq results/memcached_latency.bess-sq.spark.*.yaml --mq results/memcached_latency.bess-mq.spark.*.yaml --loom results/memcached_latency.bess.spark.*.yaml --outf cdf_latency.spark.90p.yaml
./pri_plot_latency_cdf.py --results cdf_latency.spark.90p.yaml --figname latency.spark.90p.pdf
./pri_plot_latency_cdf.py --results cdf_latency.spark.90p.yaml --figname latency.spark.90p.png

./pri_plot_latency_cdf.py --results cdf_latency.qdisc.spark.yaml --figname latency.qdisc.spark.pdf
./pri_plot_latency_cdf.py --results cdf_latency.qdisc.spark.yaml --figname latency.qdisc.spark.png

#./pri_gen_latency_lines.py --sq results/memcached_latency.sq.spark.206.yaml --mq results/memcached_latency.mq.spark.206.yaml --mq-pri results/memcached_latency.mq-pri.spark.206.yaml --outf memcached_latency_ts.spark.yaml
./pri_plot_latency_ts.py --results memcached_latency_ts.spark.yaml --figname memcached_latency_ts.spark.pdf
./pri_plot_latency_ts.py --results memcached_latency_ts.spark.yaml --figname memcached_latency_ts.spark.png

./results_scripts/plot_latency_cdf.py --results memcached_latency_cdf.yaml --figname memcached_latency_cdf.pdf
./results_scripts/plot_latency_cdf.py --results memcached_latency_cdf.yaml --figname memcached_latency_cdf.png

