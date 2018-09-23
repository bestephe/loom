#!/bin/bash

cd
. run.sh -q

WRITE_JOBS=()
for i in {1..1}
do
    $CRAIL_HOME/bin/crail iobench -t write -s 16777216 -k 32 -f /tmp$i.dat -w 0 -b 64 &
    WRITE_JOBS+=" $!"
done
wait ${WRITE_JOBS[@]}


READ_JOBS=()
for i in {1..7}
do
    $CRAIL_HOME/bin/crail iobench -t readSequential -s 8388608 -k 2048 -f /tmp1.dat -w 32 -b 256 &
    READ_JOBS+=" $!"
done
wait ${READ_JOBS[@]}
