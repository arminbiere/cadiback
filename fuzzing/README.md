# Simple Fuzzing frame-work for the different options

You need the 'cnfuzz' and 'cnfdd' infra-structure installed.
Then you can run 'make all' to execute 'run.sh' and start
fuzzing of all configuration option pairs related to optimizations.
With 'make clean' you can then kill the fuzzing and remove all directories
Thus you might want to first save the bugs and reduced CNFs before.
