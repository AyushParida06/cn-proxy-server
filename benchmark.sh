#!/usr/bin/env bash
# Latency and throughput measurements: direct vs through the proxy,
# with a cold cache (every request is a miss) and a warm cache (hits).
#
# Usage (from the project folder, in Linux/WSL):
#     bash tests/benchmark.sh            # defaults: N=20, T=200, C=10
#     N=50 T=500 C=20 bash tests/benchmark.sh
#
#   N = requests per latency measurement
#   T = total requests in each throughput run, C = how many run in parallel
#
# Needs gcc, curl, python3.  Your source files are not modified: the proxy is
# built into a temp folder with the rate limit raised so the benchmark is not
# throttled (state this in the report).  The 'website' is a local test server,
# so absolute numbers are small; compare the rows with each other.
# Results are also saved to tests/benchmark_results.txt.

set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORK="$(mktemp -d)"
PROXY="127.0.0.1:8888"
ORIGIN="http://127.0.0.1:9000"
N="${N:-20}"
T="${T:-200}"
C="${C:-10}"
OUT="$ROOT/tests/benchmark_results.txt"
PROXY_PID=""
ORIGIN_PID=""

cleanup() {
    [ -n "$PROXY_PID" ] && kill "$PROXY_PID" 2>/dev/null
    [ -n "$ORIGIN_PID" ] && kill "$ORIGIN_PID" 2>/dev/null
    rm -rf "$WORK"
}
trap cleanup EXIT

wait_for_port() {
    for _ in $(seq 50); do
        (exec 3<>/dev/tcp/127.0.0.1/"$1") 2>/dev/null && return 0
        sleep 0.1
    done
    echo "nothing is listening on port $1"; exit 2
}

# Refuse to run if something else already uses our ports.
for p in 8888 9000; do
    if (exec 3<>/dev/tcp/127.0.0.1/$p) 2>/dev/null; then
        echo "Port $p is already in use (is another proxy or test server running?)"; exit 2
    fi
done

mkdir -p "$WORK/proxy"
cp "$ROOT"/proxy.c "$ROOT"/cache.c "$ROOT"/access_control.c "$ROOT"/common.h "$WORK/proxy/"
cp "$ROOT"/blacklist.txt "$WORK/proxy/" 2>/dev/null || true
sed -i "s/^#define RATE_LIMIT .*/#define RATE_LIMIT 1000000/" "$WORK/proxy/access_control.c"
gcc -w -O2 -pthread -o "$WORK/proxy/proxy" "$WORK/proxy/proxy.c" "$WORK/proxy/cache.c" \
    "$WORK/proxy/access_control.c" || { echo "BUILD FAILED"; exit 2; }

python3 "$ROOT/tests/origin_server.py" 9000 &
ORIGIN_PID=$!
wait_for_port 9000
( cd "$WORK/proxy" && exec ./proxy > proxy.out 2>&1 ) &
PROXY_PID=$!
wait_for_port 8888

# ---- helpers -------------------------------------------------------------
# time_ms <curl args...>  -> prints the total request time in milliseconds
time_ms() {
    curl -s --noproxy '' -o /dev/null -w '%{time_total}\n' --max-time 20 "$@" |
        awk '{ printf "%.3f\n", $1 * 1000 }'
}

# stats: reads numbers on stdin, prints mean/min/median/max
stats() {
    sort -n | awk '{ a[NR] = $1; s += $1 }
        END { printf "mean %8.2f ms   min %8.2f   median %8.2f   max %8.2f   (n=%d)\n",
              s / NR, a[1], a[int((NR + 1) / 2)], a[NR], NR }'
}

# latency <label> <path> <direct|cold|warm>
latency() {
    local label="$1" path="$2" mode="$3" i
    {
        case "$mode" in
        direct) for i in $(seq "$N"); do time_ms "$ORIGIN$path?d=$i"; done ;;
        cold)   for i in $(seq "$N"); do time_ms -x "http://$PROXY" "$ORIGIN$path?c=$i"; done ;;
        warm)   time_ms -x "http://$PROXY" "$ORIGIN$path?w=1" > /dev/null   # warm-up, not counted
                sleep 0.3
                for i in $(seq "$N"); do time_ms -x "http://$PROXY" "$ORIGIN$path?w=1"; done ;;
        esac
    } | stats | sed "s|^|  $label  |"
}

# throughput <label> <direct|cold|warm>
throughput() {
    local label="$1" mode="$2" flags="" url start end
    [ "$mode" != direct ] && flags="-x http://$PROXY"
    if [ "$mode" = warm ]; then
        curl -s --noproxy '' $flags -o /dev/null "$ORIGIN/binary?tw=1"; sleep 0.3
        url="$ORIGIN/binary?tw=1"
    else
        url="$ORIGIN/binary?t=$mode{}"
    fi
    start=$(date +%s.%N)
    seq 1 "$T" | xargs -P "$C" -I{} curl -s --noproxy '' $flags -o /dev/null --max-time 30 "$url"
    end=$(date +%s.%N)
    awk -v l="$label" -v s="$start" -v e="$end" -v t="$T" 'BEGIN {
        d = e - s
        printf "  %s  %6d requests in %6.2f s  =  %8.1f requests/s   %6.2f MB/s of page data\n",
               l, t, d, t / d, t * 2048 / d / 1000000 }'
}

# ---- run -----------------------------------------------------------------
{
    echo "Benchmark run: $(date)"
    echo "N=$N latency samples, T=$T requests at C=$C parallel for throughput"
    echo "Proxy built with RATE_LIMIT raised to 1000000 (benchmark copy only)."
    echo "Local test website; /slow waits 200 ms before answering, /fast answers at once,"
    echo "/binary is a 2048-byte page."
    echo
    echo "LATENCY, slow page (origin takes 200 ms):"
    latency "direct to website      " /slow direct
    latency "proxy, cache miss      " /slow cold
    latency "proxy, cache hit       " /slow warm
    echo
    echo "LATENCY, fast page (origin answers immediately):"
    latency "direct to website      " /fast direct
    latency "proxy, cache miss      " /fast cold
    latency "proxy, cache hit       " /fast warm
    echo
    echo "THROUGHPUT, $C parallel clients:"
    throughput "direct to website      " direct
    throughput "proxy, cache miss      " cold
    throughput "proxy, cache hit       " warm
    echo
    echo "Cache hits and misses seen by the proxy:"
    echo "  HIT  : $(grep -c '| HIT |' "$WORK/proxy/proxy.log")"
    echo "  MISS : $(grep -c '| MISS |' "$WORK/proxy/proxy.log")"
} 2>&1 | tee "$OUT"

echo
echo "Saved to tests/benchmark_results.txt"
