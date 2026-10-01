#ifndef CINEMA_LOGGER_H
#define CINEMA_LOGGER_H

#include <stdio.h>

/* Restarts the sequence number and the clock; out == NULL disables logging. */
void logger_init(FILE *out);

void log_event(const char *actor, const char *format, ...)
    __attribute__((format(printf, 2, 3)));

#endif
