#define _POSIX_C_SOURCE 200809L

#include "constants/constants.h"
#include "models/message.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <mqueue.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
    mqd_t request_queue;
    mqd_t response_queue;
    char response_queue_name[QUEUE_NAME_SIZE];
} ClientContext;

typedef struct {
    char command[COMMAND_SIZE];
    int resource_id;
} ParsedCommand;

typedef enum {
    READ_OK,
    READ_END,
    READ_TOO_LONG,
    READ_FAILED
} ReadResult;

static volatile sig_atomic_t interrupted = 0;

static void handle_signal(int signal_number)
{
    (void)signal_number;
    interrupted = 1;
}

static bool install_signal_handlers(void)
{
    struct sigaction action;

    memset(&action, 0, sizeof(action));
    action.sa_handler = handle_signal;
    sigemptyset(&action.sa_mask);

    if (sigaction(SIGINT, &action, NULL) == -1 ||
        sigaction(SIGTERM, &action, NULL) == -1) {
        perror("sigaction");
        return false;
    }
    return true;
}

static void show_menu(int client_id)
{
    printf("================================\n");
    printf("     CINEMA RESERVATION SYSTEM\n");
    printf("================================\n");
    printf("Client ID: %d\n\n", client_id);
    printf("Commands:\n");
    printf("  LIST\n");
    printf("  STATUS <seat_id>\n");
    printf("  RESERVE <seat_id>\n");
    printf("  CANCEL <seat_id>\n");
    printf("  QUIT\n\n");
}

static ReadResult read_input(char *buffer, size_t buffer_size)
{
    int ch;

    printf("> ");
    fflush(stdout);

    errno = 0;
    if (fgets(buffer, (int)buffer_size, stdin) == NULL) {
        if (feof(stdin)) {
            return READ_END;
        }
        if (errno == EINTR && interrupted) {
            return READ_END;
        }
        return READ_FAILED;
    }

    if (strchr(buffer, '\n') == NULL && !feof(stdin)) {
        while ((ch = getchar()) != '\n' && ch != EOF) {
            /* Discard the rest of an overlong input line. */
        }
        return READ_TOO_LONG;
    }

    return READ_OK;
}

static void uppercase_ascii(char *text)
{
    while (*text != '\0') {
        *text = (char)toupper((unsigned char)*text);
        ++text;
    }
}

static bool command_needs_resource(const char *command)
{
    return strcmp(command, "STATUS") == 0 ||
           strcmp(command, "RESERVE") == 0 ||
           strcmp(command, "CANCEL") == 0;
}

static bool parse_command(char *input, ParsedCommand *parsed,
                          char *error, size_t error_size)
{
    char *save_pointer = NULL;
    char *command = strtok_r(input, " \t\r\n", &save_pointer);
    char *argument;
    char *extra;
    char *end_pointer;
    long resource_id;

    if (command == NULL) {
        snprintf(error, error_size, "Please enter a command.");
        return false;
    }
    if (strlen(command) >= sizeof(parsed->command)) {
        snprintf(error, error_size, "Command is too long.");
        return false;
    }

    snprintf(parsed->command, sizeof(parsed->command), "%s", command);
    uppercase_ascii(parsed->command);
    parsed->resource_id = 0;

    argument = strtok_r(NULL, " \t\r\n", &save_pointer);
    extra = strtok_r(NULL, " \t\r\n", &save_pointer);

    if (command_needs_resource(parsed->command)) {
        if (argument == NULL || extra != NULL) {
            snprintf(error, error_size,
                     "Usage: %s <seat_id>", parsed->command);
            return false;
        }

        errno = 0;
        end_pointer = NULL;
        resource_id = strtol(argument, &end_pointer, 10);
        if (errno == ERANGE || end_pointer == argument ||
            *end_pointer != '\0' || resource_id < INT_MIN ||
            resource_id > INT_MAX) {
            snprintf(error, error_size,
                     "Seat ID must be an integer from 1 to %d.", MAX_SEATS);
            return false;
        }
        parsed->resource_id = (int)resource_id;
        return true;
    }

    if (strcmp(parsed->command, "LIST") == 0 ||
        strcmp(parsed->command, "QUIT") == 0) {
        if (argument != NULL) {
            snprintf(error, error_size,
                     "%s does not accept an argument.", parsed->command);
            return false;
        }
        return true;
    }

    snprintf(error, error_size,
             "Unknown command. Use LIST, STATUS, RESERVE, CANCEL, or QUIT.");
    return false;
}

static bool validate_command(const ParsedCommand *parsed,
                             char *error, size_t error_size)
{
    if (command_needs_resource(parsed->command) &&
        (parsed->resource_id < 1 || parsed->resource_id > MAX_SEATS)) {
        snprintf(error, error_size,
                 "Seat ID must be between 1 and %d.", MAX_SEATS);
        return false;
    }
    return true;
}

static bool parse_client_id(const char *text, int *client_id)
{
    char *end_pointer = NULL;
    long value;

    errno = 0;
    value = strtol(text, &end_pointer, 10);
    if (errno == ERANGE || end_pointer == text || *end_pointer != '\0' ||
        value < 1 || value > MAX_CLIENT_ID) {
        return false;
    }

    *client_id = (int)value;
    return true;
}

static mqd_t open_request_queue(void)
{
    mqd_t queue = mq_open(REQUEST_QUEUE, O_WRONLY);

    if (queue == (mqd_t)-1) {
        if (errno == ENOENT) {
            fprintf(stderr,
                    "Cannot connect to server: request queue %s does not exist.\n"
                    "Start the server first, then run the client again.\n",
                    REQUEST_QUEUE);
        } else {
            fprintf(stderr, "Cannot open request queue %s: %s\n",
                    REQUEST_QUEUE, strerror(errno));
        }
    }
    return queue;
}

static mqd_t create_response_queue(char *queue_name, size_t queue_name_size,
                                   int client_id)
{
    struct mq_attr attributes;
    int written;
    mqd_t queue;

    written = snprintf(queue_name, queue_name_size, "%s%d_%ld",
                       CLIENT_QUEUE_PREFIX, client_id, (long)getpid());
    if (written < 0 || (size_t)written >= queue_name_size) {
        fprintf(stderr, "Response queue name is too long.\n");
        return (mqd_t)-1;
    }

    memset(&attributes, 0, sizeof(attributes));
    attributes.mq_maxmsg = QUEUE_MAX_MESSAGES;
    attributes.mq_msgsize = RESPONSE_MESSAGE_SIZE_BYTES;

    queue = mq_open(queue_name, O_RDONLY | O_CREAT | O_EXCL,
                    RESPONSE_QUEUE_PERMISSIONS, &attributes);
    if (queue == (mqd_t)-1 && errno == EEXIST) {
        /* A crashed process can leave a queue with this PID behind. */
        if (mq_unlink(queue_name) == -1) {
            fprintf(stderr, "Cannot remove stale response queue %s: %s\n",
                    queue_name, strerror(errno));
            return (mqd_t)-1;
        }
        queue = mq_open(queue_name, O_RDONLY | O_CREAT | O_EXCL,
                        RESPONSE_QUEUE_PERMISSIONS, &attributes);
    }

    if (queue == (mqd_t)-1) {
        fprintf(stderr, "Cannot create response queue %s: %s\n",
                queue_name, strerror(errno));
    }
    return queue;
}

static void build_request(Request *request, int client_id,
                          const ParsedCommand *parsed,
                          const char *response_queue_name)
{
    memset(request, 0, sizeof(*request));
    request->client_id = client_id;
    request->resource_id = parsed->resource_id;
    snprintf(request->command, sizeof(request->command), "%s",
             parsed->command);
    snprintf(request->response_queue, sizeof(request->response_queue), "%s",
             response_queue_name);
}

static bool send_request(mqd_t request_queue, const Request *request)
{
    if (mq_send(request_queue, (const char *)request,
                sizeof(*request), 0) == -1) {
        fprintf(stderr, "Failed to send request: %s\n", strerror(errno));
        return false;
    }
    return true;
}

static bool receive_response(mqd_t response_queue, Response *response)
{
    ssize_t received;

    do {
        received = mq_receive(response_queue, (char *)response,
                              sizeof(*response), NULL);
    } while (received == -1 && errno == EINTR && !interrupted);

    if (received == -1) {
        if (errno == EINTR && interrupted) {
            fprintf(stderr, "Interrupted while waiting for server response.\n");
        } else {
            fprintf(stderr, "Failed to receive response: %s\n",
                    strerror(errno));
        }
        return false;
    }
    if ((size_t)received != sizeof(*response)) {
        fprintf(stderr,
                "Protocol error: expected %zu response bytes, received %zd.\n",
                sizeof(*response), received);
        return false;
    }

    response->message[sizeof(response->message) - 1] = '\0';
    return true;
}

static void cleanup(ClientContext *context)
{
    if (context->request_queue != (mqd_t)-1) {
        if (mq_close(context->request_queue) == -1) {
            fprintf(stderr, "Warning: could not close request queue: %s\n",
                    strerror(errno));
        }
        context->request_queue = (mqd_t)-1;
    }

    if (context->response_queue != (mqd_t)-1) {
        if (mq_close(context->response_queue) == -1) {
            fprintf(stderr, "Warning: could not close response queue: %s\n",
                    strerror(errno));
        }
        context->response_queue = (mqd_t)-1;
    }

    if (context->response_queue_name[0] != '\0') {
        if (mq_unlink(context->response_queue_name) == -1 && errno != ENOENT) {
            fprintf(stderr, "Warning: could not remove response queue %s: %s\n",
                    context->response_queue_name, strerror(errno));
        }
        context->response_queue_name[0] = '\0';
    }
}

int main(int argc, char *argv[])
{
    ClientContext context = {
        .request_queue = (mqd_t)-1,
        .response_queue = (mqd_t)-1,
        .response_queue_name = ""
    };
    char input[INPUT_SIZE];
    char error[160];
    ParsedCommand parsed;
    Request request;
    Response response;
    ReadResult read_result;
    int client_id;
    int exit_status = EXIT_SUCCESS;

    if (argc != 2 || !parse_client_id(argv[1], &client_id)) {
        fprintf(stderr, "Usage: %s <client_id>\n", argv[0]);
        fprintf(stderr, "client_id must be an integer from 1 to %d.\n",
                MAX_CLIENT_ID);
        return EXIT_FAILURE;
    }

    if (!install_signal_handlers()) {
        return EXIT_FAILURE;
    }

    context.request_queue = open_request_queue();
    if (context.request_queue == (mqd_t)-1) {
        return EXIT_FAILURE;
    }

    context.response_queue = create_response_queue(
        context.response_queue_name, sizeof(context.response_queue_name),
        client_id);
    if (context.response_queue == (mqd_t)-1) {
        cleanup(&context);
        return EXIT_FAILURE;
    }

    show_menu(client_id);

    while (!interrupted) {
        read_result = read_input(input, sizeof(input));
        if (read_result == READ_END) {
            printf("\nClosing client.\n");
            break;
        }
        if (read_result == READ_TOO_LONG) {
            fprintf(stderr, "Input is too long (maximum %d characters).\n",
                    INPUT_SIZE - 2);
            continue;
        }
        if (read_result == READ_FAILED) {
            fprintf(stderr, "Failed to read input: %s\n", strerror(errno));
            exit_status = EXIT_FAILURE;
            break;
        }

        if (!parse_command(input, &parsed, error, sizeof(error)) ||
            !validate_command(&parsed, error, sizeof(error))) {
            fprintf(stderr, "Invalid input: %s\n\n", error);
            continue;
        }

        build_request(&request, client_id, &parsed,
                      context.response_queue_name);
        printf("Sending request...\n");
        if (!send_request(context.request_queue, &request)) {
            exit_status = EXIT_FAILURE;
            break;
        }
        if (!receive_response(context.response_queue, &response)) {
            exit_status = interrupted ? EXIT_SUCCESS : EXIT_FAILURE;
            break;
        }

        printf("%s: %s\n\n",
               response.success == RESPONSE_SUCCESS ? "SUCCESS" : "FAILED",
               response.message);

        if (strcmp(parsed.command, "QUIT") == 0) {
            break;
        }
    }

    cleanup(&context);
    return exit_status;
}
