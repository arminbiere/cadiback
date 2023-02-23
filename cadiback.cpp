// clang-format off

#define VERSION "0.1.2"

static const char * usage =

"usage: cadiback [ <option> ... ] [ <dimacs> ]\n"
"\n"
"where '<option>' is one of the following\n"
"\n"
"  -h          print this command line option summary\n"
"  -l          extensive logging for debugging\n"
"  -q          disable all messages\n"
"  -r          report what the solver is doing\n"
"  -n          do not print backbone \n"
"\n"
"  -v          increase verbosity\n"
"              (SAT solver verbosity is increased with two '-v')\n"
"\n"
"  --version   print version and exit\n"
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

static int vars;      // The number of variables in the CNF.
static int *backbone; // The backbone candidates (if non-zero).

static size_t backbones;     // Number of backbones found.
static size_t sat_calls;     // Calls with result SAT to SAT solver.
static size_t unsat_calls;   // Calls with result UNSAT to SAT solver.
static size_t unknown_calls; // Interrupted solver calls.
static size_t calls;         // Calls to SAT solver.

static double first_time, sat_time, unsat_time, solving_time, unknown_time;
static double satmax_time, unsatmax_time;
static volatile double started = -1;

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

static double average (double a, double b) { return b ? a / b : 0; }
static double percent (double a, double b) { return average (100 * a, b); }

static double time () { return CaDiCaL::absolute_process_time (); }

static void statistics () {
  if (verbosity < 0)
    return;
  if (started >= 0) {
    double end = time ();
    double delta = end - started;
    started = -1;
    unknown_time += delta;
    solving_time += delta;
    unknown_calls++;
  }
  printf ("c\n");
  printf ("c --- [ backbone statistics ] ");
  printf ("------------------------------------------------\n");
  printf ("c\n");
  printf ("c found %zu backbones\n", backbones);
  printf ("c called SAT solver %zu times (%zu SAT, %zu UNSAT)\n",
	  calls, sat_calls, unsat_calls);
  printf ("c\n");
  if (verbosity > 0 || first_time)
    printf ("c   %10.2f %6.2f %% first\n", first_time,
            percent (first_time, solving_time));
  if (verbosity > 0 || sat_time)
    printf ("c   %10.2f %6.2f %% sat\n", sat_time,
            percent (sat_time, solving_time));
  if (verbosity > 0 || satmax_time)
    printf ("c   %10.2f %6.2f %% satmax\n", satmax_time,
            percent (satmax_time, solving_time));
  if (verbosity > 0 || unsat_time)
    printf ("c   %10.2f %6.2f %% unsat\n", unsat_time,
            percent (unsat_time, solving_time));
  if (verbosity > 0 || unsatmax_time)
    printf ("c   %10.2f %6.2f %% unsatmax\n", unsatmax_time,
            percent (unsatmax_time, solving_time));
  if (verbosity > 0 || unknown_time)
    printf ("c   %10.2f %6.2f %% unknown\n", unknown_time,
            percent (unknown_time, solving_time));
  printf ("c ---------------------------------\n");
  printf ("c   %10.2f 100.00 %% solving\n", solving_time);
  printf ("c\n");
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
  started = time ();
  calls++;
  int res = solver->solve ();
  if (res == 10) {
    sat_calls++;
  } else {
    assert (res == 20);
    unsat_calls++;
  }
  double end = time ();
  double delta = end - started;
  started = -1;
  if (calls == 1)
    first_time = delta;
  if (res == 10) {
    sat_time += delta;
    if (delta > satmax_time)
      satmax_time = delta;
  } else {
    unsat_time += delta;
    if (delta > unsatmax_time)
      unsatmax_time = delta;
  }
  solving_time += delta;
  return res;
}

int main (int argc, char **argv) {

  // Print backbones by default. Otherwise only produce statistics.
  //
  bool print = true;

  // Disable by default  printing those 'c <character> ...' lines
  // in the solver.  If enabled is useful to see what is going on.
  //
  bool report = false;

  const char *path = 0; // The path to the input file.

  for (int i = 1; i != argc; i++) {
    const char *arg = argv[i];
    if (!strcmp (arg, "-h")) {
      fputs (usage, stdout);
      exit (0);
    } else if (!strcmp (arg, "--version")) {
      fputs (VERSION, stdout);
      fputc ('\n', stdout);
      exit (0);
    } else if (!strcmp (arg, "-l")) {
      verbosity = INT_MAX;
    } else if (!strcmp (arg, "-n")) {
      print = false;
    } else if (!strcmp (arg, "-q")) {
      verbosity = -1;
    } else if (!strcmp (arg, "-r")) {
      report = true;
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
  msg ("Version " VERSION " CaDiCaL %s", CaDiCaL::Solver::version ());
  line ();

  solver = new CaDiCaL::Solver ();

  if (verbosity < 0)
    solver->set ("quiet", 1);
  else if (verbosity > 0)
    solver->set ("verbose", verbosity - 1);
  if (report || verbosity > 1)
    solver->set ("report", 1);

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
      if (vars == INT_MAX) {
        die ("can not support 'INT_MAX == %d' variables", vars);
        // Otherwise 'vars + 1' as well as the idiom 'idx <= vars' does not
        // work and for simplicity we force having less variables here.
      }
    }
    msg ("found %d variables", vars);
    line ();
    msg ("starting solving after %.2f seconds", time ());
    res = solve ();
    assert (res == 10 || res == 20);
    if (res == 10) {
      msg ("solver determined first model after %.2f seconds", time ());
      line ();
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
          dbg ("no model with %d thus found backbone literal %d", -lit,
               lit);
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
      line ();
      printf ("s SATISFIABLE\n");
      fflush (stdout);
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
