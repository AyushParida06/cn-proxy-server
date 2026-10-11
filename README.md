# cn-proxy-server

A multi-threaded HTTP/HTTPS forward proxy written in C, with response caching, access control (blacklist and rate limiting) and request logging. Built as a Computer Networks course project.

## Features

- **HTTP forwarding**: handles `GET` and `HEAD` requests for `http://` URLs. The proxy rewrites the request line and headers, sends `Connection: close` to the origin and relays the response back to the client.
- **HTTPS tunneling**: handles `CONNECT` requests by opening a TCP connection to the target (port 443 by default) and relaying bytes in both directions with `select()` until either side closes. TLS traffic is never decrypted.
- **Multi-threading**: one thread per client connection, listening on port **8888**.
- **Caching**: successful `200` responses to `GET` requests are cached in memory unless they contain `no-store`. Cached responses are served directly on later requests.
- **Access control**: blocks hosts listed in `blacklist.txt` and rate-limits clients by IP.
- **Logging**: every request is appended to `proxy.log` with timestamp, client IP, URL, cache status and decision.
- **Error handling**: returns proper HTTP error codes (see table below).

## Project structure

| File | Purpose |
|------|---------|
| `proxy.c` | Main server: accept loop, per-client threads, HTTP forwarding, `CONNECT` tunneling, error responses |
| `cache.c` | Thread-safe in-memory response cache |
| `access_control.c` | Blacklist check, per-IP rate limiter, request logger |
| `common.h` | Shared declarations (`cache_get`, `cache_put`, `is_blocked`, `is_rate_limited`, `log_request`) |
| `blacklist.txt` | Blocked domains, one per line (`#` starts a comment) |
| `tests/` | Local origin server, functional tests and benchmarks |

## Module details

### Cache (`cache.c`)

- Stores up to **10** responses keyed by URL, protected by a mutex.
- Entries expire after **60 seconds** (`CACHE_TTL_SECONDS`).
- If the cache is full, the entry with the oldest store time is evicted (oldest-stored, not true LRU).
- Binary responses are cached safely.
- `cache_get` returns a hit only if the entry is fresh and fits in the caller's buffer.

### Access control (`access_control.c`)

- **Blacklist**: `blacklist.txt` is read per check. Matching is case-insensitive and covers the exact host and any subdomain (blocking `facebook.com` also blocks `www.facebook.com`). Blocked requests get `403`. If the file is missing, nothing is blocked.
- **Rate limiting**: up to **20 requests per 60 seconds** per client IP, tracking up to 50 IPs. Clients over the limit get `429`.
- **Logging**: appends to `proxy.log`, guarded by its own mutex.

Default `blacklist.txt`:

```
facebook.com
youtube.com
instagram.com
```

## Response codes

| Code | When |
|------|------|
| `400` | Malformed request |
| `403` | Host is blacklisted |
| `429` | Client exceeded the rate limit |
| `501` | Unsupported method |
| `502` | DNS lookup or upstream connection failed |

## Requirements

- Linux or WSL
- `gcc` with pthread support
- `curl` and `python3` (for testing)

## Build

From the repository root:

```bash
gcc proxy.c cache.c access_control.c -o proxy -pthread
```

## Run

```bash
./proxy
```

The proxy listens on port 8888. Make sure nothing else is using it.

## Usage

Plain HTTP:

```bash
curl -x http://localhost:8888 http://example.com/
```

HTTPS (via `CONNECT`):

```bash
curl -x http://localhost:8888 https://example.com/
```

Check the cache: request the same URL twice and look at `proxy.log`. The first entry shows a cache MISS and the second a HIT.

You can also set `localhost:8888` as the HTTP/HTTPS proxy in your browser or OS network settings.

## Testing

All test files live in `tests/`. Run them from the folder containing `proxy.c`.

- `origin_server.py`: local test website with fast, slow (200 ms delay), binary and no-store endpoints.
- `run_tests.sh`: functional tests that print PASS/FAIL. Covers GET forwarding, cache MISS/HIT logging, binary-safe caching, no-store handling, blacklisting (including subdomains), unreachable hosts, POST, malformed requests, parallel clients and rate limiting.
- `benchmark.sh`: measures latency and throughput, comparing direct access vs the proxy and cold vs warm cache. Results are saved to `tests/benchmark_results.txt`.

Notes:

- The benchmark build raises the rate limit, so disclose this if you report the numbers.
- The origin is local, so only the relative comparisons between rows are meaningful.
- HTTPS (`CONNECT`) is not covered by the scripts and must be tested manually.

The cache and access-control modules also have standalone test harnesses, compiled with `-DTEST`:

```bash
gcc -DTEST cache.c -o cache_test -pthread
gcc -DTEST access_control.c -o access_test -pthread
```

## Limitations

- Only `GET` and `HEAD` are forwarded for plain HTTP; other methods return `501`.
- HTTPS is tunneled only, so HTTPS responses are never cached or inspected.
- The cache is in memory only, holds 10 entries and is lost on restart.
- Rate limiter tracks 50 IPs. Clients arriving after the table is full are not limited.

## Contributors

- Cache module: Aarushi
- Access control and logging: Jaysmita
- Proxy core and testing: Sandipan
- Repository owner: Ayush Parida
