#define _GNU_SOURCE

#include <strings.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <sys/types.h>
#include <dirent.h>

#include <errno.h>

#include "sc_array.h"

// extern ?type? metadata_arr*;

static struct sc_array_str addr_arr;

static int parse_mtdt_file(const char *fname, void *dlh) {

  size_t len = 0;
  ssize_t nread = 0;
  char *line = NULL;
  FILE *f = fopen(fname, "r");
  if (!f) {
    perror("fopen");
    return -1;
  }

  errno = 0;
  while ((nread = getline(&line, &len, f)) != -1) {

    char *c = index(line, '\n');
    if (c != NULL) {
      *c = '\0';
    }

    dlerror();
    void *tls_var_addr = dlsym(RTLD_DEFAULT, line);
    if (tls_var_addr == NULL) {
      char *err = dlerror();
      if (err == NULL) {
        printf("NULL TLS variable address encountered\n");
      } else {
        fprintf(stderr, "dlsym: %s\n", err);
      }
      continue;
    }

    printf("dlsym addr for %s\n: %p", line, tls_var_addr);
  }

  if (errno) {
    perror("getline");
    fclose(f);
    return -1;
  }

  fclose(f);
  return 0;
}

int __tls_isol_metadata_init(void) {

  DIR *curdir;
  void *dlh;

  curdir = opendir(".");
  if (!curdir) {
    perror("opendir");
    exit(-1);
  }

/*
 dlh = dlopen("/proc/self/exe", RTLD_LOCAL | RTLD_LAZY);
if (dlh == NULL) {
  fprintf(stderr, "dlsym: %s\n", dlerror());
  exit(-1);
}

*/
#if 0
  /* Search curdir for *.mtdt file and load them */
  do {
    errno = 0;
    struct dirent *cur_dirent = readdir(curdir);

    if (cur_dirent == NULL && errno) {
      perror("readdir");
      exit(-1);
    } else if (cur_dirent == NULL) {
      break;
    }

    if (cur_dirent->d_type != DT_REG ||
        (strstr(cur_dirent->d_name, ".mtdt") == NULL)) {
      continue;
    }

    if (parse_mtdt_file(cur_dirent->d_name, dlh)) {
      // dlclose(dlh);
      exit(-1);
    }
  } while (1);
#endif
  return 0;
}
