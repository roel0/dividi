/*****************************************************************************
 *
 * Dividi : A ComPort TCP share tool
 *
 *          Copyright (c) 2016. Some rights reserved.
 *          See LICENSE and COPYING for usage.
 *
 * Authors: Roel Postelmans
 *
 ****************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "util.h"
#ifdef __linux__
  #include <linux/limits.h>
#endif

/**
 * Split a string based on a delimiter
 */
char* strsep_delim(char** stringp, const char* delim)
{
  char* start = *stringp;
  char* p;

  p = (start != NULL) ? strpbrk(start, delim) : NULL;

  if (p == NULL) {
    *stringp = NULL;
  } else {
    *p = '\0';
    *stringp = p + 1;
  }

  return start;
}

/*
 * Trim whitespace and newlines from a string
 */
size_t strtrim(char *str)
{
  char *end, *pch = str;

  if(str == NULL || *str == '\0') {
    return 0;
  }

  while(isspace((unsigned char)*pch)) {
    pch++;
  }
  if(pch != str) {
    if(*pch != '\0') {
      memmove(str, pch, strlen(pch) + 1);
    } else {
      *str = '\0';
      return 0;
    }
  }

  end = (str + strlen(str) - 1);
  while(isspace((unsigned char)*end)) {
    end--;
  }
  *++end = '\0';

  return end - pch;
}

/*
 * Safe copy a file path
 */
void copy_file_path(char *dst, char *src)
{
  size_t size;
  if((size = strlen(src)+1) > PATH_MAX) {
    fprintf(stderr, "Filepaths can be maximum %d characters long\n", PATH_MAX);
    exit(-1);
  }
  memcpy(dst, src, strlen(src)+1);
}

