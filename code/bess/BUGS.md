## This file is a place I'm using to keep track of bugs and issues I've encountered in BESS

### bessd crashes from incorrect configuration

- Incorrectly configuring gates on IPLookup causes a bessd crash.  For example, this snippet:
```
ipfwd = IPLookup()
ipfwd.add(prefix='10.10.101.1', prefix_len=32, gate=1)
```
    - This is caused by lpm\_ not being initialized properly

- Incorrectly setting traffic classes caused bessd to crash.  For example,
  trying to attach multiple tasks to a rate limit:
```
bess.add_tc('bit_limit',
            policy='rate_limit',
            resource='bit',
            limit={'bit': int(2.2e9)})
v_inc::PortInc(port=v.name)
for i in range(queue_count):
    v_inc.attach_task(parent='bit_limit_rr', module_taskid=i)
```

### Packet loss:

- The kmod interfaces with the kernel incorrectly and unnecessarily drops packets

- The TX PMD should exert backpressure

### Software switching

For something name a "software switch", BESS is surprisingly lacking in the
ability to behave as a L2 or L2/L3 switch appropriately

- ARPs are a problem

- It seems like switching on VLANs is the best possible solution
    - Unfortunately, this solution does not easily play well with CloudLab and
      switches that expect non-tagged packets and switches that do not support
      VLAN-in-VLAN tagging.

- One simple solution is that L2Forward should have a broadcast gate
    - More generally, L2Forward should have support for multicast

- Creating a VPort or VHost should have an option for conifguring a MAC address

