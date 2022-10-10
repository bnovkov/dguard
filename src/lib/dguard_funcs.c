#include <stdlib.h>
#include <stdio.h>

__attribute__((noreturn)) void __dguard_abort(const char *fname) {
  fprintf(stderr, "dguard: %s: illegal isolated variable access detected\n",
          fname);
  abort();
}
