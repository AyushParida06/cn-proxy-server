// Tiny local web server used as the "real website" behind the proxy.
// (C version of the old origin_server.py)
//
// Build:  gcc -o tests/origin_server tests/origin_server.c -pthread
// Usage:  ./tests/origin_server [port]      (default port 9000)
//
// Paths:
//   /fast      small text page, answers immediately
//   /slow      small text page, answers after DELAY seconds (simulates a slow site)
//   /binary    2048 bytes containing zero bytes (checks binary-safe caching)
//   /nostore   page with 'Cache-Control: no-store' (must never be cached)
//   anything else: small text page
//
// It speaks HTTP/1.1 with Content-Length, because the proxy only caches
// responses that begin with 'HTTP/1.1 200'. A query string (?x=1) is ignored
// when choosing the page.

#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define DEFAULT_PORT 9000
#define DELAY_USEC 200000 /* 0.2 seconds */
#define REQ_MAX 4096

static int send_all(int fd, const char *buf, size_t len) {
    while (len > 0) {
        ssize_t n = send(fd, buf, len, MSG_NOSIGNAL);
        if (n <= 0) return -1;
        buf += n;
        len -= (size_t)n;
    }
    return 0;
}

// Answer one request. Returns 0 to keep the connection open, -1 to close it.
static int respond(int fd, const char *request, int keep_alive) {
    char method[16], target[1024], version[16];
    if (sscanf(request, "%15s %1023s %15s", method, target, version) != 3) {
        const char *bad = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        send_all(fd, bad, strlen(bad));
        return -1;
    }
    if (strcmp(method, "GET") != 0) {
        const char *no = "HTTP/1.1 501 Not Implemented\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        send_all(fd, no, strlen(no));
        return -1;
    }

    // drop the query string
    char *q = strchr(target, '?');
    if (q) *q = '\0';

    char body[2200];
    size_t body_len;
    const char *ctype = "text/plain";
    const char *extra = "";

    if (strcmp(target, "/binary") == 0) {
        for (int i = 0; i < 2048; i++) body[i] = (char)(i % 256);
        body_len = 2048;
        ctype = "application/octet-stream";
    } else if (strcmp(target, "/nostore") == 0) {
        strcpy(body, "not cacheable\n");
        body_len = strlen(body);
        extra = "Cache-Control: no-store\r\n";
    } else {
        if (strcmp(target, "/slow") == 0) usleep(DELAY_USEC);
        int n = snprintf(body, sizeof(body), "hello from origin: %.1000s\n", target);
        body_len = (size_t)n;
    }

    char head[512];
    int h = snprintf(head, sizeof(head),
                     "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %zu\r\n%s%s\r\n",
                     ctype, body_len, extra, keep_alive ? "" : "Connection: close\r\n");
    if (send_all(fd, head, (size_t)h) < 0) return -1;
    if (send_all(fd, body, body_len) < 0) return -1;
    return keep_alive ? 0 : -1;
}

// One thread per connection; serves requests until the client closes.
static void *serve(void *arg) {
    int fd = (int)(long)arg;
    char buf[REQ_MAX + 1];
    size_t have = 0;
    buf[0] = '\0';

    for (;;) {
        char *end;
        while ((end = strstr(buf, "\r\n\r\n")) == NULL) {
            if (have >= REQ_MAX) goto done; // header too big
            ssize_t n = recv(fd, buf + have, REQ_MAX - have, 0);
            if (n <= 0) goto done;
            have += (size_t)n;
            buf[have] = '\0';
        }
        size_t used = (size_t)(end - buf) + 4;

        // the request without the trailing blank line, as a string
        char request[REQ_MAX + 1];
        memcpy(request, buf, used);
        request[used] = '\0';

        int keep_alive = (strcasestr(request, "connection: close") == NULL);
        if (respond(fd, request, keep_alive) < 0) goto done;

        // forget this request, keep any bytes of the next one
        memmove(buf, buf + used, have - used);
        have -= used;
        buf[have] = '\0';
    }

done:
    close(fd);
    return NULL;
}

int main(int argc, char **argv) {
    int port = (argc > 1) ? atoi(argv[1]) : DEFAULT_PORT;
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "usage: %s [port]\n", argv[0]);
        return 1;
    }
    signal(SIGPIPE, SIG_IGN);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(port);
    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }
    listen(server_fd, 128);

    for (;;) {
        int fd = accept(server_fd, NULL, NULL);
        if (fd < 0) continue;
        pthread_t t;
        if (pthread_create(&t, NULL, serve, (void *)(long)fd) != 0) {
            close(fd);
            continue;
        }
        pthread_detach(t);
    }
}
