# Testing and benchmarks - Sandipan

Everything here runs on Linux or WSL and needs `gcc`, `curl` and `python3`.
Run the commands from the project folder (the one with `proxy.c`).

## Files

| File | What it does |
|---|---|
| `origin_server.py` | Small local "website" behind the proxy: `/fast`, `/slow` (200 ms delay), `/binary` (2048 bytes with zero bytes), `/nostore` (`Cache-Control: no-store`) |
| `run_tests.sh` | Functional tests, prints PASS/FAIL for each check |
| `benchmark.sh` | Latency and throughput: direct vs proxy, cold cache vs warm cache |

Neither script changes the source files. They build the proxy in a temporary
folder and start and stop everything themselves. Make sure no proxy is already
running on port 8888 first.

## Run

```
bash tests/run_tests.sh
bash tests/benchmark.sh              # defaults: N=20, T=200, C=10
N=50 T=500 C=20 bash tests/benchmark.sh
```

`benchmark.sh` saves its numbers to `tests/benchmark_results.txt`.

## What the functional tests cover

GET forwarding, cache MISS then HIT in the log, binary-safe caching (the page
is byte-identical from the cache), `no-store` pages not cached, blacklist
(403, including subdomains), unreachable destination (502), POST (501),
malformed request and non-http URL (400), 10 parallel clients, and rate
limiting (429 after RATE_LIMIT requests, logged).

## Notes for the report

- The benchmark proxy is built with the rate limit raised (otherwise the
  20-requests-per-minute limit would throttle the measurements). Say this in
  the report.
- The website is local, so absolute times are small and only the comparison
  between rows is meaningful. For a real-website comparison, run the `curl`
  commands by hand against a public http:// page.
- HTTPS (CONNECT) is not covered by these scripts. Test it by hand, for
  example `curl -x localhost:8888 https://example.com/`.

## Results and analysis

(Paste the numbers from `benchmark_results.txt` here and write your own
analysis and conclusions.)
