#define _POSIX_C_SOURCE 200809L

#include "constants/constants.h"
#include "models/message.h"
#include "reservation/reservation.h"
#include "utils/logger.h"
#include "utils/server_lock.h"

#include <errno.h>
#include <fcntl.h>
#include <mqueue.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define DEFAULT_WORKERS 3
#define MAX_WORKERS 32
#define DEFAULT_DELAY_MIN_MS 50
#define DEFAULT_DELAY_MAX_MS 500
#define MAX_DELAY_MS 10000
#define WORK_QUEUE_CAPACITY 64
#define RECEIVE_TIMEOUT_MS 200
#define ACTOR_SIZE 32

typedef struct {
    SyncMode mode;
    int workers;
    int delay_min_ms;
    int delay_max_ms;
} ServerConfig;

/* Receiver-to-Worker hand-over; its mutex guards only this queue, not the seat table. */
typedef struct {
    Request items[WORK_QUEUE_CAPACITY];
    size_t head;
    size_t count;
    bool closed;
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
} WorkQueue;

typedef struct {
    int id;
    pthread_t thread;
    WorkQueue *queue;
    unsigned long processed; /* written only by its own thread, read after join */
} Worker;

static volatile sig_atomic_t running = 1;

static void stop_server(int signal_number)
{
    (void)signal_number;
    running = 0;
}

static bool install_signal_handlers(void)
{
    struct sigaction action;

    memset(&action, 0, sizeof(action));
    action.sa_handler = stop_server;
    sigemptyset(&action.sa_mask);
    /* No SA_RESTART, so blocking calls return EINTR. */
    if (sigaction(SIGINT, &action, NULL) == -1 ||
        sigaction(SIGTERM, &action, NULL) == -1) {
        perror("sigaction");
        return false;
    }
    return true;
}

static void print_usage(const char *program)
{
    fprintf(stderr,
            "Usage: %s [sync|nosync] [workers] [delay_min_ms delay_max_ms]\n"
            "  sync|nosync  protect the seat table with mutexes (default: sync)\n"
            "  workers      number of worker threads, 1-%d (default: %d)\n"
            "  delay_*_ms   random delay between check and update of RESERVE,\n"
            "               0 <= min <= max <= %d (default: %d %d)\n",
            program, MAX_WORKERS, DEFAULT_WORKERS, MAX_DELAY_MS,
            DEFAULT_DELAY_MIN_MS, DEFAULT_DELAY_MAX_MS);
}

static bool parse_int_argument(const char *text, int minimum, int maximum,
                               int *value)
{
    char *end = NULL;
    long parsed;

    errno = 0;
    parsed = strtol(text, &end, 10);
    if (errno == ERANGE || end == text || *end != '\0' || parsed < minimum ||
        parsed > maximum) {
        return false;
    }
    *value = (int)parsed;
    return true;
}

static bool parse_arguments(int argc, char *argv[], ServerConfig *config)
{
    int index = 1;

    config->mode = SYNC_MODE_SYNC;
    config->workers = DEFAULT_WORKERS;
    config->delay_min_ms = DEFAULT_DELAY_MIN_MS;
    config->delay_max_ms = DEFAULT_DELAY_MAX_MS;

    if (index < argc && strcmp(argv[index], "sync") == 0) {
        config->mode = SYNC_MODE_SYNC;
        ++index;
    } else if (index < argc && strcmp(argv[index], "nosync") == 0) {
        config->mode = SYNC_MODE_NOSYNC;
        ++index;
    }
    if (index < argc) {
        if (!parse_int_argument(argv[index], 1, MAX_WORKERS, &config->workers)) {
            return false;
        }
        ++index;
    }
    if (index < argc) {
        if (index + 1 >= argc ||
            !parse_int_argument(argv[index], 0, MAX_DELAY_MS,
                                &config->delay_min_ms) ||
            !parse_int_argument(argv[index + 1], 0, MAX_DELAY_MS,
                                &config->delay_max_ms)) {
            return false;
        }
        index += 2;
    }
    return index == argc && config->delay_min_ms <= config->delay_max_ms;
}

static bool work_queue_init(WorkQueue *queue)
{
    memset(queue, 0, sizeof(*queue));
    if (pthread_mutex_init(&queue->mutex, NULL) != 0) {
        return false;
    }
    if (pthread_cond_init(&queue->not_empty, NULL) != 0) {
        pthread_mutex_destroy(&queue->mutex);
        return false;
    }
    if (pthread_cond_init(&queue->not_full, NULL) != 0) {
        pthread_cond_destroy(&queue->not_empty);
        pthread_mutex_destroy(&queue->mutex);
        return false;
    }
    return true;
}

static void work_queue_destroy(WorkQueue *queue)
{
    pthread_cond_destroy(&queue->not_full);
    pthread_cond_destroy(&queue->not_empty);
    pthread_mutex_destroy(&queue->mutex);
}

static bool work_queue_push(WorkQueue *queue, const Request *request)
{
    bool pushed = false;

    pthread_mutex_lock(&queue->mutex);
    while (queue->count == WORK_QUEUE_CAPACITY && !queue->closed) {
        pthread_cond_wait(&queue->not_full, &queue->mutex);
    }
    if (!queue->closed) {
        queue->items[(queue->head + queue->count) % WORK_QUEUE_CAPACITY] = *request;
        ++queue->count;
        pushed = true;
        pthread_cond_signal(&queue->not_empty);
    }
    pthread_mutex_unlock(&queue->mutex);
    return pushed;
}

/* Returns false only once the queue is closed and drained. */
static bool work_queue_pop(WorkQueue *queue, Request *request)
{
    bool popped = false;

    pthread_mutex_lock(&queue->mutex);
    while (queue->count == 0 && !queue->closed) {
        pthread_cond_wait(&queue->not_empty, &queue->mutex);
    }
    if (queue->count > 0) {
        *request = queue->items[queue->head];
        queue->head = (queue->head + 1) % WORK_QUEUE_CAPACITY;
        --queue->count;
        popped = true;
        pthread_cond_signal(&queue->not_full);
    }
    pthread_mutex_unlock(&queue->mutex);
    return popped;
}

static void work_queue_close(WorkQueue *queue)
{
    pthread_mutex_lock(&queue->mutex);
    queue->closed = true;
    pthread_cond_broadcast(&queue->not_empty);
    pthread_cond_broadcast(&queue->not_full);
    pthread_mutex_unlock(&queue->mutex);
}

static bool command_needs_seat(const char *command)
{
    return strcmp(command, "STATUS") == 0 || strcmp(command, "RESERVE") == 0 ||
           strcmp(command, "CANCEL") == 0;
}

static bool is_known_command(const char *command)
{
    return command_needs_seat(command) || strcmp(command, "LIST") == 0 ||
           strcmp(command, "QUIT") == 0;
}

/* Only /cinema_client_ + digits/underscores, so a forged name cannot target another queue. */
static bool is_valid_response_queue(const char *name)
{
    size_t prefix_length = strlen(CLIENT_QUEUE_PREFIX);
    const char *suffix;

    if (strncmp(name, CLIENT_QUEUE_PREFIX, prefix_length) != 0) {
        return false;
    }
    suffix = name + prefix_length;
    return suffix[0] != '\0' && strspn(suffix, "0123456789_") == strlen(suffix);
}

/* NULL when the request is acceptable, otherwise the reason it is not. */
static const char *validate_request(const Request *request)
{
    if (!is_known_command(request->command)) {
        return "Unsupported command.";
    }
    if (request->client_id < 1 || request->client_id > MAX_CLIENT_ID) {
        return "Invalid client ID.";
    }
    if (command_needs_seat(request->command) &&
        (request->resource_id < 1 || request->resource_id > MAX_SEATS)) {
        return "Invalid seat ID.";
    }
    return NULL;
}

static void send_reply(const char *actor, const Request *request,
                       const Response *response)
{
    mqd_t queue = mq_open(request->response_queue, O_WRONLY | O_NONBLOCK);

    if (queue == (mqd_t)-1) {
        log_event(actor, "cannot reply to Client-%d (%s): %s", request->client_id,
                  request->response_queue, strerror(errno));
        return;
    }
    if (mq_send(queue, (const char *)response, sizeof(*response), 0) == -1) {
        log_event(actor, "cannot reply to Client-%d: %s", request->client_id,
                  strerror(errno));
    } else {
        log_event(actor, "replied %s to Client-%d",
                  response->success == RESPONSE_SUCCESS ? "SUCCESS" : "FAILED",
                  request->client_id);
    }
    if (mq_close(queue) == -1) {
        log_event(actor, "cannot close reply queue of Client-%d: %s",
                  request->client_id, strerror(errno));
    }
}

static void execute_request(int worker_id, const Request *request,
                            Response *response)
{
    const char *command = request->command;

    if (strcmp(command, "LIST") == 0) {
        reservation_list(worker_id, response);
    } else if (strcmp(command, "STATUS") == 0) {
        reservation_status(worker_id, request->resource_id, response);
    } else if (strcmp(command, "RESERVE") == 0) {
        reservation_reserve(worker_id, request->client_id, request->resource_id,
                            response);
    } else if (strcmp(command, "CANCEL") == 0) {
        reservation_cancel(worker_id, request->client_id, request->resource_id,
                           response);
    } else if (strcmp(command, "QUIT") == 0) {
        response_set(response, RESPONSE_SUCCESS, "Client session closed.");
    } else {
        response_set(response, RESPONSE_FAILED, "Unsupported command.");
    }
}

static void *worker_main(void *argument)
{
    Worker *worker = argument;
    char actor[ACTOR_SIZE];
    Request request;
    Response response;

    snprintf(actor, sizeof(actor), "Worker-%d", worker->id);
    while (work_queue_pop(worker->queue, &request)) {
        if (command_needs_seat(request.command)) {
            log_event(actor, "received %s %d from Client-%d", request.command,
                      request.resource_id, request.client_id);
        } else {
            log_event(actor, "received %s from Client-%d", request.command,
                      request.client_id);
        }
        execute_request(worker->id, &request, &response);
        send_reply(actor, &request, &response);
        ++worker->processed;
    }
    return NULL;
}

/* Block SIGINT/SIGTERM while creating workers so only the main thread handles them. */
static int start_workers(Worker workers[], int count, WorkQueue *queue)
{
    sigset_t blocked;
    sigset_t previous;
    int started = 0;
    int error;

    sigemptyset(&blocked);
    sigaddset(&blocked, SIGINT);
    sigaddset(&blocked, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &blocked, &previous);

    while (started < count) {
        workers[started].id = started + 1;
        workers[started].queue = queue;
        workers[started].processed = 0;
        error = pthread_create(&workers[started].thread, NULL, worker_main,
                               &workers[started]);
        if (error != 0) {
            fprintf(stderr, "pthread_create failed: %s\n", strerror(error));
            break;
        }
        ++started;
    }

    pthread_sigmask(SIG_SETMASK, &previous, NULL);
    return started;
}

static void stop_workers(Worker workers[], int started, WorkQueue *queue)
{
    int index;

    work_queue_close(queue);
    for (index = 0; index < started; ++index) {
        pthread_join(workers[index].thread, NULL);
    }
}

static mqd_t create_request_queue(void)
{
    struct mq_attr attributes;
    mqd_t queue;

    memset(&attributes, 0, sizeof(attributes));
    attributes.mq_maxmsg = QUEUE_MAX_MESSAGES;
    attributes.mq_msgsize = REQUEST_MESSAGE_SIZE;

    /* A crashed run may have left its queue behind. */
    if (mq_unlink(REQUEST_QUEUE) == -1 && errno != ENOENT) {
        fprintf(stderr, "Cannot remove stale request queue %s: %s\n",
                REQUEST_QUEUE, strerror(errno));
        return (mqd_t)-1;
    }
    queue = mq_open(REQUEST_QUEUE, O_RDONLY | O_CREAT | O_EXCL,
                    REQUEST_QUEUE_PERMISSIONS, &attributes);
    if (queue == (mqd_t)-1) {
        fprintf(stderr, "Cannot create request queue %s: %s\n", REQUEST_QUEUE,
                strerror(errno));
    }
    return queue;
}

/* Timed receive, so a signal arriving just before the call is still noticed. */
static bool receive_requests(mqd_t request_queue, WorkQueue *work_queue)
{
    Request request;
    Response response;
    struct timespec deadline;
    ssize_t received;
    const char *reason;

    while (running) {
        clock_gettime(CLOCK_REALTIME, &deadline);
        deadline.tv_nsec += RECEIVE_TIMEOUT_MS * 1000000L;
        if (deadline.tv_nsec >= 1000000000L) {
            deadline.tv_sec += 1;
            deadline.tv_nsec -= 1000000000L;
        }

        received = mq_timedreceive(request_queue, (char *)&request,
                                   sizeof(request), NULL, &deadline);
        if (received == -1) {
            if (errno == ETIMEDOUT || errno == EINTR) {
                continue;
            }
            log_event("Receiver", "mq_receive failed: %s", strerror(errno));
            return false;
        }
        if ((size_t)received != sizeof(request)) {
            log_event("Receiver",
                      "dropped request with unexpected size %zd (expected %zu)",
                      received, sizeof(request));
            continue;
        }

        request.command[sizeof(request.command) - 1] = '\0';
        request.response_queue[sizeof(request.response_queue) - 1] = '\0';

        if (!is_valid_response_queue(request.response_queue)) {
            log_event("Receiver",
                      "dropped request from Client-%d: invalid response queue name",
                      request.client_id);
            continue;
        }
        reason = validate_request(&request);
        if (reason != NULL) {
            log_event("Receiver", "rejected %s from Client-%d: %s", request.command,
                      request.client_id, reason);
            response_set(&response, RESPONSE_FAILED, "%s", reason);
            send_reply("Receiver", &request, &response);
            continue;
        }
        if (!work_queue_push(work_queue, &request)) {
            break;
        }
    }
    return true;
}

int main(int argc, char *argv[])
{
    ServerConfig config;
    WorkQueue work_queue;
    Worker workers[MAX_WORKERS];
    mqd_t request_queue;
    int started;
    int index;
    int exit_status = EXIT_SUCCESS;

    if (!parse_arguments(argc, argv, &config)) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }
    if (!install_signal_handlers()) {
        return EXIT_FAILURE;
    }
    if (server_lock_acquire() == -1) {
        return EXIT_FAILURE;
    }

    logger_init(stdout);
    if (reservation_init(config.mode, config.delay_min_ms, config.delay_max_ms) == -1) {
        fprintf(stderr, "Cannot initialise the reservation table.\n");
        return EXIT_FAILURE;
    }
    if (!work_queue_init(&work_queue)) {
        fprintf(stderr, "Cannot initialise the work queue.\n");
        reservation_destroy();
        return EXIT_FAILURE;
    }
    request_queue = create_request_queue();
    if (request_queue == (mqd_t)-1) {
        work_queue_destroy(&work_queue);
        reservation_destroy();
        return EXIT_FAILURE;
    }

    started = start_workers(workers, config.workers, &work_queue);
    if (started < config.workers) {
        exit_status = EXIT_FAILURE;
    } else {
        log_event("Server", "started: mode=%s workers=%d delay=%d-%d ms queue=%s",
                  config.mode == SYNC_MODE_SYNC ? "sync" : "nosync", config.workers,
                  config.delay_min_ms, config.delay_max_ms, REQUEST_QUEUE);
        if (!receive_requests(request_queue, &work_queue)) {
            exit_status = EXIT_FAILURE;
        }
    }

    log_event("Server", "shutting down");
    stop_workers(workers, started, &work_queue);
    for (index = 0; index < started; ++index) {
        log_event("Server", "Worker-%d processed %lu requests", workers[index].id,
                  workers[index].processed);
    }
    reservation_print_table(stdout);

    mq_close(request_queue);
    mq_unlink(REQUEST_QUEUE);
    work_queue_destroy(&work_queue);
    reservation_destroy();
    log_event("Server", "stopped");
    return exit_status;
}
