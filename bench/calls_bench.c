// 24.1 and 24.3: times a few Lox programs. bench/run.sh builds this three
// ways: the default (the ip in a local variable, natives checked), with
// FRAME_IP (the book's run(), which goes through frame->ip), and with
// UNCHECKED_NATIVES (the book's native calls, with no arity or error check).
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "../src/vm.h"

#define RUNS 7

static double now(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec + t.tv_nsec / 1e9;
}

static int compareDoubles(const void* a, const void* b) {
  double x = *(const double*)a, y = *(const double*)b;
  return (x > y) - (x < y);
}

// The median time to run source, in seconds.
static double timeProgram(const char* source) {
  double times[RUNS];
  for (int i = 0; i < RUNS; i++) {
    double start = now();
    if (interpret(source) != INTERPRET_OK) exit(1);
    times[i] = now() - start;
  }
  qsort(times, RUNS, sizeof(double), compareDoubles);
  return times[RUNS / 2];
}

int main(void) {
  initVM();

  // Lots of calls and returns, where the ip is saved and loaded.
  double fib = timeProgram(
      "fun fib(n) { if (n < 2) return n; return fib(n - 2) + fib(n - 1); }"
      "fib(27);");
  printf("  fib(27):               %7.1f ms\n", fib * 1000);

  // No calls at all: only instructions that read the ip.
  double loop = timeProgram(
      "{ var sum = 0; var i = 0;"
      "  while (i < 10000000) { sum = sum + i * 2 - 1; i = i + 1; } }");
  printf("  loop, no calls:        %7.1f ms\n", loop * 1000);

  // A loop with a call to a Lox function that does nothing.
  double lox = timeProgram(
      "fun nothing(x) {}"
      "{ for (var i = 0; i < 5000000; i = i + 1) nothing(i); }");
  printf("  5M Lox calls:          %7.1f ms\n", lox * 1000);

  // 24.3: the same loop calling a native. The empty loop is subtracted to
  // leave the cost of each call.
  double empty = timeProgram(
      "{ for (var i = 0; i < 5000000; i = i + 1) i; }");
  double native = timeProgram(
      "{ for (var i = 0; i < 5000000; i = i + 1) floor(i); }");
  printf("  5M native calls:       %7.1f ms (%.1f ns per call)\n",
         native * 1000, (native - empty) / 5000000 * 1e9);

  freeVM();
  return 0;
}
