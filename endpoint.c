#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#include "endpoint.h"

struct timeval gettime() {
    struct timeval tv;
    if (gettimeofday(&tv, NULL) < 0) {
        perror("gettimeofday() error");
        exit(EXIT_FAILURE);
    }
    return tv;
}

parse_error_t endpoint_parse_address(endpoint_t *self, char *addr) {
    if (strlen(addr) > sizeof(self->host)) {
        return PARSE_ERROR_HOST_IS_TOO_LONG;
    }
    int res = sscanf(addr, "%[^:]:%u/%u", (char *)&self->host, &self->port, &self->timeout);
    if (res == 2 || res == 3) {
        return PARSE_SUCCESS;
    }
    return PARSE_ERROR_INVALID_ADDRESS;
}

int endpoint_getaddrinfo(endpoint_t *self) {
    struct addrinfo hints = {0}, *ai;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(self->host, NULL, &hints, &ai) < 0) {
        return -1;
    }
    self->sa = *(struct sockaddr_in *)ai->ai_addr;
    self->sa.sin_port = htons(self->port);
    freeaddrinfo(ai);
    return 0;
}

int endpoint_create_socket(endpoint_t *self) {
    self->fd = socket(AF_INET, SOCK_STREAM, 0);
    if (self->fd < 0) {
        return -1;
    }
    if (fcntl(self->fd, F_SETFL, O_NONBLOCK) < 0) {
        return -1;
    }
    return 0;
}

void endpoint_set_deadline(endpoint_t *self) {
    if (self->timeout > 0) {
        const struct timeval now = gettime();
        self->deadline.tv_sec = now.tv_sec + self->timeout;
        self->deadline.tv_usec = now.tv_usec;
    }
}

int endpoint_connect(endpoint_t *self) {
    return connect(self->fd, (struct sockaddr *)&self->sa, sizeof(self->sa));
}

bool endpoint_is_expired(endpoint_t *self) {
    if (self->deadline.tv_sec > 0) {
        const struct timeval now = gettime();
        return now.tv_sec >= self->deadline.tv_sec && now.tv_usec >= self->deadline.tv_usec;
    }
    return false;
}

bool endpoint_is_connected(endpoint_t *self) {
    struct sockaddr addr;
    socklen_t len;
    return getpeername(self->fd, &addr, &len) >= 0;
}

void endpoint_close(endpoint_t *self) {
    close(self->fd);
}
