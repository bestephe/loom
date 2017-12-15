## Guide for running the Spark vs. memcached priority experiment

### Configuration:

1. Setup spark 
- See ../hadoop-fairness/README.md

2. Setup memcached
- See ../memcached-ycsb/README.md and ../memcached-ycsb/setup\_notes.sh

### Running the experiment

1. Start memcached on node-1
```
./start_memcached.py --high-prio
```

2. Start the experiment
```
./run_bess_all.sh
```
