#ifndef CINEMA_RESERVATION_H
#define CINEMA_RESERVATION_H

#include "common.h"

#include <stdio.h>

typedef enum {
    SYNC_MODE_SYNC,
    SYNC_MODE_NOSYNC
} SyncMode;

/* Fill `response` with a printf-style message. */
void response_set(Response *response, int success, const char *format, ...)
    __attribute__((format(printf, 3, 4)));

/* Prepare the seat table. In SYNC_MODE_SYNC every seat is protected by its own
 * mutex; in SYNC_MODE_NOSYNC nothing is locked so the race can be observed.
 * RESERVE sleeps a random time in [delay_min_ms, delay_max_ms] between its
 * check and its update. Returns 0 on success, -1 on invalid delays or when a
 * mutex cannot be created. Call reservation_destroy() before calling it again. */
int reservation_init(SyncMode mode, int delay_min_ms, int delay_max_ms);
void reservation_destroy(void);

/* Command handlers. The caller has already checked 1 <= seat <= MAX_SEATS and
 * client_id >= 1. `worker_id` only labels the log lines. */
void reservation_list(int worker_id, Response *response);
void reservation_status(int worker_id, int seat, Response *response);
void reservation_reserve(int worker_id, int client_id, int seat,
                         Response *response);
void reservation_cancel(int worker_id, int client_id, int seat,
                        Response *response);

/* Owner of a seat, 0 when available. Not synchronised: for tests and for the
 * final table printed after all workers have stopped. */
int reservation_owner(int seat);
void reservation_print_table(FILE *out);

#endif
