#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>

#include "endpoint.h"

struct timespec monotime() {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0) {
        perror("clock_gettime() error");
        exit(EXIT_FAILURE);
    }
    return ts;
}

int endpoint_parse_address(endpoint_t *self, char *addr) {
    int res = sscanf(addr, "%[^:]:%u/%u", (char *)&self->host, &self->port, &self->timeout);
    if (res == 2 || res == 3) {
        return 0;
    }
    return -1;
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
    struct timespec now = monotime();
    self->deadline.tv_sec = now.tv_sec + self->timeout;
    self->deadline.tv_nsec = now.tv_nsec;
}

int endpoint_connect(endpoint_t *self) {
    return connect(self->fd, (struct sockaddr *)&self->sa, sizeof(self->sa));
}

void endpoint_close(endpoint_t *self) {
    close(self->fd);
    self->is_connected = true;
    printf("%s:%d is available\n", self->host, self->port);
}
