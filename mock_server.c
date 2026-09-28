#define _POSIX_C_SOURCE 200809L

#include "common.h"

#include <errno.h>
#include <fcntl.h>
#include <mqueue.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

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
    return sigaction(SIGINT, &action, NULL) != -1 &&
           sigaction(SIGTERM, &action, NULL) != -1;
}

static void set_response(Response *response, int success, const char *message)
{
    memset(response, 0, sizeof(*response));
    response->success = success;
    snprintf(response->message, sizeof(response->message), "%s", message);
}

static void handle_list(const int owners[], Response *response)
{
    size_t used = 0;
    int seat;

    set_response(response, RESPONSE_SUCCESS,
                 "Seats (A=available, R=reserved): ");
    used = strlen(response->message);
    for (seat = 1; seat <= MAX_SEATS; ++seat) {
        int written = snprintf(response->message + used,
                               sizeof(response->message) - used,
                               "%d:%c%s", seat,
                               owners[seat] == 0 ? 'A' : 'R',
                               seat == MAX_SEATS ? "" : " ");
        if (written < 0 || (size_t)written >=
                sizeof(response->message) - used) {
            set_response(response, RESPONSE_FAILED,
                         "LIST response does not fit in the message buffer.");
            return;
        }
        used += (size_t)written;
    }
}

static void process_request(const Request *request, int owners[],
                            Response *response)
{
    char message[RESPONSE_MESSAGE_SIZE];
    int seat = request->resource_id;

    if (strcmp(request->command, "LIST") == 0) {
        handle_list(owners, response);
        return;
    }
    if (strcmp(request->command, "QUIT") == 0) {
        set_response(response, RESPONSE_SUCCESS, "Client session closed.");
        return;
    }
    if (seat < 1 || seat > MAX_SEATS) {
        set_response(response, RESPONSE_FAILED, "Invalid seat ID.");
        return;
    }

    if (strcmp(request->command, "STATUS") == 0) {
        if (owners[seat] == 0) {
            snprintf(message, sizeof(message), "Seat %d is available.", seat);
        } else {
            snprintf(message, sizeof(message),
                     "Seat %d is reserved by Client %d.",
                     seat, owners[seat]);
        }
        set_response(response, RESPONSE_SUCCESS, message);
    } else if (strcmp(request->command, "RESERVE") == 0) {
        if (owners[seat] != 0) {
            snprintf(message, sizeof(message),
                     "Seat %d is already reserved.", seat);
            set_response(response, RESPONSE_FAILED, message);
        } else {
            owners[seat] = request->client_id;
            snprintf(message, sizeof(message),
                     "Seat %d reserved successfully.", seat);
            set_response(response, RESPONSE_SUCCESS, message);
        }
    } else if (strcmp(request->command, "CANCEL") == 0) {
        if (owners[seat] == 0) {
            snprintf(message, sizeof(message),
                     "Seat %d is not reserved.", seat);
            set_response(response, RESPONSE_FAILED, message);
        } else if (owners[seat] != request->client_id) {
            snprintf(message, sizeof(message),
                     "Seat %d belongs to another client.", seat);
            set_response(response, RESPONSE_FAILED, message);
        } else {
            owners[seat] = 0;
            snprintf(message, sizeof(message),
                     "Seat %d reservation cancelled.", seat);
            set_response(response, RESPONSE_SUCCESS, message);
        }
    } else {
        set_response(response, RESPONSE_FAILED, "Unsupported command.");
    }
}

static bool send_response(const Request *request, const Response *response)
{
    mqd_t response_queue = mq_open(request->response_queue, O_WRONLY);

    if (response_queue == (mqd_t)-1) {
        fprintf(stderr, "Cannot open %s for Client %d: %s\n",
                request->response_queue, request->client_id, strerror(errno));
        return false;
    }
    if (mq_send(response_queue, (const char *)response,
                sizeof(*response), 0) == -1) {
        fprintf(stderr, "Cannot respond to Client %d: %s\n",
                request->client_id, strerror(errno));
        mq_close(response_queue);
        return false;
    }
    mq_close(response_queue);
    return true;
}

int main(void)
{
    struct mq_attr attributes;
    mqd_t request_queue;
    Request request;
    Response response;
    int owners[MAX_SEATS + 1] = {0};

    if (!install_signal_handlers()) {
        perror("sigaction");
        return EXIT_FAILURE;
    }

    memset(&attributes, 0, sizeof(attributes));
    attributes.mq_maxmsg = QUEUE_MAX_MESSAGES;
    attributes.mq_msgsize = REQUEST_MESSAGE_SIZE;

    /* This test server owns the request queue and removes stale test state. */
    if (mq_unlink(REQUEST_QUEUE) == -1 && errno != ENOENT) {
        fprintf(stderr, "Cannot remove stale request queue: %s\n",
                strerror(errno));
        return EXIT_FAILURE;
    }
    request_queue = mq_open(REQUEST_QUEUE, O_RDONLY | O_CREAT | O_EXCL,
                            REQUEST_QUEUE_PERMISSIONS, &attributes);
    if (request_queue == (mqd_t)-1) {
        fprintf(stderr, "Cannot create request queue %s: %s\n",
                REQUEST_QUEUE, strerror(errno));
        return EXIT_FAILURE;
    }

    printf("Mock server is ready on %s. Press Ctrl+C to stop.\n",
           REQUEST_QUEUE);

    while (running) {
        ssize_t received = mq_receive(request_queue, (char *)&request,
                                      sizeof(request), NULL);
        if (received == -1) {
            if (errno == EINTR) {
                continue;
            }
            fprintf(stderr, "mq_receive failed: %s\n", strerror(errno));
            break;
        }
        if ((size_t)received != sizeof(request)) {
            fprintf(stderr, "Ignored request with unexpected size %zd.\n",
                    received);
            continue;
        }

        request.command[sizeof(request.command) - 1] = '\0';
        request.response_queue[sizeof(request.response_queue) - 1] = '\0';
        printf("[Mock] Client-%d %s %d -> %s\n",
               request.client_id, request.command, request.resource_id,
               request.response_queue);
        process_request(&request, owners, &response);
        send_response(&request, &response);
    }

    mq_close(request_queue);
    mq_unlink(REQUEST_QUEUE);
    printf("Mock server stopped.\n");
    return EXIT_SUCCESS;
}
