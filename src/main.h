#ifndef MAIN_H
#define MAIN_H

#include <stdarg.h>
#include <stdio.h>

#define MAX_BUFFER_SIZE 4096
#define NUM_WORKERS 5

// FIXME: Move functions way from here

void log_error(const char *format, ...);
void log_info(const char *format, ...);
void log_warn(const char *format, ...);

#endif // MAIN_H
