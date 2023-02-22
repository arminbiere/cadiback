# CaDiCaL BackBone Analyzer CadiBack

This is a tool using the [CaDiCaL](https://github.com/arminbiere/cadical)
solver to determine the backbone of a satisfiable instance, which are those
literals set to true in all models of the formula.

You need to get and compile CaDiCaL in the same directory in which you have
the [CadiBack](https://github.com/arminbiere/cadiback) sources, i.e.,
before configuring and compiling it make sure to have

- `../cadical/build/libcadical.a`
- `../cadical/src/cadical.hpp`

Then issue `./configure && make` to configure and compile it.  There
are additional configuration options which are listed with `./configure -h`.

Usage of the tool is as follows.  Given the following CNF in `dimacs.cnf`
calling `cadiback` on it it will give:

```
$ cat dimacs.cnf
p cnf 3 4
1 2 0
1 -2 0
2 -3 0
-2 -3 0
$ cadiback -q dimacs.cnf
s SATISFIABLE
b 1 3 0
```

which shows that literals `1` and `-3` are backbones and that there
models both with `2` and `-2`.
