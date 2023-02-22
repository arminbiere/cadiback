# CaDiBack BackBone Analyzer

This is a tool using the [CaDiCaL](https://github.com/arminbiere/cadical)
solver to determine the backbone of a satisfiable instance, which are those
literals set to true in all models of the formula.

You need to get and compile CaDiCaL in the same directory in which you have
the [CaDiBack](https://github.com/arminbiere/cadiback) sources, i.e.,
before configuring and compiling it make sure to have

- `../cadical/build/libcadical.a`
- `../cadical/src/cadical.hpp`

Then issue `./configure && make` to configure and compile it.  To also test
the library you also use `make test` (additionally or alternatively).  There
are further configuration options which can be listed with `./configure -h`.

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
b 1
b 3
b 0
```

This example shows that exactly the two literals `1` and `-3` are backbones
and that there models both with `2` and `-2`, i.e., the variable `2` is not
a backbone variable.  In general the tool can produce multiple `b` lines which
in principle can be interleaved with comment lines starting with `c`.  After
the analyzer found all backbones it prints the `b 0` line.  The backbones
are printed as they are found and the output is flushed whenever a new
backbone is found.  This allows to use the tool in an any time fashion.
Without the `-q` option more comment lines are printed before and after
the backbone section.  If the given CNF is unsatisfiable the analyzer
prints 's UNSATISFIABLE' (and backbone section of course).
