// clang-format off

static const char * usage =

"usage: cadiback [ <option> ... ] [ <dimacs> ]\n"
"\n"
"where '<option>' is one of the following\n"
"\n"
"  -c | --check       check that backbones are really backbones\n"
"  -h | --help        print this command line option summary\n"
"  -l | --logging     extensive logging for debugging\n"
"  -n | --no-print    do not print backbone \n"
"  -q | --quiet       disable all messages\n"
"  -r | --report      report what the solver is doing\n"
"  -s | --statistics  always print full statistics (not only with '-v')\n"
"  -v | --verbose     increase verbosity (SAT solver needs three)\n"
"  -V | --version     print version and exit\n"
"\n"
"  --no-filter        do not filter additional candidates\n"
"  --no-fixed         do not use root-level fixed literal information\n"
#ifndef NFLIP
"  --no-flip          do not try to flip values of candidates in models\n"
#endif
"  --no-inprocessing  disable any preprocessing and inprocessing\n"
"  --one-by-one       try each candidate one-by-one (do not use 'constrain')\n"
"  --set-phase        force phases to satisfy negation of candidates\n"
"\n"
"  --plain            disable all optimizations, which is the same as:\n"
"\n"
"                       --no-filter --no-fixed"
#ifndef NFLIP
" --no-flip"
#endif
"\n"
"                       --no-inprocessing --one-by-one\n"
"\n"
"and '<dimacs>' is a SAT instances for which the backbone literals are\n"
"determined and then printed (unless '-n' is specified).  If no input\n"
"file is given the formula is read from '<stdin>'. All compressed file\n"
"types supported by 'CaDiCaL' are supported too.\n"

;

// clang-format on

#include <cassert>
#include <climits>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// Include the main 'CaDiCaL' API from 'cadical.hpp', but also some helper
// code from its library (from the 'CaDiCaL' source code directory').

#include "cadical.hpp"   // Main 'CaDiCaL' API.
#include "resources.hpp" // Get time and memory usage.
#include "signal.hpp"    // To set up a signal handler.
#include "version.hpp"   // Print 'CaDiCaL' version too.

// 'CadiBack' build information is generated by './generate'.

#include "config.hpp"

// Verbosity level: -1=quiet, 0=default, 1=verbose, INT_MAX=logging.

static int verbosity;

// Checker solver to check that backbones are really back-bones, enabled by
// '-c' or '--check' (and quite expensive but useful for debugging).
//
static const char *check;
static CaDiCaL::Solver *checker;

// Print backbones by default. Otherwise only produce statistics.
//
static bool print = true;

// Disable by default  printing those 'c <character> ...' lines
// in the solver.  If enabled is useful to see what is going on.
//
static bool report = false;

// From command line option '-s'.
//
static bool always_print_statistics;

// Do not filter candidates by obtained models.
//
static const char *no_filter;

#ifndef NFLIP

// There is an extension of CaDiCaL with the 'bool flip (lit)' API call
// which allows to flip values of literals in a given model.  This is
// cheaper than resetting the SAT solver and calling 'solve ()'.
//
static const char *no_flip;

#endif

// Disable filtering of backbones by obtained models.
//

// The solver can give back information about root-level fixed literals
// which can cheaply be used to remove candidates or determine backbones.
//
static const char *no_fixed;

// Disable preprocessing and inprocessing.
//
static const char *no_inprocessing;

// Force the SAT solver to assign decisions to a value which would make the
// remaining backbone candidate literals false.  This is a very natural idea
// but actual had negative effects and thus is now disabled by default.
//
static bool set_phase;

// Try each candidate after each other with a single assumption, i.e., do
// not use the 'constrain' optimization.
//
static const char *one_by_one;

static int vars;        // The number of variables in the CNF.
static int *fixed;      // The resulting fixed backbone literals.
static int *candidates; // The backbone candidates (if non-zero).
static int *constraint; // Literals to constrain.

// The actual incrementally used solver for backbone computation is a global
// variable such that it can be accessed by the signal handler to print
// statistics even if execution is interrupted or an error occurs.
//
static CaDiCaL::Solver *solver;

// Some statistics are collected here.

static struct {
  size_t backbones; // Number of backbones found.
  size_t dropped;   // Number of non-backbones found.
  size_t filtered;  // Number of candidates with two models.
  size_t checked;   // How often checked model or backbone.
  size_t fixed;     // Number of fixed variables.
  struct {
    size_t sat;     // Calls with result SAT to SAT solver.
    size_t unsat;   // Calls with result UNSAT to SAT solver.
    size_t unknown; // Interrupted solver calls.
    size_t total;   // Calls to SAT solver.
  } calls;
#ifndef NFLIP
  size_t flipped; // How often 'solver->flip (lit)' succeeded.
#endif
} statistics;

// Some time profiling information is collected here.

static double first_time, sat_time, unsat_time, solving_time, unknown_time;
static double satmax_time, unsatmax_time, flip_time, check_time;
static volatile double *started, start_time;

// Declaring these with '__attribute__ ...' gives nice warnings.

static void die (const char *, ...) __attribute__ ((format (printf, 1, 2)));
static void msg (const char *, ...) __attribute__ ((format (printf, 1, 2)));

static void fatal (const char *, ...)
    __attribute__ ((format (printf, 1, 2)));

// Actual message printing code starts here.

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
  fputs ("c CADIBACK ", stdout);
  va_list ap;
  va_start (ap, fmt);
  vprintf (fmt, ap);
  va_end (ap);
  fputc ('\n', stdout);
  fflush (stdout);
}

static void fatal (const char *fmt, ...) {
  fputs ("cadiback: fatal error: ", stderr);
  va_list ap;
  va_start (ap, fmt);
  vfprintf (stderr, fmt, ap);
  va_end (ap);
  fputc ('\n', stderr);
  fflush (stderr);
  abort ();
}

static double average (double a, double b) { return b ? a / b : 0; }
static double percent (double a, double b) { return average (100 * a, b); }

static double time () { return CaDiCaL::absolute_process_time (); }

static void start_timer (double *timer) {
  assert (!started);
  start_time = time ();
  started = timer;
}

static double stop_timer () {
  assert (started);
  double *timer = (double *) started;
  started = 0;
  double end = time ();
  double delta = end - start_time;
  *timer += delta;
  return delta;
}

static void print_statistics () {
  if (verbosity < 0)
    return;
  solver->prefix ("c ");
  double total_time = time ();
  volatile double *timer = started;
  if (started) {
    double delta = stop_timer ();
    if (timer == &solving_time) {
      statistics.calls.unknown++;
      unknown_time += delta;
    }
  }
  printf ("c\n");
  printf ("c --- [ backbone statistics ] ");
  printf ("------------------------------------------------\n");
  printf ("c\n");
  printf ("c found         %9zu backbones  %3.0f%%\n", statistics.backbones,
          percent (statistics.backbones, vars));
  printf ("c dropped       %9zu candidates %3.0f%%\n", statistics.dropped,
          percent (statistics.dropped, vars));
  printf ("c\n");
  printf ("c filtered      %9zu candidates %3.0f%%\n", statistics.filtered,
          percent (statistics.filtered, vars));
#ifndef NFLIP
  printf ("c flipped       %9zu candidates %3.0f%%\n", statistics.flipped,
          percent (statistics.flipped, vars));
#endif
  printf ("c fixed         %9zu candidates %3.0f%%\n", statistics.fixed,
          percent (statistics.fixed, vars));
  printf ("c\n");
  printf ("c called solver %9zu times      %3.0f%%\n",
          statistics.calls.total,
          percent (statistics.calls.total, vars + 1));
  printf ("c satisfiable   %9zu times      %3.0f%%\n", statistics.calls.sat,
          percent (statistics.calls.sat, statistics.calls.total));
  printf ("c unsatisfiable %9zu times      %3.0f%%\n",
          statistics.calls.unsat,
          percent (statistics.calls.unsat, statistics.calls.total));
  printf ("c\n");
  printf ("c --- [ backbone profiling ] ");
  printf ("-------------------------------------------------\n");
  printf ("c\n");
  if (always_print_statistics || verbosity > 0 || first_time)
    printf ("c   %10.2f %6.2f %% first\n", first_time,
            percent (first_time, total_time));
  if (verbosity > 0 || sat_time)
    printf ("c   %10.2f %6.2f %% sat\n", sat_time,
            percent (sat_time, total_time));
  if (verbosity > 0 || unsat_time)
    printf ("c   %10.2f %6.2f %% unsat\n", unsat_time,
            percent (unsat_time, total_time));
  if (verbosity > 0 || satmax_time)
    printf ("c   %10.2f %6.2f %% satmax\n", satmax_time,
            percent (satmax_time, total_time));
  if (verbosity > 0 || unsatmax_time)
    printf ("c   %10.2f %6.2f %% unsatmax\n", unsatmax_time,
            percent (unsatmax_time, total_time));
  if (verbosity > 0 || unknown_time)
    printf ("c   %10.2f %6.2f %% unknown\n", unknown_time,
            percent (unknown_time, total_time));
  if (verbosity > 0 || solving_time)
    printf ("c   %10.2f %6.2f %% solving\n", solving_time,
            percent (solving_time, total_time));
  if (verbosity > 0 || flip_time)
    printf ("c   %10.2f %6.2f %% flip\n", flip_time,
            percent (flip_time, total_time));
  if (verbosity > 0 || check_time)
    printf ("c   %10.2f %6.2f %% check\n", check_time,
            percent (check_time, total_time));
  printf ("c ====================================\n");
  printf ("c   %10.2f 100.00 %% total\n", total_time);
  printf ("c\n");
  printf ("c\n");
  fflush (stdout);
  if (!solver)
    return;
  if (always_print_statistics || verbosity > 0)
    solver->statistics ();
  solver->resources ();
}

class CadiBackSignalHandler : public CaDiCaL::Handler {
  virtual void catch_signal (int sig) {
    if (verbosity < 0)
      return;
    printf ("c caught signal %d\n", sig);
    print_statistics ();
  }
};

static int remaining_candidates () {
  size_t determined = statistics.dropped + statistics.backbones;
  assert (determined <= (size_t) vars);
  return vars - determined;
}

// Provide a wrapper function for calling the main solver.

static int solve () {
  assert (solver);
  start_timer (&solving_time);
  statistics.calls.total++;
  {
    char prefix[32];
    snprintf (prefix, sizeof prefix, "c #%zu ", statistics.calls.total);
    solver->prefix (prefix);
  }
  int remain = remaining_candidates ();
  if (report || verbosity > 1) {
    line ();
    msg ("---- [ "
         "SAT solver call #%zu (%d candidates remain %.0f%%)"
         " ] ----",
         statistics.calls.total, remain, percent (remain, vars));
    line ();
  } else if (verbosity > 0)
    msg ("SAT solver call %zu (%d candidates remain %0.f%%)",
         statistics.calls.total, remain, percent (remain, vars));
  int res = solver->solve ();
  if (res == 10) {
    statistics.calls.sat++;
  } else {
    assert (res == 20);
    statistics.calls.unsat++;
  }
  double delta = stop_timer ();
  if (statistics.calls.total == 1)
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
  return res;
}

// If 'check' is set (through '-c' or '--check') then we check all literals
// to either be a backbone literal or that they have a model.  The cost for
// doing this is expensive and needs one call to the checker SAT solver for
// each literal.  The checker solver is copied from the main incremental
// solver after parsing. The first model of the main solver is not checked.

static void inc_checked () {
  assert (checker);
  statistics.checked++;
  char prefix[32];
  snprintf (prefix, sizeof prefix, "c C%zu ", statistics.checked);
  checker->prefix (prefix);
}

static void check_model (int lit) {
  double *timer = (double *) started;
  if (timer)
    stop_timer ();
  start_timer (&check_time);
  inc_checked ();
  dbg ("checking that there is a model with %d", lit);
  checker->assume (lit);
  int tmp = checker->solve ();
  if (tmp != 10)
    fatal ("checking claimed model for %d failed", lit);
  stop_timer ();
  if (timer)
    start_timer (timer);
}

static void check_backbone (int lit) {
  start_timer (&check_time);
  inc_checked ();
  dbg ("checking that there is no model with %d", -lit);
  checker->assume (-lit);
  int tmp = checker->solve ();
  if (tmp != 20)
    fatal ("checking %d backbone failed", -lit);
  stop_timer ();
}

// The given variable was proven not be a backbone variable.

static void drop_candidate (int idx) {
  int lit = candidates[idx];
  dbg ("dropping candidate literal %d", lit);
  assert (lit);
  candidates[idx] = 0;
  assert (!fixed[idx]);
  assert (statistics.dropped < (size_t) vars);
  statistics.dropped++;
  if (set_phase)
    solver->unphase (idx);
  if (check)
    check_model (-lit);
}

#ifndef NFLIP

// This is the technique first implemented in 'Kitten' for SAT sweeping
// within 'Kissat', which tries to flip the value in a model of the formula
// without making the formula false.  It goes over the watches of the
// literal and checks if all watched clauses are double satisfied and also
// replaces watches if the second satisfying literal is not watched.

// This requires support by 'CaDiCaL' via the 'bool flip (int lit)'
// function, which is slightly more expensive than the one in 'Kitten' as in
// essence it is compatible with blocking literals (used in 'CaDiCaL' but
// not in 'Kitten').  The first attempt to 'flip' a literal will need to
// propagate all the assigned literals and find replacement watches while
// ignoring blocking literals.

// We to flip all remaining backbone candidate literals until none can be
// flipped anymore.  This optimization pays off if on average one literal
// can be flipped but still is pretty cheap if not.

// As only more recent versions of CaDiCaL (starting with '1.5.4-rc.2')
// support flipping we keep it under compile time control too (beside
// allowing to disable it during run-time).

static void try_to_flip_remaining (int start) {

  if (no_flip)
    return;

  start_timer (&flip_time);

  for (size_t round = 1, changed = 1; changed; round++, changed = 0) {
    for (int idx = start; idx <= vars; idx++) {
      int lit = candidates[idx];
      if (!lit)
        continue;
      if (!solver->flip (lit))
        continue;
      dbg ("flipped value of %d in round %d", lit, round);
      statistics.flipped++;
      drop_candidate (idx);
      changed++;
    }
  }

  stop_timer ();
}

#else

#define try_to_flip_remaining(START) \
  do { \
  } while (0)

#endif

// If the SAT solver has a model in which the candidate backbone literal for
// the given variable index is false, we drop it as a backbone candidate.

static bool filter_candidate (int idx) {
  assert (!no_filter);
  int lit = candidates[idx];
  if (!lit)
    return false;
  int val = solver->val (idx) < 0 ? -idx : idx; // Legacy support.
  assert (val == idx || val == -idx);
  if (lit == val)
    return false;
  assert (lit == -val);
  dbg ("model also satisfies negation %d "
       "of backbone candidate %d thus dropping %d",
       -lit, lit, lit);
  statistics.filtered++;
  drop_candidate (idx);
  return true;
}

// Try dropping as many variables as possible from 'start' to 'vars' based
// on the value of the remaining candidates in the current model.

static void filter_candidates (int start) {

  if (no_filter || start > vars)
    return;

  unsigned res = 0;
  for (int idx = start; idx <= vars; idx++)
    if (filter_candidate (idx), !res)
      res++;

  assert (res);
  (void) res;
}

// Drop the first candidate refuted by the current model and drop it.  In
// principle we could have merged this logic with 'filter_candidates' but we
// want to distinguish the one guaranteed dropped candidate if we find a
// model from the additional ones filtered by the model both with respect to
// statistics as well as supporting '--no-filter'.

static int drop_first_candidate (int start) {
  assert (start <= vars);
  int idx = start, lit = 0, val = 0;
  for (;; idx++) {
    assert (idx <= vars);
    lit = candidates[idx];
    if (!lit)
      continue;
    val = solver->val (idx) < 0 ? -idx : idx; // Legacy support.
    assert (val == idx || val == -idx);
    if (lit == -val)
      break;
  }
  assert (lit);
  assert (lit == -val);
  assert (idx <= vars);
  assert (candidates[idx] == lit);
  dbg ("model satisfies negation %d "
       "of backbone candidate %d thus dropping %d",
       -lit, lit, lit);
  drop_candidate (idx);
  return idx;
}

// Assume the given variable is a backbone variable with its candidate
// literal as backbone literal.  Optionally print, check and count it.

static bool backbone_variable (int idx) {
  int lit = candidates[idx];
  if (!lit)
    return false;
  fixed[idx] = lit;
  candidates[idx] = 0;
  if (print) {
    printf ("b %d\n", lit);
    fflush (stdout);
  }
  if (checker)
    check_backbone (lit);
  assert (statistics.backbones < (size_t) vars);
  statistics.backbones++;
  return true;
}

static bool fix_candidate (int idx) {

  assert (!no_fixed);
  int lit = candidates[idx];
  assert (lit);

  int tmp = solver->fixed (lit);
  if (!tmp)
    return false;

  if (tmp > 0) {
    dbg ("found fixed backbone %d", lit);
    backbone_variable (idx);
  }

  if (tmp < 0) {
    dbg ("removing fixed backbone %d candidate", lit);
    drop_candidate (idx);
  }

  statistics.fixed++;
  return true;
}

// Force all variables from 'start' to 'vars' to be backbones unless they
// were already dropped.  This is used for 'constrain'.

static void backbone_variables (int start) {
  size_t count = 0;
  for (int idx = start; idx <= vars; idx++)
    if (backbone_variable (idx))
      count++;
  assert (count);
  (void) count;
}

int main (int argc, char **argv) {

  const char *path = 0; // The path to the input file.

  for (int i = 1; i != argc; i++) {
    const char *arg = argv[i];
    if (!strcmp (arg, "-h")) {
      fputs (usage, stdout);
      exit (0);
    } else if (!strcmp (arg, "-V") || !strcmp (arg, "--version")) {
      fputs (VERSION, stdout);
      fputc ('\n', stdout);
      exit (0);
    } else if (!strcmp (arg, "-c") || !strcmp (arg, "--check")) {
      check = arg;
    } else if (!strcmp (arg, "-l") || !strcmp (arg, "--logging")) {
      verbosity = INT_MAX;
    } else if (!strcmp (arg, "-n") || !strcmp (arg, "--no-print")) {
      print = false;
    } else if (!strcmp (arg, "-q") || !strcmp (arg, "--quiet")) {
      verbosity = -1;
    } else if (!strcmp (arg, "-r") || !strcmp (arg, "--report")) {
      report = true;
    } else if (!strcmp (arg, "-s") || !strcmp (arg, "--statistics")) {
      always_print_statistics = true;
    } else if (!strcmp (arg, "-v") || !strcmp (arg, "--verbose")) {
      if (verbosity < 0)
        verbosity = 1;
      else if (verbosity < INT_MAX)
        verbosity++;
    } else if (!strcmp (arg, "--no-filter")) {
      no_filter = arg;
    } else if (!strcmp (arg, "--no-fixed")) {
      no_fixed = arg;
    } else if (!strcmp (arg, "--no-flip")) {
#ifndef NFLIP
      no_flip = arg;
#else
      die ("invalid option '%s' "
           "(CaDiCaL version does not support 'bool flip (int)')",
           arg);
#endif
    } else if (!strcmp (arg, "--no-inprocessing")) {
      no_inprocessing = arg;
    } else if (!strcmp (arg, "--one-by-one")) {
      one_by_one = arg;
    } else if (!strcmp (arg, "--set-phase")) {
      set_phase = true;
    } else if (!strcmp (arg, "--plain")) {
      no_filter = no_fixed = arg;
#ifndef NFLIP
      no_flip = arg;
#endif
      no_inprocessing = one_by_one = arg;
    } else if (*arg == '-')
      die ("invalid option '%s' (try '-h')", arg);
    else if (path)
      die ("multiple file arguments '%s' and '%s'", path, arg);
    else
      path = arg;
  }

  msg ("CadiBack BackBone Analyzer");
  msg ("Copyright (c) 2023 Armin Biere University of Freiburg");
  msg ("Version " VERSION " " GITID);
  msg ("CaDiCaL %s %s", CaDiCaL::version (), CaDiCaL::identifier ());
  msg ("Compiled with '%s'", BUILD);
  line ();

  if (check) {
    checker = new CaDiCaL::Solver ();
    msg ("checking models with copy of main solver by '%s'", check);
  } else
    msg ("not checking models and backbones "
         "(enable with '--check')");

  if (no_filter)
    msg ("filtering backbones by models disabled by '%s'", no_filter);
  else
    msg ("filtering backbones by models (disable with '--no-filter')");

  if (no_fixed)
    msg ("using root-level fixed literals disabled by '%s'", no_fixed);
  else
    msg ("using root-level fixed literals (disable with '--no-fixed')");

#ifndef NFLIP
  if (no_flip)
    msg ("trying to flip candidate literals disabled by '%s'", no_flip);
  else
    msg ("trying to flip candidate literals (disable with '--no-flip')");
#endif

  if (no_inprocessing)
    msg ("SAT solver inprocessing disabled by '%s'", no_inprocessing);
  else
    msg ("SAT solver inprocessing (disable with '--no-inprocessing')");

  if (one_by_one)
    msg ("backbone candidates checked one-by-one by '%s'", one_by_one);
  else
    msg ("backbone candidates checked all-at-once "
         "(disable with '--one-by-one')");

  if (set_phase)
    msg ("phases explicitly forced by '--set-phase'");
  else
    msg ("phases picked by SAT solver "
         "(force with '--set-phase')");
  line ();

  solver = new CaDiCaL::Solver ();
  if (no_inprocessing)
    solver->set ("inprocessing", 0);

  if (verbosity < 0)
    solver->set ("quiet", 1);
  else if (verbosity > 1)
    solver->set ("verbose", verbosity - 2);
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

      // Computing 'vars + 1' as well as the idiom 'idx <= vars' in 'for'
      // loops requires 'vars' to be less than 'INT_MAX' to avoid overflows.
      // For simplicity we force having less variables here.
      //
      if (vars == INT_MAX) {
        die ("can not support 'INT_MAX == %d' variables", vars);
      }
    }
    msg ("found %d variables", vars);

    // Determine first model or that formula is unsatisfiable.

    line ();
    msg ("starting solving after %.2f seconds", time ());
    res = solve ();
    assert (res == 10 || res == 20);

    if (checker) {
      dbg ("copying checker after first model");
      solver->copy (*checker);
    }

    if (res == 10) {

      msg ("solver determined first model after %.2f seconds", time ());
      if (report || verbosity > 1)
        line ();

      candidates = new int[vars + 1];
      if (!candidates)
        fatal ("out-of-memory allocating backbone candidate array");

      fixed = new int[vars + 1];
      if (!fixed)
        fatal ("out-of-memory allocating backbone result array");

      if (!one_by_one) {
        constraint = new int[vars];
        if (!constraint)
          fatal ("out-of-memory allocating constraint stack");
      }

      // Initialize the candidate backbone literals with first model.

      for (int idx = 1; idx <= vars; idx++) {
        int lit = solver->val (idx) < 0 ? -idx : idx; // Legacy support.
        assert (lit == idx || lit == -idx);
        candidates[idx] = lit;
        fixed[idx] = 0;

        // If enabled by '--set-phase' set opposite value as default
        // decision phase.  This seems to have  negative effects with and
        // without using 'constrain' and thus is disabled by default.

        if (set_phase)
          solver->phase (-lit);
      }

      // Use first model to flip as many literals as possible which if
      // successful is cheaper than calling the SAT solver.

      try_to_flip_remaining (1);

      // Now go over all variables in turn and check whether they still are
      // candidates for being a backbone variables.  Each step of this loop
      // either drops at least one candidate or determines at least one
      // candidate to be a backbone (or skips already dropped variables).

      int last = 10;

      for (int idx = 1; idx <= vars; idx++) {

        // First skip variables that have been dropped as candidates before.

        int lit = candidates[idx];
        if (!lit)
          continue;

        // With 'constrain' we might drop another literal but not 'idx' and
        // in that case simply restart checking 'idx' for being a candidate.

      TRY_SAME_CANDIDATE_AGAIN:

        assert (lit == candidates[idx]);
        assert (lit);

        // If not disabled by '--no-fixed' filter root-level fixed literals.

        if (!no_fixed && fix_candidate (idx))
          continue;

        // If not disabled through '--one-by-one' use the 'constrain'
        // optimization which assumes the disjunction of all remaining
        // possible backbone candidate literals using the 'constrain' API
        // call described in our FMCAD'21 paper.

        // If remaining backbone candidates are all actually backbones
        // then only this call is enough to prove it. Otherwise without
        // 'constrain' we need as many solver calls as there are
        // candidates. Without constrain this puts heavy load on the
        // 'restore' algorithm which in some instances ended up taking 99%
        // of the running time.

        if (!one_by_one && last == 20) {

          int assumed = 0;
          assert (assumed < vars);
          constraint[assumed++] = -lit;

          for (int other = idx + 1; other <= vars; other++) {
            int lit_other = candidates[other];
            if (!lit_other)
              continue;
            if (!no_fixed && fix_candidate (other))
              continue;
            assert (assumed < vars);
            constraint[assumed++] = -lit_other;
          }

          if (assumed > 1) { // At least one other candidate left.

            dbg ("assuming negation of all %d remaining backbone "
                 "candidates starting with variable %d",
                 assumed, idx);

            for (int i = 0; i != assumed; i++)
              solver->constrain (constraint[i]);
            solver->constrain (0);

            last = solve ();
            if (last == 10) {
              dbg ("constraining negation of all %d backbones candidates "
                   "starting with variable %d all-at-once produced model",
                   assumed, idx);
              int other = drop_first_candidate (idx);
              filter_candidates (other + 1);
              try_to_flip_remaining (idx);

              lit = candidates[idx];
              if (lit)
                goto TRY_SAME_CANDIDATE_AGAIN;

              continue; // ... with next candidate.
            }

            assert (last == 20);
            msg ("all %d remaining candidates starting at %d "
                 "shown to be backbones in one call",
                 assumed, lit);
            backbone_variables (idx); // Plural!  So all remaining.
            break;

          } else {

            dbg ("no other literal besides %d "
                 "remains a backbone candidate",
                 lit);

            // ... so fall through and continue with assumption below.
          }
        }

        dbg ("assuming negation %d of backbone candidate %d", -lit, lit);
        solver->assume (-lit);
        last = solve ();
        if (last == 10) {
          dbg ("found model satisfying single assumed "
               "negation %d of backbone candidate %d",
               -lit, lit);
          drop_candidate (idx);
          filter_candidates (idx + 1);
          assert (!candidates[idx]);
          try_to_flip_remaining (idx + 1);
        } else {
          assert (last == 20);
          dbg ("no model with %d thus found backbone literal %d", -lit,
               lit);
          backbone_variable (idx); // Singular! So only this one.
        }
      }

      // All backbones found! So terminate the backbone list with 'b 0'.

      if (print) {
        printf ("b 0\n");
        fflush (stdout);
      }

      // We only print 's SATISFIABLE' here which is supposed to indicate
      // that the run completed.  Otherwise printing it before printing
      // 'b' lines confuses scripts (and 'zummarize').

      line ();
      printf ("s SATISFIABLE\n");
      fflush (stdout);

#ifndef NDEBUG

      if (res == 10) {

        // At the end all variables are either backbones or filtered.

        {
          size_t count = 0;
          for (int idx = 1; idx <= vars; idx++)
            if (fixed[idx])
              count++;

          assert (count == statistics.backbones);
        }

        {
          size_t count = 0;
          for (int idx = 1; idx <= vars; idx++)
            if (!fixed[idx])
              count++;

          assert (count == statistics.dropped);
        }

        assert (statistics.backbones + statistics.dropped == (size_t) vars);
      }

#endif

      delete[] candidates;
      delete[] fixed;

      if (!one_by_one)
        delete[] constraint;

      if (checker) {
        if (statistics.checked < (size_t) vars)
          fatal ("checked %zu literals and not all %d variables",
                 statistics.checked, vars);
        else if (statistics.checked > (size_t) vars)
          fatal ("checked %zu literals thus more than all %d variables",
                 statistics.checked, vars);
        delete checker;
      }

    } else {
      assert (res == 20);
      printf ("s UNSATISFIABLE\n");
    }
    print_statistics ();
    dbg ("deleting solver");
    CaDiCaL::Signal::reset ();
  }

  delete solver;

  line ();
  msg ("exit %d", res);

  return res;
}
