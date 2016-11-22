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
#ifndef __UTIL_H__
#define __UTIL_H__

/**
 * Split a string based on a delimiter
 */
char* strsep_delim(char** stringp, const char* delim);
/*
 * Trim whitespace and newlines from a string
 */
size_t strtrim(char *str);

/*
 * Safe copy a file path
 */
void copy_file_path(char *dst, char *src);

#endif
