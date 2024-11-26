#include <fcntl.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#define _POSIX_C_SOURCE 199309L
#include <time.h>
#include <stdbool.h>
#include <string.h>
#include <stdarg.h>

#ifndef CONNECTION_TIME_MS
#define CONNECTION_TIME_MS 25
#endif

#ifndef INTERVAL_MS
#define INTERVAL_MS 1000 // 1s
#endif

#if INTERVAL_MS < CONNECTION_TIME_MS
#error INTERVAL_MS cannot be less than CONNECTION_TIME_MS
#endif

const unsigned int INTERVAL_MS_ = INTERVAL_MS - CONNECTION_TIME_MS;
const struct timespec INTERVAL = {
    .tv_sec = INTERVAL_MS_ / 1000,
    .tv_nsec = INTERVAL_MS_ % 1000 * 1000000,
};

typedef struct {
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

void perrorf(const char *format, ...) {
    va_list args;
    va_start(args, format);
    int status = 0;
    status = vfprintf(stderr, format, args);
    if (status >= 0) {
        status = fprintf(stderr, ": %s\n", strerror(errno));
    }
    va_end(args);
    if (status < 0) {
        perror("perrorf error");
        exit(EXIT_FAILURE);
    }
}

int endpoint_parse_address(endpoint_t *ep, char *addr) {
    int res = sscanf(addr, "%[^:]:%d/%d", (char *)&ep->host, &ep->port, &ep->timeout);
    if (res == 2 || res == 3) {
        return 0;
    }
    return -1;
}

void endpoint_close(endpoint_t *ep) {
    close(ep->fd);
    ep->is_connected = true;
    printf("%s:%d is available\n", ep->host, ep->port);
}

int sockaddr_in_getaddrinfo(struct sockaddr_in *sa, const char *host, const unsigned int port) {
    struct addrinfo hints = {0}, *ai;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, NULL, &hints, &ai) < 0) {
        return -1;
    }
    *sa = *(struct sockaddr_in *)ai->ai_addr;
    sa->sin_port = htons(port);
    freeaddrinfo(ai);
    return 0;
}

int fd_set_socket(int *fd_p) {
    *fd_p = socket(AF_INET, SOCK_STREAM, 0);
    if (*fd_p < 0) {
        return -1;
    }
    if (fcntl(*fd_p, F_SETFL, O_NONBLOCK) < 0) {
        return -1;
    }
    return 0;
}

void timespec_monotime(struct timespec *ts) {
    if (clock_gettime(CLOCK_MONOTONIC, ts) < 0) {
        perror("clock_gettime() error");
        exit(EXIT_FAILURE);
    }
}

int main(int argc, char *argv[]) {
    int exit_code = EXIT_SUCCESS;
#define RETURN(code) exit_code = code; goto exit

    // count out endpoints
    int ep_count = 0;
    for (ep_count = 0; ep_count < argc - 1; ++ep_count) {
        if (strcmp(argv[ep_count + 1], "--") == 0) {
            break;
        }
    }
    if (ep_count == 0) {
        fprintf(stderr,
            "Usage: %s <host>:<port>[/<timeout>] [<host>:<port>[/<timeout>] ...] [-- <command>]\n",
            argv[0]);
        return EXIT_FAILURE;
    }

    endpoint_t *endpoints = calloc(ep_count, sizeof(endpoint_t));
    struct timespec ts;

    // setup endpoints
    for (int i = 0; i < ep_count; ++i) {
        endpoint_t *ep = &endpoints[i];
        if (endpoint_parse_address(ep, argv[i + 1]) < 0) {
            perrorf("[%s] bad address", argv[i + 1]);
            RETURN(EXIT_FAILURE);
        }

        if (sockaddr_in_getaddrinfo(&ep->sa, ep->host, ep->port) < 0) {
            perrorf("[%s:%d] getaddrinfo error", ep->host, ep->port);
            RETURN(EXIT_FAILURE);
        }

        if (fd_set_socket(&ep->fd) < 0) {
            perrorf("[%s:%d] socket error", ep->host, ep->port);
            RETURN(EXIT_FAILURE);
        }

        // setup deadlines for endpoints
        if (ep->timeout > 0) {
            timespec_monotime(&ts);
            ep->deadline.tv_sec = ts.tv_sec + ep->timeout;
            ep->deadline.tv_nsec = ts.tv_nsec;
        }
    }

    struct sockaddr _addr_;
    socklen_t _addr_len_;
    int done = 0;

    while (true) {
        // try to connect each not connected and not time-outed endpoint
        for (int i = 0; i < ep_count; ++i) {
            endpoint_t *ep = &endpoints[i];
            if (ep->is_connected || ep->is_failed) {
                continue;
            }
            if (connect(ep->fd, (struct sockaddr *)&ep->sa, sizeof(ep->sa)) < 0) {
                if (errno == EINPROGRESS) {
                    ep->in_progress = true;
                } else {
                    if (errno == EAFNOSUPPORT) {
                        RETURN(EXIT_FAILURE);
                    }
                    ep->in_progress = false;
                }
                if (ep->deadline.tv_sec > 0) {
                    timespec_monotime(&ts);
                    if (ts.tv_sec >= ep->deadline.tv_sec && ts.tv_nsec >= ep->deadline.tv_nsec) {
                        ep->is_failed = true;
                        ++done;
                        printf("%s:%d is unavailable\n", ep->host, ep->port);
                        RETURN(EXIT_FAILURE);
                    }
                }
            } else {
                endpoint_close(ep);
                ++done;
            }
        }
        if (done >= ep_count) break;
        nanosleep(&INTERVAL, NULL); // give some time to establish connections

        // check availability by executing getpeername on each socket which is in progress
        for (int i = 0; i < ep_count; ++i) {
            endpoint_t *ep = &endpoints[i];
            if (ep->is_connected || ep->is_failed || !ep->in_progress) {
                continue;
            }
            memset(&_addr_, 0, sizeof(_addr_));
            memset(&_addr_len_, 0, sizeof(_addr_len_));
            if (getpeername(ep->fd, &_addr_, &_addr_len_) < 0) {
                continue;
            }
            // getpeername succeeded
            endpoint_close(ep);
            ++done;
        }
        if (done >= ep_count) break;
        nanosleep(&INTERVAL, NULL);
    }

    exit:
        free(endpoints);
        fflush(stdout);

        if (exit_code == EXIT_SUCCESS) {
            const int cmd_index = ep_count + 2;
            if (argc > cmd_index) {
                if (execvp(argv[cmd_index], &argv[cmd_index]) < 0) {
                    perror("exec error");
                    exit_code = EXIT_FAILURE;
                }
            }
        }

        return exit_code;
}