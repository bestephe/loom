#!/usr/bin/python

import argparse
#import dpkt
import itertools
import os
import re
import socket
import sys
import yaml

import dpkt

#XXX: Build our own reader because dpkt is not designed well
# This reader also returns the actual packet length and not just the capture
# length.
class PcapReader(dpkt.pcap.Reader):
    def __iter__(self):
        self._Reader__f.seek(dpkt.pcap.FileHdr.__hdr_len__)
        first_sec = -1
        while 1:
            buf = self._Reader__f.read(dpkt.pcap.PktHdr.__hdr_len__)
            if not buf: break
            hdr = self._Reader__ph(buf)
            buf = self._Reader__f.read(hdr.caplen)
            if first_sec == -1:
                first_sec = hdr.tv_sec
            yield ((hdr.tv_sec - first_sec) + (hdr.tv_usec / 1000000.0), hdr.len, buf)

def get_flowid(pkt):
    eth = dpkt.ethernet.Ethernet(pkt) 
    if eth.type != dpkt.ethernet.ETH_TYPE_IP:
       return None
    ip = eth.data
    if ip.p != dpkt.ip.IP_PROTO_TCP and ip.p != dpkt.ip.IP_PROTO_UDP: 
        return None
    tcp = ip.data

    flowid = (socket.inet_ntoa(ip.src), tcp.sport,
              socket.inet_ntoa(ip.dst), tcp.dport)
    #flowid = {'sip': socket.inet_ntoa(ip.src), 'sport': tcp.sport,
    #          'dip': socket.inet_ntoa(ip.dst), 'dport': tcp.dport}

    return flowid

def parse_trace_flows(fname):
    flows = {}
    with open(fname) as tf:
        pkt_reader = PcapReader(tf)
        pkts = pkt_reader.readpkts()
        for pkti, (ts, plen, pkt) in enumerate(pkts):
            flowid = get_flowid(pkt)
            if flowid != None:
                if flowid not in flows:
                    flows[flowid] = 0
                flows[flowid] += plen
    return flows

def main():
    # Parse arguments
    parser = argparse.ArgumentParser(description='Get all of the flows that '
        'are present in the PCAP trace.')
    # TODO: I could just take in a list of files instead
    parser.add_argument('files', help='The files to parse.',
        nargs='+')
    args = parser.parse_args()

    # Plot the files
    for fname in args.files:
        flows = parse_trace_flows(fname)
        print yaml.dump(flows)

if __name__ == "__main__":
    main()
