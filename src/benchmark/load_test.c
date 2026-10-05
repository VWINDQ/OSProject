#define _POSIX_C_SOURCE 200809L

/* Usage: load_test <clients 1-256> <seconds 1-300>. Prints one RESULT line; exits 1 if any request failed. */

#include "benchmark/latency_stats.h"
#include "constants/constants.h"
#include "models/message.h"

#include <errno.h>
#include <fcntl.h>
#include <mqueue.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define MAX_LOAD_CLIENTS 256
#define MAX_LOAD_SECONDS 300
#define REPLY_TIMEOUT_SECONDS 5
#define LOAD_CLIENT_ID_BASE 900000
#define INITIAL_SAMPLES 4096

typedef struct {
    int number;
    long duration_ns;
    long *latencies_ns;
    size_t count;
    size_t capacity;
    unsigned long errors;
} LoadClient;

static long elapsed_ns(const struct timespec *from, const struct timespec *to)
{
    return (long)(to->tv_sec - from->tv_sec) * 1000000000L +
           (to->tv_nsec - from->tv_nsec);
}

static struct timespec realtime_in(int seconds)
{
    struct timespec deadline;

    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += seconds;
    return deadline;
}

static int record_sample(LoadClient *client, long latency_ns)
{
    long *grown;

    if (client->count == client->capacity) {
        size_t capacity = client->capacity == 0 ? INITIAL_SAMPLES
                                                : client->capacity * 2;
        grown = realloc(client->latencies_ns, capacity * sizeof(*grown));
        if (grown == NULL) {
            return -1;
        }
        client->latencies_ns = grown;
        client->capacity = capacity;
    }
    client->latencies_ns[client->count++] = latency_ns;
    return 0;
}

static void *run_client(void *argument)
{
    LoadClient *client = argument;
    char reply_name[QUEUE_NAME_SIZE];
    struct mq_attr attributes;
    struct timespec started;
    struct timespec begin;
    struct timespec end;
    struct timespec deadline;
    Request request;
    Response response;
    mqd_t request_queue;
    mqd_t reply_queue;
    int client_id = LOAD_CLIENT_ID_BASE + client->number;
    int seat = 1 + client->number % MAX_SEATS;

    snprintf(reply_name, sizeof(reply_name), "%s%d_%ld", CLIENT_QUEUE_PREFIX,
             client_id, (long)getpid());
    memset(&attributes, 0, sizeof(attributes));
    attributes.mq_maxmsg = QUEUE_MAX_MESSAGES;
    attributes.mq_msgsize = RESPONSE_MESSAGE_SIZE_BYTES;

    request_queue = mq_open(REQUEST_QUEUE, O_WRONLY);
    if (request_queue == (mqd_t)-1) {
        ++client->errors;
        return NULL;
    }
    mq_unlink(reply_name);
    reply_queue = mq_open(reply_name, O_RDONLY | O_CREAT | O_EXCL,
                          RESPONSE_QUEUE_PERMISSIONS, &attributes);
    if (reply_queue == (mqd_t)-1) {
        ++client->errors;
        mq_close(request_queue);
        return NULL;
    }

    memset(&request, 0, sizeof(request));
    request.client_id = client_id;
    snprintf(request.command, sizeof(request.command), "STATUS");
    snprintf(request.response_queue, sizeof(request.response_queue), "%s",
             reply_name);

    clock_gettime(CLOCK_MONOTONIC, &started);
    for (;;) {
        clock_gettime(CLOCK_MONOTONIC, &begin);
        if (elapsed_ns(&started, &begin) >= client->duration_ns) {
            break;
        }

        request.resource_id = seat;
        seat = seat % MAX_SEATS + 1;
        deadline = realtime_in(REPLY_TIMEOUT_SECONDS);
        if (mq_timedsend(request_queue, (const char *)&request,
                         sizeof(request), 0, &deadline) == -1) {
            ++client->errors;
            break;
        }
        deadline = realtime_in(REPLY_TIMEOUT_SECONDS);
        if (mq_timedreceive(reply_queue, (char *)&response, sizeof(response),
                            NULL, &deadline) != (ssize_t)sizeof(response) ||
            response.success != RESPONSE_SUCCESS) {
            ++client->errors;
            break;
        }
        clock_gettime(CLOCK_MONOTONIC, &end);
        if (record_sample(client, elapsed_ns(&begin, &end)) == -1) {
            ++client->errors;
            break;
        }
    }

    mq_close(reply_queue);
    mq_unlink(reply_name);
    mq_close(request_queue);
    return NULL;
}

static int parse_bounded(const char *text, int minimum, int maximum, int *value)
{
    char *end_pointer = NULL;
    long parsed;

    errno = 0;
    parsed = strtol(text, &end_pointer, 10);
    if (errno == ERANGE || end_pointer == text || *end_pointer != '\0' ||
        parsed < minimum || parsed > maximum) {
        return -1;
    }
    *value = (int)parsed;
    return 0;
}

int main(int argc, char *argv[])
{
    LoadClient clients[MAX_LOAD_CLIENTS];
    pthread_t threads[MAX_LOAD_CLIENTS];
    int started[MAX_LOAD_CLIENTS] = {0};
    struct timespec begin;
    struct timespec end;
    long *all = NULL;
    size_t total = 0;
    size_t used = 0;
    unsigned long errors = 0;
    double seconds;
    int client_count;
    int duration;
    int index;

    if (argc != 3 ||
        parse_bounded(argv[1], 1, MAX_LOAD_CLIENTS, &client_count) == -1 ||
        parse_bounded(argv[2], 1, MAX_LOAD_SECONDS, &duration) == -1) {
        fprintf(stderr, "Usage: %s <clients 1-%d> <seconds 1-%d>\n", argv[0],
                MAX_LOAD_CLIENTS, MAX_LOAD_SECONDS);
        return 2;
    }

    memset(clients, 0, sizeof(clients));
    clock_gettime(CLOCK_MONOTONIC, &begin);
    for (index = 0; index < client_count; ++index) {
        clients[index].number = index;
        clients[index].duration_ns = (long)duration * 1000000000L;
        if (pthread_create(&threads[index], NULL, run_client,
                           &clients[index]) == 0) {
            started[index] = 1;
        } else {
            ++clients[index].errors;
        }
    }
    for (index = 0; index < client_count; ++index) {
        if (started[index]) {
            pthread_join(threads[index], NULL);
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    seconds = (double)elapsed_ns(&begin, &end) / 1e9;

    for (index = 0; index < client_count; ++index) {
        total += clients[index].count;
        errors += clients[index].errors;
    }
    all = malloc((total == 0 ? 1 : total) * sizeof(*all));
    if (all == NULL) {
        fprintf(stderr, "Out of memory\n");
        return 1;
    }
    for (index = 0; index < client_count; ++index) {
        if (clients[index].count > 0) {
            memcpy(all + used, clients[index].latencies_ns,
                   clients[index].count * sizeof(*all));
            used += clients[index].count;
        }
        free(clients[index].latencies_ns);
    }
    latency_sort(all, total);

    printf("RESULT clients=%d seconds=%d requests=%zu errors=%lu rps=%.1f "
           "mean_ms=%.3f p50_ms=%.3f p95_ms=%.3f p99_ms=%.3f max_ms=%.3f\n",
           client_count, duration, total, errors, (double)total / seconds,
           latency_mean(all, total) / 1e6,
           (double)latency_percentile(all, total, 50.0) / 1e6,
           (double)latency_percentile(all, total, 95.0) / 1e6,
           (double)latency_percentile(all, total, 99.0) / 1e6,
           (double)latency_percentile(all, total, 100.0) / 1e6);
    free(all);
    return errors == 0 ? 0 : 1;
}
