#!/bin/sh

die () {
  echo "test/run.sh: error: $*" 1>&2
  exit 1
}

cd `dirname $0` || exit 1

[ -f ../cadiback ] || die "could not find '../cadiback'"

runs=0

run () {
  expected=$1
  shift
  name=$1
  cnf=$1.cnf
  shift
  log=$name.log
  err=$name.err
  ../cadiback $cnf $* >$log 2>$err
  status=$?
  if [ $status = $expected ]
  then
    echo "./cadiback test/$cnf $* # succeeded with expected exit code '$status'"
  else
    echo "./cadiback test/$cnf $* # failed with exit code '$status' (expected '$expected)"
    exit 1
  fi
  runs=`expr $runs + 1`
}

run 10 empty
run 10 empty -l
run 10 empty -q
run 10 empty -v

echo "passed $runs test runs"
