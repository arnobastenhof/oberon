#include <assert.h>
#include <stdio.h>

#include "minunit.h"

/* Convenience macro for running a single test case. */
#define RUN_TEST(test) RunTest(#test, test)

static int      g_run = 0;          /* number of executed tests */
static int      g_passed = 0;       /* number of passed tests */
static int      g_failed = 0;       /* number of failed tests */

static void     RunTest(const char * const, char * (*)(void));
static void     RunAllTests(void);
extern char *   TestScanner(void);
extern char *   TestScopes(void);
extern char *   TestParser(void);

int
main()
{
  RunAllTests();
  printf("Tests: %d. Passed: %d. Failed: %d.\n", g_run, g_passed, g_failed);
  return 0;
}

static void
RunTest(const char * const name, char * (*test)(void))
{
  char *  msg;

  assert(name && test);
  printf("Running %s\n", name);
  if ((msg = test())) {
    ++g_failed;
    printf("Test failure. %s\n", msg);
  } else {
    ++g_passed;
    printf("Passed: %s.\n", name);
  }
  ++g_run;
}

static void
RunAllTests(void)
{
  RUN_TEST(TestScanner);
  RUN_TEST(TestScopes);
  RUN_TEST(TestParser);
}
