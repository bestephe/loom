## Environment setup guide for the RoGUE experiments (v2.0 and non-"cluster" exps)

### Step 0: Initial CloudLab/cluster configuration

1. Clone git to a shared directory.

E.g.:

```
cd /proj/opennf-PG0/exp/loomtest/datastore/bes/git/
git clone ssh://brentstephens@bs.cs.wisc.edu//p/akella-research/repos/loom-code.git/

```

2. Generate some keys for the cluster to get ssh to work

```
cd $LOOM_HOME/env
mkdir keys
ssh-keygen -f keys/id_rsa
```

On each server, copy the keys and setup env variables. (Use parallel ssh if
there are many servers)

```
./bootstrap_env.sh
```

### Step 1: Environment setup with ansible

1. Create the list of hostnames for use with ansible by creating and modifying
   a `cluster-hosts` file

Test with:
```
ansible all -i cluster-hosts -m ping
```

2. Run ansible to install packages

```
ansible-playbook -i cluster-hosts packages.yml
```

### DEBUG/NOTES:

OFED is difficult for 40G.  The following command:
'sudo ./mlnxofedinstall --dpdk --upstream-libs --force --add-kernel-support -s /proj/opennf-PG0/exp/loomtest40g/datastore/bes/git/loom-code/code/linux-4.9/'


### 40G Notes

echo eth | sudo tee /sys/bus/pci/devices/0000\:82\:00.0/mlx4_port1
