# Benchmarks

Each benchmark defines one deterministic workload and runs it through
`harness.cies`. The harness verifies the workload result before reporting a
measurement, so a faster but incorrect execution is always a failure.

Run one benchmark with a release build:

```text
cieto benchmarks/bench_arithmetic_loop.cies
```

The default mode performs one untimed warmup followed by five measured runs in
the same VM. Each measured run emits one JSON line:

```json
{"benchmark":"arithmetic_loop","mode":"steady","size":3000000,"iteration":1,"elapsed_us":12345,"verified":true}
```

Run the fast correctness suite with:

```text
cieto benchmarks/smoke.cies --smoke
```

Smoke mode uses reduced workload sizes, skips warmup, and runs each case once.
CTest runs this mode as a correctness check. It does not compare timings or use
performance thresholds. Both optimized and `--no-opt` execution are checked.
