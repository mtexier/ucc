#ifndef __STDLIB_H
#define __STDLIB_H

#include "sys/types.h"
#include "macros.h"

int atoi(const char *);

void *malloc(size_t);
void *calloc(size_t count, size_t);
void *realloc(void *, size_t);
void  free(   void *);

void exit(int);
void abort(void);

char *getenv(const char *);

extern char *__progname;

int atexit(void (*)(void));

#endif
