#ifndef CINEMA_RESERVATION_H
#define CINEMA_RESERVATION_H

#include "models/message.h"

#include <stdio.h>

typedef enum {
    SYNC_MODE_SYNC,
    SYNC_MODE_NOSYNC
} SyncMode;

void response_set(Response *response, int success, const char *format, ...)
    __attribute__((format(printf, 3, 4)));

/* Returns 0 on success, -1 on invalid delays or a mutex failure.
 * Call reservation_destroy() before initialising again. */
int reservation_init(SyncMode mode, int delay_min_ms, int delay_max_ms);
void reservation_destroy(void);

/* The caller has already checked 1 <= seat <= MAX_SEATS and client_id >= 1. */
void reservation_list(int worker_id, Response *response);
void reservation_status(int worker_id, int seat, Response *response);
void reservation_reserve(int worker_id, int client_id, int seat,
                         Response *response);
void reservation_cancel(int worker_id, int client_id, int seat,
                        Response *response);

/* Not synchronised: for tests and for the final table. */
int reservation_owner(int seat);
void reservation_print_table(FILE *out);

#endif
