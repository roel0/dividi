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
#include <malloc.h>
#if defined __linux__
  #include <linux/limits.h>
#endif
#include "dividi.h"
#include "util.h"

static struct s_link *active_link = NULL;

/**
 * Parse the settigns for the current
 * parsed link
 */
static int conf_parse_link_settings(char *key, char *value)
{
  if(strcmp(key, "timeout") == 0) {
    active_link->serial.timeout = atoi(value);
  } else if(strcmp(key, "baudrate") == 0) {
    active_link->serial.baudrate = atoi(value);
  } else if(strcmp(key, "data_bits") == 0) {
    active_link->serial.data_bits = atoi(value);
  } else if(strcmp(key, "stop_bits") == 0) {
    active_link->serial.stop_bits = atoi(value);
  } else if(strcmp(key, "parity") == 0) {
    //XXX
  } else if(strcmp(key, "flow") == 0) {
    //XXX
  } else {
    return -1;
  }
  return 0;
}

/**
 * Parse the global settings
 */
static int conf_parse_settings(char *key, char *value)
{
  if(strcmp(key, "cert") == 0) {
    set_cert_file(value);
  } else if(strcmp(key, "key") == 0) {
    set_key_file(value);
  } else if(strcmp(key, "rootCA") == 0) {
    set_root_file(value);
  } else {
    return -1;
  }
  return 0;
}

/**
 * Parse a new link
 */
static int conf_parse_link(char *line, int line_len)
{
  char *serial_port, *tcp_port, *link;
  /* line == '[]' */
  if(line_len <= 2) {
    return -1;
  }
  /* Remove [ and ] */
  link = strdup(line + 1);
  link[line_len - 2] = '\0';

  /* Split link string in tcp_port and serial_port */
  tcp_port = link;
  serial_port = link;
  strsep(&tcp_port, ":");
  strtrim(tcp_port);
  strtrim(serial_port);
  if(tcp_port == NULL || serial_port == NULL) {
    free(link);
    return -1;
  }
  active_link = add_link(serial_port, tcp_port);
  free(link);
  return 0;
}

/**
 * Parse a value/key line
 */
static int conf_parse_key(char *line)
{
  char *key, *value;
  key = line;
  value = line;
  strsep(&value, "=");
  strtrim(key);
  strtrim(value);
  if(key == NULL || value == NULL) {
    return -1;
  }
  if(active_link == NULL) {
    //parse dividi settings
    return conf_parse_settings(key, value);
  } else {
    //parse active link settings
    return conf_parse_link_settings(key,value);
  }
  return 0;
}

/**
 * Parse a configuration file
 */
int conf_parse(const char *file)
{
  FILE *fp = NULL;
  char line[PATH_MAX];
  int linenum = 0;
  int ret = 0;
  int line_len = 0;

  fp = fopen(file, "r");
  if(fp == NULL) {
    perror("fopen failed");
    goto cleanup;
  }

  while(fgets(line, PATH_MAX, fp)) {
    char *comment;

    linenum++;

    /* ignore comments */
    if((comment = strchr(line, '#'))) {
      *comment = '\0';
    }

    line_len = strtrim(line);
    if(line_len == 0) {
      continue;
    }

    if(line[0] == '[' && line[line_len - 1] == ']') {
      if(conf_parse_link(line, line_len) < 0) {
        ret = -1;
        fprintf(stderr, "Configuration file (%s) error at line %d\n", file, linenum);
        goto cleanup;
      }
    }
    else if(conf_parse_key(line) < 0) {
      ret = -1;
      fprintf(stderr, "Configuration file (%s) error at line %d\n", file, linenum);
      goto cleanup;
    }
  }

cleanup:
  if(fp) {
    fclose(fp);
  }
  return ret;
}
