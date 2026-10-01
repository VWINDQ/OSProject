#define _POSIX_C_SOURCE 200809L

#include "reservation/reservation.h"

#include "utils/logger.h"

#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define ACTOR_SIZE 32

/*
 * Shared Data of the experiment: owners[seat] is 0 when the seat is
 * available, otherwise the client id of the owner.
 *
 * `volatile` only forces every access to go to memory, so the race stays
 * observable after the random delay. It does NOT make the accesses safe;
 * that is the job of seat_mutex[] (SYNC_MODE_SYNC).
 */
static volatile int owners[MAX_SEATS + 1];
static pthread_mutex_t seat_mutex[MAX_SEATS + 1];
static SyncMode sync_mode = SYNC_MODE_SYNC;
static int config_delay_min_ms = 0;
static int config_delay_max_ms = 0;

static void mutex_lock(pthread_mutex_t *mutex)
{
    int error = pthread_mutex_lock(mutex);

    if (error != 0) {
        fprintf(stderr, "pthread_mutex_lock failed: %s\n", strerror(error));
        abort();
    }
}

static void mutex_unlock(pthread_mutex_t *mutex)
{
    int error = pthread_mutex_unlock(mutex);

    if (error != 0) {
        fprintf(stderr, "pthread_mutex_unlock failed: %s\n", strerror(error));
        abort();
    }
}

void response_set(Response *response, int success, const char *format, ...)
{
    va_list arguments;

    memset(response, 0, sizeof(*response));
    response->success = success;
    va_start(arguments, format);
    vsnprintf(response->message, sizeof(response->message), format, arguments);
    va_end(arguments);
}

int reservation_init(SyncMode mode, int delay_min_ms, int delay_max_ms)
{
    int seat;
    int error;

    if (delay_min_ms < 0 || delay_max_ms < delay_min_ms) {
        return -1;
    }

    sync_mode = mode;
    config_delay_min_ms = delay_min_ms;
    config_delay_max_ms = delay_max_ms;

    for (seat = 0; seat <= MAX_SEATS; ++seat) {
        owners[seat] = 0;
        error = pthread_mutex_init(&seat_mutex[seat], NULL);
        if (error != 0) {
            fprintf(stderr, "pthread_mutex_init failed: %s\n", strerror(error));
            while (--seat >= 0) {
                pthread_mutex_destroy(&seat_mutex[seat]);
            }
            return -1;
        }
    }
    return 0;
}

void reservation_destroy(void)
{
    int seat;

    for (seat = 0; seat <= MAX_SEATS; ++seat) {
        pthread_mutex_destroy(&seat_mutex[seat]);
    }
}

static void actor_name(char *buffer, size_t size, int worker_id)
{
    snprintf(buffer, size, "Worker-%d", worker_id);
}

static int random_delay_ms(int worker_id)
{
    static _Thread_local unsigned int seed = 0;
    struct timespec now;

    if (seed == 0) {
        clock_gettime(CLOCK_REALTIME, &now);
        seed = (unsigned int)now.tv_nsec ^ ((unsigned int)worker_id * 2654435761u);
        if (seed == 0) {
            seed = 1;
        }
    }
    if (config_delay_max_ms <= config_delay_min_ms) {
        return config_delay_min_ms;
    }
    return config_delay_min_ms +
           (int)(rand_r(&seed) %
                 (unsigned int)(config_delay_max_ms - config_delay_min_ms + 1));
}

static void sleep_ms(int milliseconds)
{
    struct timespec request;
    struct timespec remaining;

    request.tv_sec = milliseconds / 1000;
    request.tv_nsec = (long)(milliseconds % 1000) * 1000000L;
    while (nanosleep(&request, &remaining) == -1 && errno == EINTR) {
        request = remaining;
    }
}

/* Take one seat's mutex, logging when it is busy so a wait is never invisible. */
static void lock_seat(const char *actor, int seat)
{
    if (pthread_mutex_trylock(&seat_mutex[seat]) != 0) {
        log_event(actor, "waiting for mutex of Resource %d", seat);
        mutex_lock(&seat_mutex[seat]);
    }
}

/* Critical-section entry/exit for one seat. In SYNC_MODE_NOSYNC nothing is
 * locked, and the log lines say so. */
static void enter_seat(const char *actor, int seat)
{
    if (sync_mode == SYNC_MODE_SYNC) {
        lock_seat(actor, seat);
        log_event(actor, "entering critical section (Resource %d)", seat);
    } else {
        log_event(actor, "entering critical section (Resource %d) (NO LOCK)", seat);
    }
}

/* The "leaving" line is logged before the unlock so the log order never
 * contradicts the real order of events. */
static void leave_seat(const char *actor, int seat)
{
    if (sync_mode == SYNC_MODE_SYNC) {
        log_event(actor, "leaving critical section (Resource %d)", seat);
        mutex_unlock(&seat_mutex[seat]);
    } else {
        log_event(actor, "leaving critical section (Resource %d) (NO LOCK)", seat);
    }
}

void reservation_list(int worker_id, Response *response)
{
    char actor[ACTOR_SIZE];
    char entry[32];
    int snapshot[MAX_SEATS + 1];
    size_t used;
    int seat;

    actor_name(actor, sizeof(actor), worker_id);

    if (sync_mode == SYNC_MODE_SYNC) {
        /* Seats are taken one by one in ascending order (no deadlock), so
         * while LIST waits for a busy seat it already holds the lower ones. */
        log_event(actor, "locking all seats in order 1..%d", MAX_SEATS);
        for (seat = 1; seat <= MAX_SEATS; ++seat) {
            lock_seat(actor, seat);
        }
        log_event(actor, "entering critical section (all seats)");
    } else {
        log_event(actor, "entering critical section (all seats) (NO LOCK)");
    }
    for (seat = 1; seat <= MAX_SEATS; ++seat) {
        snapshot[seat] = owners[seat];
    }
    if (sync_mode == SYNC_MODE_SYNC) {
        log_event(actor, "leaving critical section (all seats)");
        for (seat = MAX_SEATS; seat >= 1; --seat) {
            mutex_unlock(&seat_mutex[seat]);
        }
    } else {
        log_event(actor, "leaving critical section (all seats) (NO LOCK)");
    }

    response_set(response, RESPONSE_SUCCESS,
                 "Seats (- = available, Cn = reserved by Client n):");
    used = strlen(response->message);
    for (seat = 1; seat <= MAX_SEATS; ++seat) {
        if (snapshot[seat] == 0) {
            snprintf(entry, sizeof(entry), " %d:-", seat);
        } else {
            snprintf(entry, sizeof(entry), " %d:C%d", seat, snapshot[seat]);
        }
        if (used + strlen(entry) >= sizeof(response->message)) {
            response_set(response, RESPONSE_FAILED,
                         "LIST response does not fit in the message buffer.");
            return;
        }
        memcpy(response->message + used, entry, strlen(entry) + 1);
        used += strlen(entry);
    }
}

void reservation_status(int worker_id, int seat, Response *response)
{
    char actor[ACTOR_SIZE];
    int owner;

    actor_name(actor, sizeof(actor), worker_id);

    enter_seat(actor, seat);
    owner = owners[seat];
    if (owner == 0) {
        log_event(actor, "check Resource %d: AVAILABLE", seat);
    } else {
        log_event(actor, "check Resource %d: RESERVED by Client-%d", seat, owner);
    }
    leave_seat(actor, seat);

    if (owner == 0) {
        response_set(response, RESPONSE_SUCCESS, "Seat %d is available.", seat);
    } else {
        response_set(response, RESPONSE_SUCCESS,
                     "Seat %d is reserved by Client %d.", seat, owner);
    }
}

void reservation_reserve(int worker_id, int client_id, int seat,
                         Response *response)
{
    char actor[ACTOR_SIZE];
    int owner;
    int current;
    int delay;

    actor_name(actor, sizeof(actor), worker_id);

    enter_seat(actor, seat);

    owner = owners[seat];
    if (owner != 0) {
        log_event(actor, "check Resource %d: RESERVED by Client-%d", seat, owner);
        log_event(actor, "Resource %d already reserved", seat);
        response_set(response, RESPONSE_FAILED,
                     "Seat %d is already reserved.", seat);
    } else {
        log_event(actor, "check Resource %d: AVAILABLE", seat);

        /* Widen the race window (assignment requirement: 50-500 ms). */
        delay = random_delay_ms(worker_id);
        log_event(actor, "random delay %d ms", delay);
        sleep_ms(delay);

        /* Observation only: in NOSYNC another worker may have reserved the
         * seat during the delay. We still overwrite it, like real buggy code. */
        current = owners[seat];
        if (current != 0) {
            log_event(actor,
                      "RACE DETECTED: Resource %d is now owned by Client-%d, overwriting",
                      seat, current);
        }
        owners[seat] = client_id;
        log_event(actor, "Resource %d reserved by Client-%d", seat, client_id);
        response_set(response, RESPONSE_SUCCESS,
                     "Seat %d reserved successfully.", seat);
    }

    leave_seat(actor, seat);
}

void reservation_cancel(int worker_id, int client_id, int seat,
                        Response *response)
{
    char actor[ACTOR_SIZE];
    int owner;

    actor_name(actor, sizeof(actor), worker_id);

    enter_seat(actor, seat);

    owner = owners[seat];
    if (owner == 0) {
        log_event(actor, "check Resource %d: AVAILABLE", seat);
        response_set(response, RESPONSE_FAILED, "Seat %d is not reserved.", seat);
    } else if (owner != client_id) {
        log_event(actor, "check Resource %d: RESERVED by Client-%d", seat, owner);
        response_set(response, RESPONSE_FAILED,
                     "Seat %d belongs to another client.", seat);
    } else {
        log_event(actor, "check Resource %d: RESERVED by Client-%d", seat, owner);
        owners[seat] = 0;
        log_event(actor, "Resource %d reservation cancelled by Client-%d", seat,
                  client_id);
        response_set(response, RESPONSE_SUCCESS,
                     "Seat %d reservation cancelled.", seat);
    }

    leave_seat(actor, seat);
}

int reservation_owner(int seat)
{
    return owners[seat];
}

void reservation_print_table(FILE *out)
{
    int seat;
    int owner;

    fprintf(out, "Resource  Status     Owner\n");
    fprintf(out, "--------------------------------\n");
    for (seat = 1; seat <= MAX_SEATS; ++seat) {
        owner = owners[seat];
        if (owner == 0) {
            fprintf(out, "%-9d AVAILABLE  -\n", seat);
        } else {
            fprintf(out, "%-9d RESERVED   Client-%d\n", seat, owner);
        }
    }
    fflush(out);
}
