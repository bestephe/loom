#!/bin/bash
exit 1

# Setup a SOCKs proxy for browsing doxygen
ssh -f -N -D 1080 -oProxyCommand="ssh -W %h:%p aslan" brentstephens@jarry

# Start a local webserver for doxygen
python2 -m SimpleHTTPServer 8081
