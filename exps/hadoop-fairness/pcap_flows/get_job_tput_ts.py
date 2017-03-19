#!/usr/bin/python

import argparse
#import dpkt
import itertools
import numpy
import os
import re
import socket
import sys
import yaml

import dpkt

TIME_SCALE = 0.1

J2_PORTS = [9020, 9077, 9080, 9091, 9092, 9093, 9094, 9095, 9096, 9097,
    9098, 9099, 9337, 51070, 51090, 51091, 51010, 51075, 51020, 51070,
    51475, 51470, 51100, 51105, 9485, 9480, 9481, 3049, 5242]

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

    flowid = {'sip': socket.inet_ntoa(ip.src), 'sport': tcp.sport,
              'dip': socket.inet_ntoa(ip.dst), 'dport': tcp.dport}

    return flowid

def parse_trace_job_tput(fname):
    cur_ts = 0.0
    xs = [cur_ts]
    j1_bytes = {cur_ts: 0}
    j2_bytes = {cur_ts: 0}
    with open(fname) as tf:
        pkt_reader = PcapReader(tf)
        pkts = pkt_reader.readpkts()
        for pkti, (ts, plen, pkt) in enumerate(pkts):
            while ts > (cur_ts + TIME_SCALE):
                cur_ts += TIME_SCALE
                xs.append(cur_ts)
                j1_bytes[cur_ts] = 0
                j2_bytes[cur_ts] = 0
            flowid = get_flowid(pkt)
            if flowid != None:
                if flowid['sport'] in J2_PORTS or flowid['dport'] in J2_PORTS:
                    j = j2_bytes
                else:
                    j = j1_bytes
                j[cur_ts] += plen

    j1_gbpss = [j1_bytes[x] * 8 / TIME_SCALE / 1e9 for x in xs]
    j2_gbpss = [j2_bytes[x] * 8 / TIME_SCALE / 1e9 for x in xs]
    tot_gbpps = [(j1_bytes[x] + j2_bytes[x]) * 8 / TIME_SCALE / 1e9 for x in xs]

    lines = [
        {'lname': 'Job1', 'xs': xs, 'ys': j1_gbpss},
        {'lname': 'Job2', 'xs': xs, 'ys': j2_gbpss},
        {'lname': 'Total', 'xs': xs, 'ys': tot_gbpps},
    ]
    results = {'lines': lines}

    return results

def main():
    # Parse arguments
    parser = argparse.ArgumentParser(description='Get all of the flows that '
        'are present in the PCAP trace.')
    # TODO: I could just take in a list of files instead
    parser.add_argument('--pcap', help='The files to parse.',
        required=True)
    parser.add_argument('--outf', help='The output file.')
    args = parser.parse_args()

    # Plot the files
    lines = parse_trace_job_tput(args.pcap)
    if args.outf:
        with open(args.outf, 'w') as f:
            yaml.dump(lines, f)
    else:
        print yaml.dump(lines)

if __name__ == "__main__":
    main()
