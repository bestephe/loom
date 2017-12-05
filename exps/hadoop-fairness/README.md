## Guide for running the Spark fairness experiments

### Configuration:

1. Setup users and install spark and hadoop

Run on each machine:
```
./bootstrap_cmds.sh
./setup_all.sh
```

2. Setup passwordless ssh.  On each machine for users ubuntu and ubuntu2

```
sudo -u ubuntu -H ./passwordless_ssh.sh
sudo -u ubuntu2 -H ./passwordless_ssh.sh
```

3. Format the HDFS Name node
On the master, as each user (ubuntu, ubuntu2), run:
```
hadoop namenode -format
```

### Running the experiment

1. Generate the HDFS data
```
./spark_teragen.sh
```

2. Run the correct varient of the experiment
