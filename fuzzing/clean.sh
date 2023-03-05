#!/bin/sh

. ./options.sh

killall -9 runcnfuzz
killall -9 cadical

for o1 in $options
do
  for o2 in $options
  do
    dir=${o1}-${o2}
    rm -rf $dir
  done
done
