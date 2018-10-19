#include <stdio.h>
#include <stdlib.h>

#include "orp.h"

static const char * const g_help =
  "Usage: oc [options] file\n"
  "Options:\n"
  "  -s  Print assembly.\n"
  "  -h  Show this message.\n";

int
main(int argc, char *argv[])
{
  int     sc = 0;       /* Return status */
  int     ch;           /* Input character */
  int     sflag = 0;    /* Print assembly */

  /* Parse command line arguments (cf. section 5.10 of K&R) */
  while (--argc > 0 && **++argv == '-') {
    while ((ch = *++*argv)) {
      switch (ch) {
      case 's':
        sflag = 1;
        break;
      case 'h':
        argc = 0;
        break;
      default:
        fprintf(stderr, "Illegal option: %c\n", ch);
        sc = 1;
        argc = 0;
        break;
      }
    }
  }

  if (argc != 1) {
    puts(g_help);
  } else {
    ORP_Compile(*argv, sflag);
  }

  return sc;
}
