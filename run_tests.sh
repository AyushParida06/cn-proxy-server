#!/usr/bin/env bash
# Functional tests for the proxy.
#
# Usage (from the project folder, in Linux/WSL):   bash tests/run_tests.sh
#
# Needs: gcc, curl, python3.  It builds the proxy into a temp folder (your
# source files are not modified), starts a local test website and the proxy,
# runs the checks, and prints PASS/FAIL for each.

set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORK="$(mktemp -d)"
PROXY="127.0.0.1:8888"
ORIGIN="http://127.0.0.1:9000"
PASS=0
FAIL=0
PROXY_PID=""
ORIGIN_PID=""

cleanup() {
    [ -n "$PROXY_PID" ] && kill "$PROXY_PID" 2>/dev/null
    [ -n "$ORIGIN_PID" ] && kill "$ORIGIN_PID" 2>/dev/null
    rm -rf "$WORK"
}
trap cleanup EXIT

# build_proxy <dir> [rate_limit_override]
build_proxy() {
    mkdir -p "$1"
    cp "$ROOT"/proxy.c "$ROOT"/cache.c "$ROOT"/access_control.c "$ROOT"/common.h "$1"/
    cp "$ROOT"/blacklist.txt "$1"/ 2>/dev/null || true
    if [ -n "${2:-}" ]; then
        sed -i "s/^#define RATE_LIMIT .*/#define RATE_LIMIT $2/" "$1/access_control.c"
    fi
    gcc -w -pthread -o "$1/proxy" "$1/proxy.c" "$1/cache.c" "$1/access_control.c" || {
        echo "BUILD FAILED"; exit 2; }
}

wait_for_port() { # port
    for _ in $(seq 50); do
        (exec 3<>/dev/tcp/127.0.0.1/"$1") 2>/dev/null && return 0
        sleep 0.1
    done
    echo "nothing is listening on port $1"; exit 2
}

start_proxy() { # dir
    ( cd "$1" && rm -f proxy.log && exec ./proxy > proxy.out 2>&1 ) &
    PROXY_PID=$!
    wait_for_port 8888
}

stop_proxy() {
    kill "$PROXY_PID" 2>/dev/null
    wait "$PROXY_PID" 2>/dev/null
    PROXY_PID=""
}

check() { # name expected actual
    if [ "$2" = "$3" ]; then
        echo "PASS  $1"; PASS=$((PASS + 1))
    else
        echo "FAIL  $1   (expected '$2', got '$3')"; FAIL=$((FAIL + 1))
    fi
}

via_proxy() { curl -s --noproxy '' -x "http://$PROXY" --max-time 10 "$@"; }
code() { via_proxy -o /dev/null -w '%{http_code}' "$@"; }
count_log() { grep -c -F "$1" "$2/proxy.log" 2>/dev/null || true; }

raw() { # send a raw string to the proxy, print the first reply line
    exec 3<>/dev/tcp/127.0.0.1/8888 || return
    printf "$1" >&3
    timeout 3 head -n1 <&3 | tr -d '\r'
    exec 3<&-
}

echo "== building and starting =="
python3 "$ROOT/tests/origin_server.py" 9000 &
ORIGIN_PID=$!
wait_for_port 9000

# Rate limit raised for the main run so the later tests are not throttled.
build_proxy "$WORK/relaxed" 100000
start_proxy "$WORK/relaxed"
LOG="$WORK/relaxed"

echo "== functional tests =="

body=$(via_proxy "$ORIGIN/hello")
check "GET is forwarded and the body arrives intact" "hello from origin: /hello" "$body"

via_proxy -o /dev/null "$ORIGIN/cacheme"; sleep 0.3
via_proxy -o /dev/null "$ORIGIN/cacheme"; sleep 0.3
check "first request to a page is logged as MISS" 1 "$(count_log "$ORIGIN/cacheme | MISS" "$LOG")"
check "second request to the same page is logged as HIT" 1 "$(count_log "$ORIGIN/cacheme | HIT" "$LOG")"

direct_md5=$(curl -s "$ORIGIN/binary" | md5sum | cut -d' ' -f1)
cold_md5=$(via_proxy "$ORIGIN/binary" | md5sum | cut -d' ' -f1); sleep 0.3
warm_md5=$(via_proxy "$ORIGIN/binary" | md5sum | cut -d' ' -f1)
check "binary page identical through the proxy (cache miss)" "$direct_md5" "$cold_md5"
check "binary page identical when served from the cache (hit)" "$direct_md5" "$warm_md5"
check "binary page really was served from the cache" 1 "$(count_log "$ORIGIN/binary | HIT" "$LOG")"

via_proxy -o /dev/null "$ORIGIN/nostore"; sleep 0.3
via_proxy -o /dev/null "$ORIGIN/nostore"; sleep 0.3
check "Cache-Control: no-store page is never a HIT" 0 "$(count_log "$ORIGIN/nostore | HIT" "$LOG")"

check "blocked domain returns 403" 403 "$(code http://facebook.com/)"
check "subdomain of a blocked domain returns 403" 403 "$(code http://www.facebook.com/)"
check "blocked request is written to the log" 1 "$(count_log "http://facebook.com/ | - | BLOCKED" "$LOG")"

check "unreachable destination returns 502" 502 "$(code http://127.0.0.1:1/)"
check "POST is rejected with 501" 501 "$(code -X POST "$ORIGIN/hello")"
check "malformed request returns 400" "HTTP/1.1 400 Bad Request" "$(raw 'garbage\r\n\r\n')"
check "non-http URL returns 400" "HTTP/1.1 400 Bad Request" "$(raw 'GET ftp://example.com/ HTTP/1.1\r\n\r\n')"

# 10 parallel requests to a page that takes 0.2 s: handled one after another
# this would take >= 2 s; with one thread per client it takes about 0.2 s.
pids=()
start=$(date +%s.%N)
for i in $(seq 10); do
    via_proxy -o /dev/null "$ORIGIN/slow?conc=$i" &
    pids+=($!)
done
wait "${pids[@]}"
end=$(date +%s.%N)
elapsed=$(awk -v s="$start" -v e="$end" 'BEGIN { printf "%.2f", e - s }')
fast_enough=$(awk -v t="$elapsed" 'BEGIN { print (t < 1.0) ? "yes" : "no" }')
check "10 parallel 0.2 s requests finish together (took ${elapsed}s, sequential would be >= 2s)" yes "$fast_enough"

stop_proxy

echo "== rate limiting (uses the real RATE_LIMIT from access_control.c) =="
LIMIT=$(grep -oE '^#define RATE_LIMIT[[:space:]]+[0-9]+' "$ROOT/access_control.c" | grep -oE '[0-9]+$')
build_proxy "$WORK/normal"
start_proxy "$WORK/normal"
ok=0; limited=0
for i in $(seq $((LIMIT + 5))); do
    c=$(code "$ORIGIN/fast")
    [ "$c" = 200 ] && ok=$((ok + 1))
    [ "$c" = 429 ] && limited=$((limited + 1))
done
check "first $LIMIT requests from one client are allowed" "$LIMIT" "$ok"
check "the 5 requests after the limit get 429" 5 "$limited"
check "rate-limited requests are written to the log" 5 "$(count_log "| RATE_LIMITED" "$WORK/normal")"
stop_proxy

echo
echo "Result: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
