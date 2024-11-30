#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>

#include "endpoint.h"

#ifndef CONNECTION_TIME_MS
#define CONNECTION_TIME_MS 25
#endif

#ifndef INTERVAL_MS
#define INTERVAL_MS 1000 // 1s
#endif

#if INTERVAL_MS < CONNECTION_TIME_MS
#error INTERVAL_MS cannot be less than CONNECTION_TIME_MS
#endif

const struct timespec CONNECTION_TIME = {
    .tv_sec = 0,
    .tv_nsec = CONNECTION_TIME_MS * 1000000,
};

#define REAL_INTERVAL_MS (INTERVAL_MS - CONNECTION_TIME_MS)

#define PERROR(...) \
    fprintf(stderr, __VA_ARGS__); \
    fprintf(stderr, ": "); \
    perror("")

const struct timespec INTERVAL = {
    .tv_sec = REAL_INTERVAL_MS / 1000,
    .tv_nsec = REAL_INTERVAL_MS % 1000 * 1000000,
};

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

    // setup endpoints
    for (int i = 0; i < ep_count; ++i) {
        endpoint_t *ep = &endpoints[i];
        if (endpoint_parse_address(ep, argv[i + 1]) < 0) {
            PERROR("[%s] bad address", argv[i + 1]);
            RETURN(EXIT_FAILURE);
        }

        if (endpoint_getaddrinfo(ep) < 0) {
            PERROR("[%s:%d] getaddrinfo error", ep->host, ep->port);
            RETURN(EXIT_FAILURE);
        }

        if (endpoint_create_socket(ep) < 0) {
            PERROR("[%s:%d] socket error", ep->host, ep->port);
            RETURN(EXIT_FAILURE);
        }

        if (ep->timeout > 0) {
            endpoint_set_deadline(ep);
        }
    }

    struct sockaddr _addr_;
    socklen_t _addr_len_;
    int done = 0;

    for (;;) {
        for (int i = 0; i < ep_count; ++i) {
            endpoint_t *ep = &endpoints[i];
            if (ep->is_connected || ep->is_failed) {
                continue;
            }
            if (endpoint_connect(ep) < 0) {
                if (errno == EINPROGRESS) {
                    ep->in_progress = true;
                } else {
                    if (errno == EAFNOSUPPORT) {
                        RETURN(EXIT_FAILURE);
                    }
                    ep->in_progress = false;
                }
                if (ep->deadline.tv_sec > 0) {
                    struct timespec now = monotime();
                    if (now.tv_sec >= ep->deadline.tv_sec && now.tv_nsec >= ep->deadline.tv_nsec) {
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
        nanosleep(&CONNECTION_TIME, NULL); // give some time to establish connections

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

#undef RETURN
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