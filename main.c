#include <fcntl.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <time.h>
#include <stdbool.h>
#include <string.h>
#include <stdarg.h>

const unsigned int USEC_INTERVAL = 50 * 1000;  // 50ms

typedef struct {
    char host[255];
    unsigned int port;
    unsigned int timeout;
} Endpoint;

typedef struct {
    Endpoint ep;
    int fd;
    struct sockaddr_in sa;
    struct timespec deadline;
    bool in_progress;
    bool is_connected;
    bool is_time_outed;
} Conn;

void perrorf(const char *format, ...) {
    va_list args;
    va_start(args, format);
    if (vfprintf(stderr, format, args) < 0) {
        perror("perrorf error");
        exit(EXIT_FAILURE);
    }
    if (fprintf(stderr, ": %s\n", strerror(errno)) < 0) {
        perror("perrorf error");
        exit(EXIT_FAILURE);
    }
    va_end(args);
}

bool parse_addr(Endpoint *ep, char *addr) {
    int res = sscanf(addr, "%[^:]:%d/%d", ep->host, &ep->port, &ep->timeout);
    if (res == 2 || res == 3) {
        return true;
    }
    return false;
}

bool tcp_socket(int *fd_p) {
    *fd_p = socket(AF_INET, SOCK_STREAM, 0);
    if (*fd_p < 0) {
        return false;
    }
    if (fcntl(*fd_p, F_SETFL, O_NONBLOCK) < 0) {
        return false;
    }
    return true;
}

bool get_sockaddr(struct sockaddr_in *sa, const char *host, const unsigned int port) {
    struct addrinfo hints = {0}, *ai;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, NULL, &hints, &ai) < 0) {
        return false;
    }
    *sa = *(struct sockaddr_in *)ai->ai_addr;
    sa->sin_port = htons(port);
    freeaddrinfo(ai);
    return true;
}

void monotime(struct timespec *ts) {
    if (clock_gettime(CLOCK_MONOTONIC, ts) < 0) {
        perror("clock_gettime() error");
        exit(EXIT_FAILURE);
    }
}

int main(int argc, char **argv) {
    // count out endpoints
    int epc = 0;
    for (epc = 0; epc < argc - 1; ++epc) {
        if (strcmp(argv[epc + 1], "--") == 0) {
            break;
        }
    }

    if (epc == 0) {
        fprintf(stderr,
            "Usage: %s <host>:<port>[/<timeout>] [<host>:<port>[/<timeout>] ...] [-- <command>]\n",
            argv[0]);
        return EXIT_FAILURE;
    }

    Conn connections[epc];
    struct timespec ts;

    // setup connections
    for (int i = 0; i < epc; ++i) {
        Endpoint ep = {0};
        if (!parse_addr(&ep, argv[i + 1])) {
            perrorf("%s - bad address", argv[i + 1]);
            return EXIT_FAILURE;
        }

        Conn *c = &connections[i];
        memset(c, 0, sizeof(Conn));
        c->ep = ep;

        if (!get_sockaddr(&c->sa, c->ep.host, c->ep.port)) {
            perrorf("%s:%d - msg", c->ep.host, c->ep.port);
            return EXIT_FAILURE;
        }

        if (!tcp_socket(&c->fd)) {
            perrorf("%s:%d - msg", c->ep.host, c->ep.port);
            return EXIT_FAILURE;
        }

        // setup deadlines for connections
        if (c->ep.timeout > 0) {
            monotime(&ts);
            c->deadline.tv_sec = ts.tv_sec + c->ep.timeout;
            c->deadline.tv_nsec = ts.tv_nsec;
        }
    }

    struct sockaddr _addr_;
    socklen_t _addr_len_;
    int done = 0;

    while (done < epc) {
        // try to connect each not connected and not time-outed endpoint
        for (int i = 0; i < epc; ++i) {
            Conn *c = &connections[i];
            if (c->is_connected || c->is_time_outed) {
                continue;
            }
            if (connect(c->fd, (struct sockaddr *)&c->sa, sizeof(c->sa))) {
                if (errno == EINPROGRESS) {
                    c->in_progress = true;
                } else {
                    if (errno == EAFNOSUPPORT) {
                        return EXIT_FAILURE;
                    }
                    c->in_progress = false;
                }
            }
            if (c->deadline.tv_sec > 0) {
                monotime(&ts);
                if (ts.tv_sec >= c->deadline.tv_sec && ts.tv_nsec >= c->deadline.tv_nsec) {
                    c->is_time_outed = true;
                    ++done;
                    printf("%s:%d is unavailable\n", c->ep.host, c->ep.port);
                    return EXIT_FAILURE;
                }
            }
        }
        // break if not connected endpoints are time-outed
        if (done == epc) {
            break;
        }
        usleep(USEC_INTERVAL); // give some time to establish connection
        // check availability by executing getpeername on each socket which is in progress
        for (int i = 0; i < epc; ++i) {
            Conn *c = &connections[i];
            if (c->is_connected || c->is_time_outed || !c->in_progress) {
                continue;
            }
            memset(&_addr_, 0, sizeof(_addr_));
            memset(&_addr_len_, 0, sizeof(_addr_len_));
            if (getpeername(c->fd, &_addr_, &_addr_len_) < 0) {
                continue;
            }
            // getpeername succeeded - close fd and mark as connected
            close(c->fd);
            c->is_connected = true;
            ++done;
            printf("%s:%d is available\n", c->ep.host, c->ep.port);
        }
    }

    const int cmd_index = epc + 2;
    if (argc > cmd_index) {
        if (execvp(argv[cmd_index], &argv[cmd_index]) < 0) {
            perror("exec error");
            return EXIT_FAILURE;
        }
    }

    return EXIT_SUCCESS;
}