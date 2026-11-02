// Helpers shared by run_tests.c and benchmark.c (Linux / WSL only).
//
// This must be the FIRST #include in a .c file: it defines _GNU_SOURCE so the
// POSIX/GNU functions used below (memmem, getline, nftw, ...) are declared.
#ifndef TEST_UTIL_H
#define TEST_UTIL_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

// Each program uses only some of the helpers, and paths are built into
// PATH_MAX buffers, so these two gcc warnings are just noise here.
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wformat-truncation"

#include <arpa/inet.h>
#include <fcntl.h>
#include <ftw.h>
#include <limits.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define PROXY_PORT 8888
#define ORIGIN_PORT 9000
#define ORIGIN_URL "http://127.0.0.1:9000"
#define IO_TIMEOUT_SEC 10

/* ---------------------------------------------------------------- time */

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void sleep_sec(double s) { usleep((useconds_t)(s * 1e6)); }

/* ------------------------------------------------------------- sockets */

static int connect_local(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
    if (connect(fd, (struct sockaddr *)&a, sizeof(a)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static int port_open(int port) {
    int fd = connect_local(port);
    if (fd < 0) return 0;
    close(fd);
    return 1;
}

/* Wait up to 5 s for something to listen on the port. */
static int wait_for_port(int port) {
    for (int i = 0; i < 50; i++) {
        if (port_open(port)) return 1;
        usleep(100000);
    }
    fprintf(stderr, "nothing is listening on port %d\n", port);
    return 0;
}

static int send_all(int fd, const char *buf, size_t len) {
    while (len > 0) {
        ssize_t n = send(fd, buf, len, MSG_NOSIGNAL);
        if (n <= 0) return -1;
        buf += n;
        len -= (size_t)n;
    }
    return 0;
}

/* ------------------------------------------------------ HTTP exchanges */

struct response {
    char *data; /* the raw reply: status line, headers, body (NUL at the end) */
    size_t len;
};

static void response_free(struct response *r) {
    free(r->data);
    r->data = NULL;
    r->len = 0;
}

/* Send one raw request, then read until the server closes the connection.
   Returns 0 on success, -1 if the connection could not be made. */
static int exchange(int port, const char *req, size_t req_len, struct response *r) {
    r->data = NULL;
    r->len = 0;
    int fd = connect_local(port);
    if (fd < 0) return -1;
    struct timeval tv = {IO_TIMEOUT_SEC, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    if (send_all(fd, req, req_len) < 0) {
        close(fd);
        return -1;
    }
    size_t cap = 4096, len = 0;
    char *buf = malloc(cap + 1);
    if (!buf) {
        close(fd);
        return -1;
    }
    for (;;) {
        if (len == cap) {
            cap *= 2;
            char *nb = realloc(buf, cap + 1);
            if (!nb) {
                free(buf);
                close(fd);
                return -1;
            }
            buf = nb;
        }
        ssize_t n = recv(fd, buf + len, cap - len, 0);
        if (n <= 0) break;
        len += (size_t)n;
    }
    close(fd);
    buf[len] = '\0';
    r->data = buf;
    r->len = len;
    return 0;
}

/* GET <path> straight from the test website (no proxy). */
static int fetch_origin(const char *path, struct response *r) {
    char req[1024];
    int n = snprintf(req, sizeof(req),
                     "GET %s HTTP/1.1\r\nHost: 127.0.0.1:%d\r\nConnection: close\r\n\r\n",
                     path, ORIGIN_PORT);
    if (n <= 0 || n >= (int)sizeof(req)) return -1;
    return exchange(ORIGIN_PORT, req, (size_t)n, r);
}

/* <method> <absolute url> through the proxy. */
static int fetch_via_proxy(const char *method, const char *url, struct response *r) {
    char req[1024];
    int n = snprintf(req, sizeof(req), "%s %s HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n", method, url);
    if (n <= 0 || n >= (int)sizeof(req)) return -1;
    return exchange(PROXY_PORT, req, (size_t)n, r);
}

static int status_code(const struct response *r) {
    int code = 0;
    if (r->data && sscanf(r->data, "HTTP/%*d.%*d %d", &code) == 1) return code;
    return 0;
}

/* Pointer to the body (after the blank line) and its length. */
static const char *body_of(const struct response *r, size_t *len) {
    if (!r->data) {
        *len = 0;
        return NULL;
    }
    char *p = memmem(r->data, r->len, "\r\n\r\n", 4);
    if (!p) {
        *len = 0;
        return r->data + r->len;
    }
    p += 4;
    *len = r->len - (size_t)(p - r->data);
    return p;
}

/* First line of the reply, without the line ending. */
static void first_line(const struct response *r, char *out, size_t size) {
    size_t i = 0;
    if (r->data) {
        while (i + 1 < size && r->data[i] != '\0' && r->data[i] != '\r' && r->data[i] != '\n') {
            out[i] = r->data[i];
            i++;
        }
    }
    out[i] = '\0';
}

/* ---------------------------------------------------------------- files */

/* Copy a text file. If rate_limit > 0, the "#define RATE_LIMIT ..." line is
   replaced by "#define RATE_LIMIT <rate_limit>" in the copy only. */
static int copy_text_file(const char *src, const char *dst, int rate_limit) {
    FILE *in = fopen(src, "r");
    if (!in) return -1;
    FILE *out = fopen(dst, "w");
    if (!out) {
        fclose(in);
        return -1;
    }
    char *line = NULL;
    size_t cap = 0;
    while (getline(&line, &cap, in) != -1) {
        if (rate_limit > 0 && strncmp(line, "#define RATE_LIMIT", 18) == 0 &&
            (line[18] == ' ' || line[18] == '\t')) {
            fprintf(out, "#define RATE_LIMIT %d\n", rate_limit);
        } else {
            fputs(line, out);
        }
    }
    free(line);
    fclose(in);
    fclose(out);
    return 0;
}

/* How many lines of the file contain the text (like grep -c -F). */
static int count_in_file(const char *path, const char *needle) {
    FILE *fp = fopen(path, "r");
    if (!fp) return 0;
    char *line = NULL;
    size_t cap = 0;
    int count = 0;
    while (getline(&line, &cap, fp) != -1) {
        if (strstr(line, needle)) count++;
    }
    free(line);
    fclose(fp);
    return count;
}

static int rm_cb(const char *path, const struct stat *sb, int flag, struct FTW *ftw) {
    (void)sb;
    (void)flag;
    (void)ftw;
    return remove(path);
}

static void rmtree(const char *path) { nftw(path, rm_cb, 16, FTW_DEPTH | FTW_PHYS); }

/* --------------------------------------------------------- processes */

static int run_cmd(char *const argv[]) {
    fflush(NULL);
    pid_t p = fork();
    if (p < 0) return -1;
    if (p == 0) {
        execvp(argv[0], argv);
        _exit(127);
    }
    int st;
    waitpid(p, &st, 0);
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

/* Start exe (an absolute path) in directory dir with one argument (may be
   NULL), sending its output to out_file. Returns the pid. */
static pid_t spawn(const char *dir, const char *out_file, const char *exe, const char *arg) {
    fflush(NULL);
    pid_t p = fork();
    if (p != 0) return p;
    if (chdir(dir) < 0) _exit(127);
    int fd = open(out_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        dup2(fd, 1);
        dup2(fd, 2);
        close(fd);
    }
    char *argv[3] = {(char *)exe, (char *)arg, NULL};
    execv(exe, argv);
    _exit(127);
}

static void stop_proc(pid_t *p) {
    if (*p > 0) {
        kill(*p, SIGTERM);
        waitpid(*p, NULL, 0);
        *p = 0;
    }
}

/* Copy the proxy sources from root into dir and compile them there, so the
   real source files are never modified. rate_limit > 0 overrides RATE_LIMIT
   in the copy. Binary: dir/proxy.  Returns 0 on success. */
static int build_proxy(const char *root, const char *dir, int rate_limit, int optimize) {
    static const char *files[] = {"proxy.c", "cache.c", "access_control.c", "common.h"};
    char src[PATH_MAX], dst[PATH_MAX];
    mkdir(dir, 0755);
    for (int i = 0; i < 4; i++) {
        snprintf(src, sizeof(src), "%s/%s", root, files[i]);
        snprintf(dst, sizeof(dst), "%s/%s", dir, files[i]);
        int limit = (strcmp(files[i], "access_control.c") == 0) ? rate_limit : 0;
        if (copy_text_file(src, dst, limit) < 0) {
            fprintf(stderr, "cannot read %s (run this from the project folder)\n", src);
            return -1;
        }
    }
    snprintf(src, sizeof(src), "%s/blacklist.txt", root);
    snprintf(dst, sizeof(dst), "%s/blacklist.txt", dir);
    copy_text_file(src, dst, 0); /* optional */

    char out[PATH_MAX], pc[PATH_MAX], cc[PATH_MAX], ac[PATH_MAX];
    snprintf(out, sizeof(out), "%s/proxy", dir);
    snprintf(pc, sizeof(pc), "%s/proxy.c", dir);
    snprintf(cc, sizeof(cc), "%s/cache.c", dir);
    snprintf(ac, sizeof(ac), "%s/access_control.c", dir);
    char *argv[] = {"gcc", "-w", optimize ? "-O2" : "-w", "-pthread", "-o", out, pc, cc, ac, NULL};
    if (run_cmd(argv) != 0) {
        fprintf(stderr, "BUILD FAILED\n");
        return -1;
    }
    return 0;
}

/* Compile tests/origin_server.c into dir/origin_server. */
static int build_origin(const char *root, const char *dir) {
    char src[PATH_MAX], out[PATH_MAX];
    snprintf(src, sizeof(src), "%s/tests/origin_server.c", root);
    snprintf(out, sizeof(out), "%s/origin_server", dir);
    char *argv[] = {"gcc", "-w", "-pthread", "-o", out, src, NULL};
    if (run_cmd(argv) != 0) {
        fprintf(stderr, "BUILD FAILED (origin_server.c)\n");
        return -1;
    }
    return 0;
}

#endif
