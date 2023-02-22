// clang-format off

static const char * usage =

"usage: cadiback [ <option> ... ] [ <dimacs> ]\n"
"\n"
"where '<option>' is one of the following\n"
"\n"
"  -h       print this command line option summary\n"
"  -l       extensive logging for debugging\n"
"  -q       disable all messages\n"
"  -n       do not print backbone \n"
"\n"
"  -v       increase verbosity\n"
"           (SAT solver verbosity is increased with two '-v')\n"
"\n"
"and '<dimacs>' is a SAT instances for which the backbone literals are\n"
"determined and then printed (unless '-n' is specified).  If no input\n"
"file is given the formula is read from '<stdin>'.\n"

;

// clang-format on

#include <cassert>
#include <climits>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "cadical.hpp"
#include "resources.hpp"
#include "signal.hpp"

// Verbosity level: -1=quiet, 0=default, 1=verbose, INT_MAX=logging.

static int verbosity;

// Print backbones by default. Otherwise only produce statistics.

static int vars;      // The number of variables in the CNF.
static int *backbone; // The backbone candidates (if non-zero).

static size_t backbones;   // Number of backbones found.
static size_t sat_calls;   // Calls with result SAT to SAT solver.
static size_t unsat_calls; // Calls with result UNSAT to SAT solver.
static size_t calls;       // Calls to SAT solver.

static void die (const char *, ...) __attribute__ ((format (printf, 1, 2)));
static void msg (const char *, ...) __attribute__ ((format (printf, 1, 2)));

static void msg (const char *fmt, ...) {
  if (verbosity < 0)
    return;
  fputs ("c ", stdout);
  va_list ap;
  va_start (ap, fmt);
  vprintf (fmt, ap);
  va_end (ap);
  fputc ('\n', stdout);
  fflush (stdout);
}

static void line () {
  if (verbosity < 0)
    return;
  fputs ("c\n", stdout);
  fflush (stdout);
}

static void die (const char *fmt, ...) {
  fputs ("cadiback: error: ", stderr);
  va_list ap;
  va_start (ap, fmt);
  vfprintf (stderr, fmt, ap);
  va_end (ap);
  fputc ('\n', stderr);
  exit (1);
}

static void dbg (const char *fmt, ...) {
  if (verbosity < INT_MAX)
    return;
  fputs ("c LOGGING ", stdout);
  va_list ap;
  va_start (ap, fmt);
  vprintf (fmt, ap);
  va_end (ap);
  fputc ('\n', stdout);
  fflush (stdout);
}

static CaDiCaL::Solver *solver;

static void statistics () {
  if (verbosity < 0)
    return;
  printf ("c\n");
  printf ("c --- [ backbone statistics ] ");
  printf ("------------------------------------------------\n");
  printf ("c\n");
  printf ("c called SAT solver %zu times (%zu SAT, %zu UNSAT)\n", calls,
          sat_calls, unsat_calls);
  printf ("c found %zu backbones\n", backbones);
  printf ("c\n");
  fflush (stdout);
  if (!solver)
    return;
  if (verbosity > 0)
    solver->statistics ();
  solver->resources ();
}

class CadiBackSignalHandler : public CaDiCaL::Handler {
  virtual void catch_signal (int sig) {
    if (verbosity < 0)
      return;
    printf ("c caught signal %d\n", sig);
    statistics ();
  }
};

static int solve () {
  assert (solver);
  calls++;
  int res = solver->solve ();
  if (res == 10) {
    sat_calls++;
  } else {
    assert (res == 20);
    unsat_calls++;
  }
  return res;
}

int main (int argc, char **argv) {
  bool print = true;
  const char *path = 0;
  for (int i = 1; i != argc; i++) {
    const char *arg = argv[i];
    if (!strcmp (arg, "-h")) {
      fputs (usage, stdout);
      exit (0);
    } else if (!strcmp (arg, "-l")) {
      verbosity = INT_MAX;
    } else if (!strcmp (arg, "-n")) {
      print = false;
    } else if (!strcmp (arg, "-q")) {
      verbosity = -1;
    } else if (!strcmp (arg, "-v")) {
      if (verbosity < 0)
        verbosity = 1;
      else if (verbosity < INT_MAX)
        verbosity++;
    } else if (*arg == '-')
      die ("invalid option '%s' (try '-h')", arg);
    else if (path)
      die ("multiple file arguments '%s' and '%s'", path, arg);
    else
      path = arg;
  }
  msg ("CaDiBack BackBone Analyzer");
  msg ("Copyright (c) 2023 Armin Biere University of Freiburg");
  msg ("Version 0.1.0 CaDiCaL %s", CaDiCaL::Solver::version ());
  line ();
  solver = new CaDiCaL::Solver ();
  if (verbosity < 0)
    solver->set ("quiet", 1);
  else if (verbosity > 0) { 
    solver->set ("verbose", verbosity - 1);
    solver->set ("report", 1);
  }
  int res;
  {
    CadiBackSignalHandler handler;
    CaDiCaL::Signal::set (&handler);
    dbg ("initialized solver");
    {
      const char *err;
      if (path) {
        msg ("reading from '%s'", path);
        err = solver->read_dimacs (path, vars);
      } else {
        msg ("reading from '<stdin>");
        err = solver->read_dimacs (stdin, "<stdin>", vars);
      }
      if (err)
        die ("%s", err);
      if (vars == INT_MAX)
        die ("can not support 'INT_MAX == %d' variables", vars);
    }
    msg ("found %d variables", vars);
    line ();
    dbg ("starting solving");
    res = solve ();
    assert (res == 10 || res == 20);
    if (res == 10) {
      msg ("solver determined first model after %.2f second",
           CaDiCaL::absolute_process_time ());
      printf ("s SATISFIABLE\n");
      fflush (stdout);
      backbone = new int[vars + 1];
      if (!backbone)
        die ("out-of-memory allocating backbone array");
      for (int idx = 1; idx <= vars; idx++)
        backbone[idx] = solver->val (idx);
      for (int idx = 1; idx <= vars; idx++) {
        int lit = backbone[idx];
        if (!backbone[idx]) {
          dbg ("skipping dropped non-backbone variable %d", idx);
          continue;
        }
        dbg ("assuming negation %d of backbone candidate %d", -lit, lit);
        solver->assume (-lit);
        int tmp = solve ();
        if (tmp == 10) {
          dbg ("found model satisfying "
               "negation %d of backbone candidate %d thus dropping %d",
               -lit, lit, lit);
          backbone[idx] = 0;
          for (int other = idx + 1; other <= vars; other++) {
            int candidate = backbone[other];
            if (candidate && candidate != solver->val (other)) {
              dbg ("model also satisfies negation %d "
                   "of other backbone candidate %d thus dropping %d too",
                   -candidate, candidate, candidate);
              backbone[other] = 0;
            }
          }
        } else {
          assert (tmp == 20);
	  dbg ("no model with %d thus found backbone literal %d", -lit, lit);
          if (print) {
            printf ("b %d\n", lit);
            fflush (stdout);
          }
          backbones++;
        }
      }
      if (print) {
        printf ("b 0\n");
        fflush (stdout);
      }
      delete[] backbone;
    } else {
      assert (res == 20);
      printf ("s UNSATISFIABLE\n");
    }
    statistics ();
    dbg ("deleting solver");
    CaDiCaL::Signal::reset ();
  }
  delete solver;
  line ();
  msg ("exit %d", res);
  return res;
}
