# Simple Fuzzing frame-work for the different options

You need the `cnfuzz` and `cnfdd` infra-structure installed.
Then you can run `make` to execute `run.sh` and start
fuzzing of all configuration option pairs related to optimizations.
With `make clean` you can then kill the fuzzing and remove all directories
Thus you might want to first save the bugs and reduced CNFs before.
More thorough testing with checking the backbones can be achieved
with `./run.sh -c`, i.e., all additional options are passed to the
extractor and in this case `-c` forced backbone checking.
