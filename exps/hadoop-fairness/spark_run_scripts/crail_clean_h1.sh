#!/bin/bash

cd
. run.sh -q

for i in {1..1}
do
    $CRAIL_HOME/bin/crail fs -rm /tmp$i.dat
done
