#!/usr/bin/python

import socket
import platform
import subprocess
import shlex

IFACE = 'enp130s0'

# Helpers
def aton(ip):
    return socket.inet_aton(ip)
# CloudLab experiment platform specific code:
def node2id(node):
    node2id_dict = {
        'pinter': 1,
        'jarry': 2,
        'node-1': 1,
        'node-0': 2,
    }
    try:
        id_ = node2id_dict[node]
    except:
        node = node.split('.')[0]
        id_ = node2id_dict[node]
    return id_
node_name = platform.node()
node_id = node2id(node_name)

ips = ['10.10.1%02d.%d' % (i, node_id) for i in range(1, 21)]
for ip in ips:
    cmd_keys = {'ip': ip, 'iface': IFACE}
    cmd = 'sudo ip addr add {ip}/24 dev {iface}'.format(**cmd_keys)
    print cmd
    subprocess.call(shlex.split(cmd))
