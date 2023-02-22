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
  shift
  log=$name.log
  err=$name.err
  pretty="./cadiback"
  cmd="../cadiback"
  while [ $# -gt 0 ]
  do
    case $1 in
      -*) cmd="$cmd $1"; pretty="$pretty $1";;
      *) cmd="$cmd $1"; pretty="$pretty test/$1";;
    esac
    shift
  done
  $cmd 1>$log 2>$err
  status=$?
  if [ $status = $expected ]
  then
    echo "$pretty # '$name' succeeded with expected exit code '$status'"
  else
    echo "$pretty # '$name' failed with exit code '$status' (expected '$expected)"
    exit 1
  fi
  runs=`expr $runs + 1`
}

run 0 usage -h

run 10 empty1 empty.cnf
run 10 empty2 empty.cnf -l
run 10 empty3 empty.cnf -q
run 10 empty4 empty.cnf -v

run 10 example1 example.cnf
run 10 example2 example.cnf -q
run 10 example3 example.cnf -v

run 20 false0 false0.cnf
run 20 false1 false1.cnf

run 10 battleship battleship.cnf

echo "passed $runs test runs"
