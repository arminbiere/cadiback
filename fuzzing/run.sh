#!/bin/sh

die () {
  echo "run.sh: error: $*" 1>&2
  exit 1
}

[ -f ../cadiback ] || \
  die "no '../cadiback' found (compile it first)"

[ x"`runcnfuzz -h 2>/dev/null`" = x ] && \
  die "could not find 'runcnfuzz' (install 'runcnfuzz', 'cnfuzz' and 'cnfdd' first)"

. ./options.sh

for o1 in $options
do
  rest="`echo $options|sed -e s,^.*$o1,,`"
  [ "$rest" = "" ] && break
  for o2 in $rest
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
    [ x"$*" = x ] || args="$args $*"
    echo $dir$args
    mkdir $dir 2>/dev/null
    cd $dir
    runcnfuzz -i ../../cadiback$args 1>/dev/null 2>/dev/null &
    cd ..
  done
done