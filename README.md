# CaDiCaL BackBone Analyzer CadiBack

This is a tool using the (CaDiCaL)[https://github.com/arminbiere/cadical)
solver to determine the backbone of a satisfiable instance, which are those
literals set to true in all models of the formula.

You need to get and compile CaDiCaL in the same directory in which you have
the (CadiBack)[https://github.com/arminbiere/cadiback] sources, i.e.,
before configuring and compiling it make sure to have

- `../cadical/build/libcadical.a`
- `../cadical/src/cadical.hpp`

Then issue `./configure && make` to configure and compile it.  There
are additional configuration options which are listed with `./configure -h`.
