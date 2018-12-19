# loom-code: The core code and experimental frameworks for Loom

Loom enables dynamic application controlled packet scheduling.  This is
the repository for of the code related to Loom.  This includes:
- The core code developed as part of the Loom prototype (`code`)
- Code to configure a cluster environment that can run Loom (`env`)
- Code to run the experiments in the paper "Loom: Flexible and Efficient
  NIC Packet Scheduling" (`exps`).
- Code to generate the figures used in the paper "Loom: Flexible and Efficient
  NIC Packet Scheduling" (`plots`).

The rest of this README describes these individual directories in more
detail.


### Core Loom Code

The core code developed for the Loom prototype is located in the
[code](./code) directory.  This includes multiple sub-modules from
different frameworks that make up Loom.  In more detail:

- The code for the BESS prototype of Loom is located in the
  [code/bess](./code/bess) directory.  This includes the driver and
  backend for Loom's new OS/NIC interface.  This also includes Loom's
  new scheduler that enables separate rate-limiting and scheduling
  traffic classes.

- The code for compiling Loom policy DAGs into C++ code usable in the
  Loom Bess prototype is located in
  [code/pifo-compiler](./code/pifo-compiler).

- The driver for the Loom SoftNIC prototype makes a small change to the
  Linux kernel (~10 lines of code).  These changes can be found in the
  Linux kernel code that is located in
  [code/linux-4.9](./code/linux-4.9).
    - As part of ongoing development, we are working on a version of
      Loom that does not require any kernel modifications.


### Environment Configuration Code

The Loom prototype was developed both on CloudLab and on a private
cluster of servers for a variety of different NICs (Intel and Mellanox).
The code needed to configure the environment is found in the
[env](./env) directory.  In more detail:

- [env/README.md](./env/README.md) is a README file that details all of
  the configuration steps needed.

- [env/packages.yml](./env/packages.yml) is an Ansible playbook that can
  be used to install and configure the packages that the Loom prototype
  depends on.

- [env/conf_misc/config_server_bess.sh](./env/conf_misc/config_server_bess.sh)
  is a script taht performs some per-boot sever configuration like
  allocating hugepages.


### Experiment Framework Code

This repository contains all of the necessary code to duplicate the
experiments in the NSDI '19 publication on Loom. The code for running
the experiments from the Loom paper can be found in the [exps](./exps)
directory.  These experiments all come with their own README files.  In
more detail:

- [exps/loom_exp_common.py](exps/loom_exp_common.py) contains common
  code for the experimental framework used to evaluate Loom.

- [exps/memcached-ycsb/](exps/memcached-ycsb/) contains the code needed
  to perform an experiment with setting rate-limits for memcached
  traffic (Figure 2).

- [exps/hadoop-fairness/](exps/hadoop-fairness/) contains the code
  needed to perform an experiment with configuring per-job fairness in
  Spark (Figure 3 and TODO).

- [exps/hierarchy/](exps/hierarchy/) contains the code to perform a
  combined memcached and Spark experiment with a hierarchical network
  scheduling policy (Figure 4 and Figure 12).

- [exps/multi-priority](exps/multi-priority) contains the code to
  perform a Spark versus memcached priority experiment.

- [exps/tc-test](exps/tc-test) contains the code needed to perform
  iperf3 and sockperf microbenchmarks (Figures 8, 9, 10, and 11).


### Plot Code

This repository also contains the raw data and scripts used to generate
the figures in the NSDI '19 paper on Loom.  These data files and scripts
can be found in the [plots](./plots) directory.


