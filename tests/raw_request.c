#define _POSIX_C_SOURCE 200809L

/*
 * Test helper: sends a hand-made Request straight to the server so that input
 * the real client never produces (seat 99, client 0, bad reply-queue name,
 * truncated message) can be tested.
 *
 *   raw_request <client_id> <command> <resource_id> <response_queue> [--no-reply | --short]
 *     (default)   create <response_queue>, send a full Request, print the reply
 *     --no-reply  send without creating the response queue
 *     --short     send only 10 bytes instead of a full Request
 */

#include "constants/constants.h"
#include "models/message.h"

#include <errno.h>
#include <fcntl.h>
#include <mqueue.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define REPLY_TIMEOUT_SECONDS 2
#define SHORT_MESSAGE_BYTES 10

static int parse_int(const char *text, int *value)
{
    char *end = NULL;
    long parsed;

    errno = 0;
    parsed = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || parsed < -2000000000L ||
        parsed > 2000000000L) {
        return 0;
    }
    *value = (int)parsed;
    return 1;
}

int main(int argc, char *argv[])
{
    Request request;
    Response response;
    struct mq_attr attributes;
    struct timespec deadline;
    mqd_t request_queue;
    mqd_t reply_queue = (mqd_t)-1;
    int client_id;
    int resource_id;
    int want_reply = 1;
    size_t send_size = sizeof(request);
    ssize_t received;

    if (argc < 5 || argc > 6 || !parse_int(argv[1], &client_id) ||
        !parse_int(argv[3], &resource_id)) {
        fprintf(stderr,
                "Usage: %s <client_id> <command> <resource_id> <response_queue> "
                "[--no-reply | --short]\n",
                argv[0]);
        return 1;
    }
    if (argc == 6) {
        if (strcmp(argv[5], "--no-reply") == 0) {
            want_reply = 0;
        } else if (strcmp(argv[5], "--short") == 0) {
            want_reply = 0;
            send_size = SHORT_MESSAGE_BYTES;
        } else {
            fprintf(stderr, "Unknown option %s\n", argv[5]);
            return 1;
        }
    }

    memset(&request, 0, sizeof(request));
    request.client_id = client_id;
    request.resource_id = resource_id;
    snprintf(request.command, sizeof(request.command), "%s", argv[2]);
    snprintf(request.response_queue, sizeof(request.response_queue), "%s", argv[4]);

    if (want_reply) {
        memset(&attributes, 0, sizeof(attributes));
        attributes.mq_maxmsg = QUEUE_MAX_MESSAGES;
        attributes.mq_msgsize = RESPONSE_MESSAGE_SIZE_BYTES;
        mq_unlink(argv[4]);
        reply_queue = mq_open(argv[4], O_RDONLY | O_CREAT | O_EXCL,
                              RESPONSE_QUEUE_PERMISSIONS, &attributes);
        if (reply_queue == (mqd_t)-1) {
            fprintf(stderr, "Cannot create %s: %s\n", argv[4], strerror(errno));
            return 1;
        }
    }

    request_queue = mq_open(REQUEST_QUEUE, O_WRONLY);
    if (request_queue == (mqd_t)-1) {
        fprintf(stderr, "Cannot open %s: %s\n", REQUEST_QUEUE, strerror(errno));
        if (want_reply) {
            mq_close(reply_queue);
            mq_unlink(argv[4]);
        }
        return 1;
    }
    if (mq_send(request_queue, (const char *)&request, send_size, 0) == -1) {
        fprintf(stderr, "Cannot send: %s\n", strerror(errno));
        mq_close(request_queue);
        if (want_reply) {
            mq_close(reply_queue);
            mq_unlink(argv[4]);
        }
        return 1;
    }
    mq_close(request_queue);

    if (!want_reply) {
        printf("SENT\n");
        return 0;
    }

    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += REPLY_TIMEOUT_SECONDS;
    received = mq_timedreceive(reply_queue, (char *)&response, sizeof(response),
                               NULL, &deadline);
    if (received == (ssize_t)sizeof(response)) {
        response.message[sizeof(response.message) - 1] = '\0';
        printf("%s: %s\n", response.success == RESPONSE_SUCCESS ? "SUCCESS" : "FAILED",
               response.message);
    } else {
        printf("NO RESPONSE\n");
    }
    mq_close(reply_queue);
    mq_unlink(argv[4]);
    return 0;
}
