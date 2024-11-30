#pragma once

#define _POSIX_C_SOURCE 199309L
#include <time.h>
#include <netdb.h>
#include <stdbool.h>

struct timespec monotime();

typedef struct endpoint_t {
    char host[255];
    unsigned int port;
    unsigned int timeout;
    int fd;
    struct sockaddr_in sa;
    struct timespec deadline;
    bool in_progress;
    bool is_connected;
    bool is_failed;
} endpoint_t;

int endpoint_parse_address(endpoint_t *, char *);

int endpoint_getaddrinfo(endpoint_t *);

int endpoint_create_socket(endpoint_t *);

void endpoint_set_deadline(endpoint_t *);

int endpoint_connect(endpoint_t *);

void endpoint_close(endpoint_t *);