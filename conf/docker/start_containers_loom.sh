#!/bin/bash
for c in loom_test1 loom_test2
do
  docker rm -f $c # This should probably go away.
  docker run --privileged -i -d -t --net=none --name=$c loom/testimage /bin/bash
done

