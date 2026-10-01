#ifndef CINEMA_MESSAGE_H
#define CINEMA_MESSAGE_H

#include "constants/constants.h"

typedef struct {
    int client_id;
    char command[COMMAND_SIZE];
    int resource_id;
    char response_queue[QUEUE_NAME_SIZE];
} Request;

typedef struct {
    int success;
    char message[RESPONSE_MESSAGE_SIZE];
} Response;

#define REQUEST_MESSAGE_SIZE ((long)sizeof(Request))
#define RESPONSE_MESSAGE_SIZE_BYTES ((long)sizeof(Response))

#endif
