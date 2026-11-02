// Functional tests for the proxy. (C version of the old run_tests.sh)
//
// Build (from the project folder, in Linux/WSL):
//     gcc -o tests/run_tests tests/run_tests.c -pthread
// Run (also from the project folder):
//     ./tests/run_tests
//
// Needs only gcc. It builds the proxy and the test website into a temp folder
// (your source files are not modified), starts both, runs the checks, and
// prints PASS/FAIL for each. Ports 8888 and 9000 must be free.

#include "test_util.h"
#include <pthread.h>

static int pass_count = 0;
static int fail_count = 0;
static char work[PATH_MAX] = "";
static pid_t proxy_pid = 0;
static pid_t origin_pid = 0;

static void cleanup(void) {
    stop_proc(&proxy_pid);
    stop_proc(&origin_pid);
    if (work[0]) rmtree(work);
}

static void on_signal(int sig) {
    (void)sig;
    exit(130); // runs cleanup() through atexit
}

/* ------------------------------------------------------------ checks */

static void check_int(const char *name, long expected, long actual) {
    if (expected == actual) {
        printf("PASS  %s\n", name);
        pass_count++;
    } else {
        printf("FAIL  %s   (expected '%ld', got '%ld')\n", name, expected, actual);
        fail_count++;
    }
}

static void check_str(const char *name, const char *expected, const char *actual) {
    if (strcmp(expected, actual) == 0) {
        printf("PASS  %s\n", name);
        pass_count++;
    } else {
        printf("FAIL  %s   (expected '%s', got '%s')\n", name, expected, actual);
        fail_count++;
    }
}

/* ---------------------------------------------------------- helpers */

// HTTP status code of a request through the proxy (0 if no reply).
static int code_via_proxy(const char *method, const char *url) {
    struct response r;
    if (fetch_via_proxy(method, url, &r) < 0) return 0;
    int c = status_code(&r);
    response_free(&r);
    return c;
}

// Body of the reply as a string, without trailing newlines.
static void body_string(const struct response *r, char *out, size_t size) {
    size_t len;
    const char *b = body_of(r, &len);
    if (len >= size) len = size - 1;
    if (len > 0) memcpy(out, b, len);
    while (len > 0 && (out[len - 1] == '\n' || out[len - 1] == '\r')) len--;
    out[len] = '\0';
}

// 1 if the two replies have byte-for-byte identical bodies.
static int same_body(const struct response *a, const struct response *b) {
    size_t la, lb;
    const char *pa = body_of(a, &la);
    const char *pb = body_of(b, &lb);
    return la == lb && la > 0 && memcmp(pa, pb, la) == 0;
}

// Send a raw string to the proxy and return the first line of the reply.
static void raw_first_line(const char *text, char *out, size_t size) {
    struct response r;
    if (exchange(PROXY_PORT, text, strlen(text), &r) < 0) {
        out[0] = '\0';
        return;
    }
    first_line(&r, out, size);
    response_free(&r);
}

static void start_proxy(const char *dir) {
    char log[PATH_MAX], out[PATH_MAX], exe[PATH_MAX];
    snprintf(log, sizeof(log), "%s/proxy.log", dir);
    snprintf(out, sizeof(out), "%s/proxy.out", dir);
    snprintf(exe, sizeof(exe), "%s/proxy", dir);
    remove(log);
    proxy_pid = spawn(dir, out, exe, NULL);
    if (!wait_for_port(PROXY_PORT)) exit(2);
}

static void stop_proxy(void) { stop_proc(&proxy_pid); }

// the parallel test: 10 clients at once, each asking for the 0.2 s page
struct par_job {
    int i;
    int code;
};

static void *par_worker(void *arg) {
    struct par_job *job = arg;
    char url[256];
    snprintf(url, sizeof(url), ORIGIN_URL "/slow?conc=%d", job->i);
    job->code = code_via_proxy("GET", url);
    return NULL;
}

// Read "#define RATE_LIMIT <n>" from access_control.c.
static int read_rate_limit(const char *root) {
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/access_control.c", root);
    FILE *fp = fopen(path, "r");
    if (!fp) return 0;
    char *line = NULL;
    size_t cap = 0;
    int limit = 0;
    while (getline(&line, &cap, fp) != -1) {
        if (strncmp(line, "#define RATE_LIMIT", 18) == 0 && sscanf(line + 18, "%d", &limit) == 1) break;
    }
    free(line);
    fclose(fp);
    return limit;
}

/* ------------------------------------------------------------- main */

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);

    char root[PATH_MAX];
    if (!getcwd(root, sizeof(root))) return 2;
    char probe[PATH_MAX];
    snprintf(probe, sizeof(probe), "%s/proxy.c", root);
    if (access(probe, R_OK) != 0) {
        fprintf(stderr, "proxy.c not found here. Run from the project folder: ./tests/run_tests\n");
        return 2;
    }

    for (int p = 0; p < 2; p++) {
        int port = p ? ORIGIN_PORT : PROXY_PORT;
        if (port_open(port)) {
            fprintf(stderr, "Port %d is already in use (is another proxy or test server running?)\n", port);
            return 2;
        }
    }

    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    atexit(cleanup);

    snprintf(work, sizeof(work), "/tmp/proxytest.XXXXXX");
    if (!mkdtemp(work)) {
        perror("mkdtemp");
        work[0] = '\0';
        return 2;
    }

    char relaxed[PATH_MAX], normal[PATH_MAX], origin_exe[PATH_MAX], origin_out[PATH_MAX];
    char relaxed_log[PATH_MAX], normal_log[PATH_MAX];
    snprintf(relaxed, sizeof(relaxed), "%s/relaxed", work);
    snprintf(normal, sizeof(normal), "%s/normal", work);
    snprintf(origin_exe, sizeof(origin_exe), "%s/origin_server", work);
    snprintf(origin_out, sizeof(origin_out), "%s/origin.out", work);
    snprintf(relaxed_log, sizeof(relaxed_log), "%s/proxy.log", relaxed);
    snprintf(normal_log, sizeof(normal_log), "%s/proxy.log", normal);

    printf("== building and starting ==\n");
    if (build_origin(root, work) < 0) return 2;
    origin_pid = spawn(work, origin_out, origin_exe, "9000");
    if (!wait_for_port(ORIGIN_PORT)) return 2;

    // Rate limit raised for the main run so the later tests are not throttled.
    if (build_proxy(root, relaxed, 100000, 0) < 0) return 2;
    start_proxy(relaxed);

    printf("== functional tests ==\n");
    struct response r, direct, cold, warm;
    char text[256];
    char needle[300];

    // GET is forwarded and the body arrives intact
    if (fetch_via_proxy("GET", ORIGIN_URL "/hello", &r) < 0) r.data = NULL;
    body_string(&r, text, sizeof(text));
    response_free(&r);
    check_str("GET is forwarded and the body arrives intact", "hello from origin: /hello", text);

    // MISS then HIT
    for (int i = 0; i < 2; i++) {
        if (fetch_via_proxy("GET", ORIGIN_URL "/cacheme", &r) == 0) response_free(&r);
        sleep_sec(0.3);
    }
    check_int("first request to a page is logged as MISS", 1,
              count_in_file(relaxed_log, ORIGIN_URL "/cacheme | MISS"));
    check_int("second request to the same page is logged as HIT", 1,
              count_in_file(relaxed_log, ORIGIN_URL "/cacheme | HIT"));

    // binary-safe caching: bytes must match directly, on a miss and on a hit
    memset(&direct, 0, sizeof(direct));
    memset(&cold, 0, sizeof(cold));
    memset(&warm, 0, sizeof(warm));
    fetch_origin("/binary", &direct);
    fetch_via_proxy("GET", ORIGIN_URL "/binary", &cold);
    sleep_sec(0.3);
    fetch_via_proxy("GET", ORIGIN_URL "/binary", &warm);
    check_int("binary page identical through the proxy (cache miss)", 1, same_body(&direct, &cold));
    check_int("binary page identical when served from the cache (hit)", 1, same_body(&direct, &warm));
    check_int("binary page really was served from the cache", 1,
              count_in_file(relaxed_log, ORIGIN_URL "/binary | HIT"));
    response_free(&direct);
    response_free(&cold);
    response_free(&warm);

    // Cache-Control: no-store must never be cached
    for (int i = 0; i < 2; i++) {
        if (fetch_via_proxy("GET", ORIGIN_URL "/nostore", &r) == 0) response_free(&r);
        sleep_sec(0.3);
    }
    check_int("Cache-Control: no-store page is never a HIT", 0,
              count_in_file(relaxed_log, ORIGIN_URL "/nostore | HIT"));

    // blacklist
    check_int("blocked domain returns 403", 403, code_via_proxy("GET", "http://facebook.com/"));
    check_int("subdomain of a blocked domain returns 403", 403,
              code_via_proxy("GET", "http://www.facebook.com/"));
    check_int("blocked request is written to the log", 1,
              count_in_file(relaxed_log, "http://facebook.com/ | - | BLOCKED"));

    // errors
    check_int("unreachable destination returns 502", 502, code_via_proxy("GET", "http://127.0.0.1:1/"));
    check_int("POST is rejected with 501", 501, code_via_proxy("POST", ORIGIN_URL "/hello"));
    raw_first_line("garbage\r\n\r\n", text, sizeof(text));
    check_str("malformed request returns 400", "HTTP/1.1 400 Bad Request", text);
    raw_first_line("GET ftp://example.com/ HTTP/1.1\r\n\r\n", text, sizeof(text));
    check_str("non-http URL returns 400", "HTTP/1.1 400 Bad Request", text);

    // 10 parallel requests to a page that takes 0.2 s: handled one after
    // another this would take >= 2 s; with one thread per client about 0.2 s.
    {
        pthread_t th[10];
        struct par_job jobs[10];
        double start = now_sec();
        for (int i = 0; i < 10; i++) {
            jobs[i].i = i + 1;
            jobs[i].code = 0;
            pthread_create(&th[i], NULL, par_worker, &jobs[i]);
        }
        for (int i = 0; i < 10; i++) pthread_join(th[i], NULL);
        double elapsed = now_sec() - start;
        int all_ok = 1;
        for (int i = 0; i < 10; i++) {
            if (jobs[i].code != 200) all_ok = 0;
        }
        char name[160];
        snprintf(name, sizeof(name),
                 "10 parallel 0.2 s requests finish together (took %.2fs, sequential would be >= 2s)", elapsed);
        check_str(name, "yes", (elapsed < 1.0 && all_ok) ? "yes" : "no");
    }

    stop_proxy();

    printf("== rate limiting (uses the real RATE_LIMIT from access_control.c) ==\n");
    int limit = read_rate_limit(root);
    if (limit <= 0) {
        fprintf(stderr, "could not read RATE_LIMIT from access_control.c\n");
        return 2;
    }
    if (build_proxy(root, normal, 0, 0) < 0) return 2;
    start_proxy(normal);

    int ok = 0, limited = 0;
    for (int i = 0; i < limit + 5; i++) {
        int c = code_via_proxy("GET", ORIGIN_URL "/fast");
        if (c == 200) ok++;
        if (c == 429) limited++;
    }
    snprintf(text, sizeof(text), "first %d requests from one client are allowed", limit);
    check_int(text, limit, ok);
    check_int("the 5 requests after the limit get 429", 5, limited);
    snprintf(needle, sizeof(needle), "| RATE_LIMITED");
    check_int("rate-limited requests are written to the log", 5, count_in_file(normal_log, needle));
    stop_proxy();

    printf("\nResult: %d passed, %d failed\n", pass_count, fail_count);
    return fail_count == 0 ? 0 : 1;
}
