#!/bin/sh

. options.sh

for o1 in $options
do
  for o2 in $options
  do
    if [ $o1 = default -a $o2 = default ]
    then
      args=""
    elif [ $o1 = default ]
    then
      args=" --$o2"
    elif [ $o2 = default ]
    then
      args=" --$o1"
    else
      args=" --$o1 --$o2"
    fi
    dir=${o1}-${o2}
    echo $dir$args
  done
done

wait
