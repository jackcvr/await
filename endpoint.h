#pragma once

#include <sys/time.h>
#include <netdb.h>
#include <stdbool.h>

struct timeval gettime();

typedef enum {
    PARSE_SUCCESS = 0,
    PARSE_ERROR_HOST_IS_TOO_LONG,
    PARSE_ERROR_INVALID_ADDRESS,
} parse_error_t;

typedef struct endpoint_t {
    char host[255];
    unsigned int port;
    unsigned int timeout;
    int fd;
    struct sockaddr_in sa;
    struct timeval deadline;
    bool in_progress;
    bool is_connected;
    bool is_failed;
} endpoint_t;

parse_error_t endpoint_parse_address(endpoint_t *, char *);
int endpoint_getaddrinfo(endpoint_t *);
int endpoint_create_socket(endpoint_t *);
void endpoint_set_deadline(endpoint_t *);
int endpoint_connect(endpoint_t *);
bool endpoint_is_expired(endpoint_t *);
bool endpoint_is_connected(endpoint_t *);
void endpoint_close(endpoint_t *);
