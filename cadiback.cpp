#include "cadical.hpp"

static CaDiCaL::Solver * solver;

int main (int argc, char ** argv) {
  solver = new CaDiCaL::Solver ();
  delete solver;
  return 0;
}
