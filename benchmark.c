// Latency and throughput measurements: direct vs through the proxy, with a
// cold cache (every request is a miss) and a warm cache (hits).
// (C version of the old benchmark.sh)
//
// Build (from the project folder, in Linux/WSL):
//     gcc -O2 -o tests/benchmark tests/benchmark.c -pthread
// Run (also from the project folder):
//     ./tests/benchmark                          # defaults: N=20, T=200, C=10
//     N=50 T=500 C=20 ./tests/benchmark
//
//   N = requests per latency measurement
//   T = total requests in each throughput run, C = how many run in parallel
//
// Needs only gcc. Your source files are not modified: the proxy is built into
// a temp folder with the rate limit raised so the benchmark is not throttled
// (state this in the report). The 'website' is a local test server, so
// absolute numbers are small; compare the rows with each other.
// Results are also saved to tests/benchmark_results.txt.

#include "test_util.h"
#include <pthread.h>

enum mode { DIRECT, COLD, WARM };

static int N = 20;
static int T = 200;
static int C = 10;

static char work[PATH_MAX] = "";
static pid_t proxy_pid = 0;
static pid_t origin_pid = 0;
static FILE *out_fp = NULL;

static void cleanup(void) {
    stop_proc(&proxy_pid);
    stop_proc(&origin_pid);
    if (work[0]) rmtree(work);
}

static void on_signal(int sig) {
    (void)sig;
    exit(130); // runs cleanup() through atexit
}

// print to the screen and to the results file (like tee)
static void emit(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    if (out_fp) {
        va_start(ap, fmt);
        vfprintf(out_fp, fmt, ap);
        va_end(ap);
    }
}

static void env_int(const char *name, int *value) {
    const char *s = getenv(name);
    if (s && atoi(s) > 0) *value = atoi(s);
}

/* ---------------------------------------------------------- latency */

// Time one full request in milliseconds (-1 if it failed).
static double time_ms(enum mode m, const char *path) {
    struct response r;
    char url[300];
    double t0 = now_sec();
    int rc;
    if (m == DIRECT) {
        rc = fetch_origin(path, &r);
    } else {
        snprintf(url, sizeof(url), ORIGIN_URL "%s", path);
        rc = fetch_via_proxy("GET", url, &r);
    }
    double t1 = now_sec();
    if (rc < 0) return -1;
    int ok = (status_code(&r) == 200);
    response_free(&r);
    return ok ? (t1 - t0) * 1000.0 : -1;
}

static int cmp_double(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static void print_stats(const char *label, double *v, int n) {
    if (n == 0) {
        emit("  %s  no successful requests\n", label);
        return;
    }
    qsort(v, (size_t)n, sizeof(double), cmp_double);
    double sum = 0;
    for (int i = 0; i < n; i++) sum += v[i];
    emit("  %s  mean %8.2f ms   min %8.2f   median %8.2f   max %8.2f   (n=%d)\n", label, sum / n, v[0],
         v[(n + 1) / 2 - 1], v[n - 1], n);
}

static void latency(const char *label, const char *path, enum mode m) {
    double *v = malloc(sizeof(double) * (size_t)N);
    int n = 0;
    char p[256];

    if (m == WARM) { // warm-up request, not counted
        snprintf(p, sizeof(p), "%s?w=1", path);
        time_ms(COLD, p);
        sleep_sec(0.3);
    }
    for (int i = 1; i <= N; i++) {
        if (m == DIRECT) snprintf(p, sizeof(p), "%s?d=%d", path, i);
        else if (m == COLD) snprintf(p, sizeof(p), "%s?c=%d", path, i);
        else snprintf(p, sizeof(p), "%s?w=1", path);
        double ms = time_ms(m, p);
        if (ms >= 0) v[n++] = ms;
    }
    print_stats(label, v, n);
    free(v);
}

/* ------------------------------------------------------- throughput */

struct tp {
    enum mode m;
    int total;
    int next; // last job number handed out
};

static void *tp_worker(void *arg) {
    struct tp *t = arg;
    for (;;) {
        int i = __atomic_add_fetch(&t->next, 1, __ATOMIC_SEQ_CST);
        if (i > t->total) break;
        char path[128];
        if (t->m == WARM) snprintf(path, sizeof(path), "/binary?tw=1");
        else snprintf(path, sizeof(path), "/binary?t=%s%d", t->m == DIRECT ? "direct" : "cold", i);
        time_ms(t->m, path);
    }
    return NULL;
}

static void throughput(const char *label, enum mode m) {
    if (m == WARM) { // fill the cache first
        time_ms(COLD, "/binary?tw=1");
        sleep_sec(0.3);
    }
    struct tp t = {m, T, 0};
    pthread_t *th = malloc(sizeof(pthread_t) * (size_t)C);
    double start = now_sec();
    for (int i = 0; i < C; i++) pthread_create(&th[i], NULL, tp_worker, &t);
    for (int i = 0; i < C; i++) pthread_join(th[i], NULL);
    double d = now_sec() - start;
    free(th);
    emit("  %s  %6d requests in %6.2f s  =  %8.1f requests/s   %6.2f MB/s of page data\n", label, T, d, T / d,
         T * 2048.0 / d / 1000000.0);
}

/* ------------------------------------------------------------- main */

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    env_int("N", &N);
    env_int("T", &T);
    env_int("C", &C);

    char root[PATH_MAX];
    if (!getcwd(root, sizeof(root))) return 2;
    char probe[PATH_MAX];
    snprintf(probe, sizeof(probe), "%s/proxy.c", root);
    if (access(probe, R_OK) != 0) {
        fprintf(stderr, "proxy.c not found here. Run from the project folder: ./tests/benchmark\n");
        return 2;
    }

    // Refuse to run if something else already uses our ports.
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

    snprintf(work, sizeof(work), "/tmp/proxybench.XXXXXX");
    if (!mkdtemp(work)) {
        perror("mkdtemp");
        work[0] = '\0';
        return 2;
    }

    char proxy_dir[PATH_MAX], proxy_exe[PATH_MAX], proxy_out[PATH_MAX], proxy_log[PATH_MAX];
    char origin_exe[PATH_MAX], origin_out[PATH_MAX], results[PATH_MAX];
    snprintf(proxy_dir, sizeof(proxy_dir), "%s/proxy", work);
    snprintf(proxy_exe, sizeof(proxy_exe), "%s/proxy", proxy_dir);
    snprintf(proxy_out, sizeof(proxy_out), "%s/proxy.out", proxy_dir);
    snprintf(proxy_log, sizeof(proxy_log), "%s/proxy.log", proxy_dir);
    snprintf(origin_exe, sizeof(origin_exe), "%s/origin_server", work);
    snprintf(origin_out, sizeof(origin_out), "%s/origin.out", work);
    snprintf(results, sizeof(results), "%s/tests/benchmark_results.txt", root);

    if (build_proxy(root, proxy_dir, 1000000, 1) < 0) return 2;
    if (build_origin(root, work) < 0) return 2;

    origin_pid = spawn(work, origin_out, origin_exe, "9000");
    if (!wait_for_port(ORIGIN_PORT)) return 2;
    proxy_pid = spawn(proxy_dir, proxy_out, proxy_exe, NULL);
    if (!wait_for_port(PROXY_PORT)) return 2;

    out_fp = fopen(results, "w");
    if (!out_fp) fprintf(stderr, "(cannot write %s, printing to the screen only)\n", results);

    char stamp[64];
    time_t now = time(NULL);
    strftime(stamp, sizeof(stamp), "%a %b %d %H:%M:%S %Y", localtime(&now));

    emit("Benchmark run: %s\n", stamp);
    emit("N=%d latency samples, T=%d requests at C=%d parallel for throughput\n", N, T, C);
    emit("Proxy built with RATE_LIMIT raised to 1000000 (benchmark copy only).\n");
    emit("Local test website; /slow waits 200 ms before answering, /fast answers at once,\n");
    emit("/binary is a 2048-byte page.\n\n");

    emit("LATENCY, slow page (origin takes 200 ms):\n");
    latency("direct to website      ", "/slow", DIRECT);
    latency("proxy, cache miss      ", "/slow", COLD);
    latency("proxy, cache hit       ", "/slow", WARM);
    emit("\n");

    emit("LATENCY, fast page (origin answers immediately):\n");
    latency("direct to website      ", "/fast", DIRECT);
    latency("proxy, cache miss      ", "/fast", COLD);
    latency("proxy, cache hit       ", "/fast", WARM);
    emit("\n");

    emit("THROUGHPUT, %d parallel clients:\n", C);
    throughput("direct to website      ", DIRECT);
    throughput("proxy, cache miss      ", COLD);
    throughput("proxy, cache hit       ", WARM);
    emit("\n");

    emit("Cache hits and misses seen by the proxy:\n");
    emit("  HIT  : %d\n", count_in_file(proxy_log, "| HIT |"));
    emit("  MISS : %d\n", count_in_file(proxy_log, "| MISS |"));

    if (out_fp) {
        fclose(out_fp);
        printf("\nSaved to tests/benchmark_results.txt\n");
    }
    return 0;
}
