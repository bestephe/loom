#!/usr/bin/python

import argparse
import glob
import os
import platform
import re
import subprocess
import sys
import yaml

from tc_test_common import *

ethtool_out = parse_ethtool_output()
print yaml.dump(ethtool_out)
