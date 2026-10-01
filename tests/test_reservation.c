#define _POSIX_C_SOURCE 200809L

#include "logger.h"
#include "reservation.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int failures = 0;

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            ++failures;                                                         \
        }                                                                       \
    } while (0)

typedef struct {
    int client_id;
    int seat;
    Response response;
} Job;

static pthread_barrier_t start_line;

static FILE *begin_log(void)
{
    FILE *log = tmpfile();

    CHECK(log != NULL);
    logger_init(log);
    return log;
}

static void end_log(FILE *log)
{
    logger_init(NULL);
    fclose(log);
}

static int count_in_log(FILE *log, const char *needle)
{
    char line[256];
    int count = 0;

    rewind(log);
    while (fgets(line, sizeof(line), log) != NULL) {
        if (strstr(line, needle) != NULL) {
            ++count;
        }
    }
    return count;
}

/* Most workers inside the critical section of `seat` at the same time, read
 * from the order of the log lines. */
static int max_overlap(FILE *log, int seat)
{
    char line[256];
    char enter[64];
    char leave[64];
    int depth = 0;
    int deepest = 0;

    snprintf(enter, sizeof(enter), "entering critical section (Resource %d)", seat);
    snprintf(leave, sizeof(leave), "leaving critical section (Resource %d)", seat);
    rewind(log);
    while (fgets(line, sizeof(line), log) != NULL) {
        if (strstr(line, enter) != NULL) {
            ++depth;
            if (depth > deepest) {
                deepest = depth;
            }
        } else if (strstr(line, leave) != NULL) {
            --depth;
        }
    }
    return deepest;
}

static void *reserve_job(void *argument)
{
    Job *job = argument;

    pthread_barrier_wait(&start_line);
    reservation_reserve(job->client_id, job->client_id, job->seat, &job->response);
    return NULL;
}

/* Run all jobs at the same instant; returns how many were told SUCCESS. */
static int run_reservers(Job jobs[], int count)
{
    pthread_t threads[16];
    int successes = 0;
    int index;

    CHECK(pthread_barrier_init(&start_line, NULL, (unsigned)count) == 0);
    for (index = 0; index < count; ++index) {
        CHECK(pthread_create(&threads[index], NULL, reserve_job, &jobs[index]) == 0);
    }
    for (index = 0; index < count; ++index) {
        pthread_join(threads[index], NULL);
    }
    pthread_barrier_destroy(&start_line);
    for (index = 0; index < count; ++index) {
        if (jobs[index].response.success == RESPONSE_SUCCESS) {
            ++successes;
        }
    }
    return successes;
}

static void test_sequential_semantics(void)
{
    Response r;
    FILE *log = begin_log();

    CHECK(reservation_init(SYNC_MODE_SYNC, 0, 0) == 0);

    reservation_status(1, 10, &r);
    CHECK(r.success == RESPONSE_SUCCESS);
    CHECK(strstr(r.message, "Seat 10 is available.") != NULL);

    reservation_reserve(1, 1, 10, &r);
    CHECK(r.success == RESPONSE_SUCCESS);
    CHECK(strstr(r.message, "Seat 10 reserved successfully.") != NULL);
    CHECK(reservation_owner(10) == 1);

    reservation_reserve(1, 2, 10, &r);
    CHECK(r.success == RESPONSE_FAILED);
    CHECK(strstr(r.message, "Seat 10 is already reserved.") != NULL);
    CHECK(reservation_owner(10) == 1);

    reservation_status(1, 10, &r);
    CHECK(strstr(r.message, "Seat 10 is reserved by Client 1.") != NULL);

    reservation_cancel(1, 2, 10, &r);
    CHECK(r.success == RESPONSE_FAILED);
    CHECK(strstr(r.message, "Seat 10 belongs to another client.") != NULL);
    CHECK(reservation_owner(10) == 1);

    reservation_cancel(1, 1, 10, &r);
    CHECK(r.success == RESPONSE_SUCCESS);
    CHECK(strstr(r.message, "Seat 10 reservation cancelled.") != NULL);
    CHECK(reservation_owner(10) == 0);

    reservation_cancel(1, 1, 10, &r);
    CHECK(r.success == RESPONSE_FAILED);
    CHECK(strstr(r.message, "Seat 10 is not reserved.") != NULL);

    reservation_reserve(1, 7, 3, &r);
    reservation_list(1, &r);
    CHECK(r.success == RESPONSE_SUCCESS);
    CHECK(strstr(r.message, "Seats (- = available, Cn = reserved by Client n):") != NULL);
    CHECK(strstr(r.message, " 1:-") != NULL);
    CHECK(strstr(r.message, " 3:C7") != NULL);
    CHECK(strstr(r.message, " 20:-") != NULL);

    CHECK(count_in_log(log, "[Worker-1] entering critical section (Resource 10)") > 0);
    CHECK(count_in_log(log, "[Worker-1] entering critical section (all seats)") == 1);
    CHECK(count_in_log(log, "(NO LOCK)") == 0);
    CHECK(count_in_log(log, "RACE DETECTED") == 0);

    reservation_destroy();
    end_log(log);
}

static void test_list_survives_a_long_owner_id(void)
{
    Response r;

    logger_init(NULL);
    CHECK(reservation_init(SYNC_MODE_SYNC, 0, 0) == 0);
    reservation_reserve(1, 999999, 20, &r);
    reservation_list(1, &r);
    CHECK(r.success == RESPONSE_SUCCESS);
    CHECK(strstr(r.message, " 20:C999999") != NULL);
    reservation_destroy();
}

static void test_invalid_delay_is_rejected(void)
{
    CHECK(reservation_init(SYNC_MODE_SYNC, -1, 10) == -1);
    CHECK(reservation_init(SYNC_MODE_SYNC, 100, 50) == -1);
}

static void test_sync_admits_one_winner(void)
{
    enum { CLIENTS = 8 };
    Job jobs[CLIENTS];
    FILE *log = begin_log();
    int successes;
    int winner = 0;
    int index;

    CHECK(reservation_init(SYNC_MODE_SYNC, 20, 60) == 0);
    for (index = 0; index < CLIENTS; ++index) {
        jobs[index].client_id = index + 1;
        jobs[index].seat = 10;
    }
    successes = run_reservers(jobs, CLIENTS);
    for (index = 0; index < CLIENTS; ++index) {
        if (jobs[index].response.success == RESPONSE_SUCCESS) {
            winner = jobs[index].client_id;
        }
    }

    CHECK(successes == 1);
    CHECK(winner != 0 && reservation_owner(10) == winner);
    CHECK(max_overlap(log, 10) == 1);
    CHECK(count_in_log(log, "RACE DETECTED") == 0);
    CHECK(count_in_log(log, "(NO LOCK)") == 0);
    CHECK(count_in_log(log, "waiting for mutex of Resource 10") >= 1);

    reservation_destroy();
    end_log(log);
}

static void test_nosync_shows_the_race(void)
{
    enum { CLIENTS = 5 };
    Job jobs[CLIENTS];
    FILE *log = begin_log();
    int successes;
    int owner;
    int owner_is_a_client = 0;
    int index;

    CHECK(reservation_init(SYNC_MODE_NOSYNC, 50, 100) == 0);
    for (index = 0; index < CLIENTS; ++index) {
        jobs[index].client_id = index + 1;
        jobs[index].seat = 10;
    }
    successes = run_reservers(jobs, CLIENTS);
    owner = reservation_owner(10);
    for (index = 0; index < CLIENTS; ++index) {
        if (jobs[index].client_id == owner) {
            owner_is_a_client = 1;
        }
    }

    /* Lost update: several clients were told SUCCESS, one of them owns the seat. */
    CHECK(successes >= 2);
    CHECK(owner_is_a_client);
    CHECK(max_overlap(log, 10) >= 2);
    CHECK(count_in_log(log, "RACE DETECTED") >= 1);
    CHECK(count_in_log(log, "(NO LOCK)") > 0);
    CHECK(count_in_log(log, "waiting for mutex") == 0);

    reservation_destroy();
    end_log(log);
}

static void test_sync_locks_each_seat_separately(void)
{
    Job jobs[3];
    struct timespec start;
    struct timespec end;
    long elapsed_ms;
    int successes;
    int index;

    logger_init(NULL);
    CHECK(reservation_init(SYNC_MODE_SYNC, 150, 150) == 0);
    for (index = 0; index < 3; ++index) {
        jobs[index].client_id = index + 1;
        jobs[index].seat = index + 1;
    }
    clock_gettime(CLOCK_MONOTONIC, &start);
    successes = run_reservers(jobs, 3);
    clock_gettime(CLOCK_MONOTONIC, &end);
    elapsed_ms = (long)(end.tv_sec - start.tv_sec) * 1000L +
                 (end.tv_nsec - start.tv_nsec) / 1000000L;

    CHECK(successes == 3);
    /* One table-wide lock would need at least 3 x 150 = 450 ms. */
    CHECK(elapsed_ms < 400);
    reservation_destroy();
}

typedef struct {
    int worker_id;
    int iterations;
} Loop;

static void *list_loop(void *argument)
{
    Loop *loop = argument;
    Response r;
    int index;

    for (index = 0; index < loop->iterations; ++index) {
        reservation_list(loop->worker_id, &r);
    }
    return NULL;
}

static void *reserve_cancel_loop(void *argument)
{
    Loop *loop = argument;
    Response r;
    int index;
    int seat;

    for (index = 0; index < loop->iterations; ++index) {
        seat = 1 + (index * 7) % MAX_SEATS;
        reservation_reserve(loop->worker_id, loop->worker_id, seat, &r);
        reservation_cancel(loop->worker_id, loop->worker_id, seat, &r);
    }
    return NULL;
}

/* 1-based number of the first log line containing `needle`, 0 if none. */
static int line_number(FILE *log, const char *needle)
{
    char line[256];
    int number = 0;

    rewind(log);
    while (fgets(line, sizeof(line), log) != NULL) {
        ++number;
        if (strstr(line, needle) != NULL) {
            return number;
        }
    }
    return 0;
}

static void *hold_seat_10(void *argument)
{
    Response r;

    (void)argument;
    reservation_reserve(1, 1, 10, &r);
    return NULL;
}

static void *list_after_a_pause(void *argument)
{
    Response r;
    struct timespec pause = {0, 100 * 1000000L};

    (void)argument;
    nanosleep(&pause, NULL);
    reservation_list(2, &r);
    return NULL;
}

/* LIST takes the seat mutexes one by one. When one of them is busy the log
 * must say so, otherwise it shows other workers waiting with no visible cause. */
static void test_list_logs_when_it_waits_for_a_seat(void)
{
    pthread_t holder;
    pthread_t lister;
    FILE *log = begin_log();
    int waiting;
    int released;

    CHECK(reservation_init(SYNC_MODE_SYNC, 300, 300) == 0);
    CHECK(pthread_create(&holder, NULL, hold_seat_10, NULL) == 0);
    CHECK(pthread_create(&lister, NULL, list_after_a_pause, NULL) == 0);
    pthread_join(holder, NULL);
    pthread_join(lister, NULL);

    waiting = line_number(log, "[Worker-2] waiting for mutex of Resource 10");
    released = line_number(log, "[Worker-1] leaving critical section (Resource 10)");
    CHECK(line_number(log, "[Worker-2] locking all seats in order 1..20") != 0);
    CHECK(waiting != 0);
    CHECK(waiting < released);
    CHECK(count_in_log(log, "[Worker-2] waiting for mutex") == 1);
    CHECK(count_in_log(log, "[Worker-2] entering critical section (all seats)") == 1);

    reservation_destroy();
    end_log(log);
}

static void test_list_and_writers_do_not_deadlock(void)
{
    pthread_t threads[4];
    Loop loops[4] = {{1, 300}, {2, 300}, {3, 300}, {4, 300}};
    int index;

    logger_init(NULL);
    CHECK(reservation_init(SYNC_MODE_SYNC, 0, 0) == 0);
    CHECK(pthread_create(&threads[0], NULL, list_loop, &loops[0]) == 0);
    CHECK(pthread_create(&threads[1], NULL, list_loop, &loops[1]) == 0);
    CHECK(pthread_create(&threads[2], NULL, reserve_cancel_loop, &loops[2]) == 0);
    CHECK(pthread_create(&threads[3], NULL, reserve_cancel_loop, &loops[3]) == 0);
    for (index = 0; index < 4; ++index) {
        pthread_join(threads[index], NULL);
    }

    /* A hang here is caught by `timeout` in the Makefile. Every reserve is
     * followed by a cancel from the same client, so all seats end free. */
    for (index = 1; index <= MAX_SEATS; ++index) {
        CHECK(reservation_owner(index) == 0);
    }
    reservation_destroy();
}

int main(void)
{
    test_sequential_semantics();
    test_list_survives_a_long_owner_id();
    test_invalid_delay_is_rejected();
    test_sync_admits_one_winner();
    test_nosync_shows_the_race();
    test_sync_locks_each_seat_separately();
    test_list_logs_when_it_waits_for_a_seat();
    test_list_and_writers_do_not_deadlock();

    if (failures != 0) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("reservation tests passed\n");
    return 0;
}
