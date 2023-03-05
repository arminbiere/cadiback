#!/bin/sh

. ./options.sh

killall run.sh
killall runcnfuzz
killall cadiback
killall cnfdd

sleep 1

killall -9 run.sh
killall -9 runcnfuzz
killall -9 cadiback
killall -9 cnfdd

for o1 in $options
do
  for o2 in $options
  do
    dir=${o1}-${o2}
    rm -rf $dir
  done
done
