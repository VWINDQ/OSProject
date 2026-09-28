#ifndef CINEMA_COMMON_H
#define CINEMA_COMMON_H

/* POSIX message queue names must start with '/'. */
#define REQUEST_QUEUE "/cinema_request"
#define CLIENT_QUEUE_PREFIX "/cinema_client_"

#define MAX_SEATS 20
#define MAX_CLIENT_ID 999999

#define COMMAND_SIZE 20
#define QUEUE_NAME_SIZE 64
#define RESPONSE_MESSAGE_SIZE 512
#define INPUT_SIZE 256

#define QUEUE_MAX_MESSAGES 10
#define REQUEST_QUEUE_PERMISSIONS 0660
#define RESPONSE_QUEUE_PERMISSIONS 0600

#define RESPONSE_FAILED 0
#define RESPONSE_SUCCESS 1

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
