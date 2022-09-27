#include <cstdint>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <sys/types.h>
#include <dirent.h>
#include <dlfcn.h>

#include <errno.h>

#include "sc_array.h"

// extern ?type? metadata_arr*;

static struct sc_array_str addr_arr;

static int parse_mtdt_file(const char *fname) {

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

    if (parse_mtdt_file(cur_dirent->d_name)) {
      exit(-1);
    }
  } while (1);

  return 0;
}
