#!/usr/bin/env bash
# This env varibale allows setting additional java parameter
CRAIL_EXTRA_JAVA_OPTIONS="-Xmx24G -Xmn16G"

#XXX: I can't figure out why this is needed if run.sh already exports it.
JAVA_HOME=/usr/lib/jvm/java-1.8.0-openjdk-amd64
JAVA=$JAVA_HOME/bin/java

CRAIL_VERSION=1.1
CRAIL_HOME=/home/ubuntu/software/crail-${CRAIL_VERSION}
