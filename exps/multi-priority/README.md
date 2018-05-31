## Guide for running the Spark vs. memcached priority experiment

### Configuration:

1. Setup spark 
- See ../hadoop-fairness/README.md

2. Setup memcached
- See ../memcached-ycsb/README.md and ../memcached-ycsb/setup\_notes.sh

- Try on both servers:
```
./start_memcached.py
```

### Running the experiment

1. Start memcached on node-1
```
./start_memcached.py --high-prio
```

2. Start the experiment
```
./run_bess_all.sh
```

### Parsing and plotting the results
```
./pri_gen_cdf.py --sq results/memcached_latency.bess-sq.spark.* --mq results/memcached_latency.bess-mq.spark.* --loom results/memcached_latency.bess.spark.* --outf memcached_latency.lines.yaml
./pri_plot_latency_cdf.py --results memcached_latency.lines.yaml --figname latency.spark.90p.pdf
```
