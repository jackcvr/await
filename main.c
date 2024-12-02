#include <string.h>
#include <errno.h>
#include <stdlib.h>
#define _GNU_SOURCE
#include <unistd.h>
#include <stdio.h>

#include "endpoint.h"

#ifndef CONNECTION_TIME_MS
#define CONNECTION_TIME_MS 25
#endif

#ifndef INTERVAL_MS
#define INTERVAL_MS 250
#endif

#if INTERVAL_MS < CONNECTION_TIME_MS
#error INTERVAL_MS cannot be less than CONNECTION_TIME_MS
#endif

#define REAL_INTERVAL_MS (INTERVAL_MS - CONNECTION_TIME_MS)

#define PERROR(...) \
    fprintf(stderr, __VA_ARGS__); \
    fprintf(stderr, ": "); \
    perror("")

const useconds_t CONNECTION_TIME = CONNECTION_TIME_MS * 1000;
const useconds_t INTERVAL = REAL_INTERVAL_MS * 1000;

int main(int argc, char *argv[]) {
    int exit_code = EXIT_SUCCESS;
#define RETURN(code) exit_code = code; goto exit

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
        endpoint_set_deadline(ep);
    }

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
                if (endpoint_is_expired(ep)) {
                    ep->is_failed = true;
                    ++done;
                    printf("%s:%d is unavailable\n", ep->host, ep->port);
                    RETURN(EXIT_FAILURE);
                }
            } else {
                endpoint_close(ep);
                ep->is_connected = true;
                ++done;
                printf("%s:%d is available\n", ep->host, ep->port);
            }
        }

        if (done >= ep_count) break;
        usleep(CONNECTION_TIME); // give some time to establish connections

        for (int i = 0; i < ep_count; ++i) {
            endpoint_t *ep = &endpoints[i];
            if (ep->is_connected || ep->is_failed || !ep->in_progress) {
                continue;
            }
            if (endpoint_is_connected(ep)) {
                endpoint_close(ep);
                ++done;
            }
        }

        if (done >= ep_count) break;
        usleep(INTERVAL);
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
