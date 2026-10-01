#ifndef CINEMA_LOGGER_H
#define CINEMA_LOGGER_H

#include <stdio.h>

/* Send log lines to `out` (NULL disables logging) and restart the sequence
 * number and the clock. Call it before any other thread logs. */
void logger_init(FILE *out);

/* Print "[#<seq> +<ms>ms][<actor>] <text>" as one line. Safe to call from
 * several threads: the sequence number is assigned under the same mutex that
 * serialises the output, so it always matches the order of the lines. */
void log_event(const char *actor, const char *format, ...)
    __attribute__((format(printf, 2, 3)));

#endif
