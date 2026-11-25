# SplitMesher: probe only possible pairs

Mesh's SplitMesher (`shiftedSplitting`) splits a size class's candidate spans
into two halves.  Each left span probes up to `t` right spans for one it can
mesh with.

## What changed

1. **Occupancy filter.** Two spans with `r` and `r'` objects can mesh only if
   `r + r' ≤ b`, the slots per span; otherwise their bitmaps must overlap.  The
   right half is sorted by occupancy (taken once at the start of the pass), and
   a left span with `r` objects probes only the `P` right spans holding at most
   `b - r`: positions `j, j+1, …` mod `P`.  Before, every probe could land on
   any right span; in fragmented classes most of them were impossible.
2. **Probe limit.** `t` was a constant 64.  It is now `MESH_MAX_PROBES`
   (default 256), and the search stops at the mesh pass's time budget
   (`MESH_PASS_BUDGET_MS`, checked every 4,096 probes).

## Why (theory)

- **The filter never skips a meshable pair.** `filter_sound` in
  `theory/lean/MeshTheory/Occupancy.lean` proves this, and `qu_antitone` shows
  that lower-occupancy partners mesh more often.
- **Probe depth.** Lemma 5.3 (`lemma53_expectation`, `lemma53_hetero` in
  `theory/lean/MeshTheory/Prob/`) guarantees
  `E[matches]/h ≥ (1 − e^{−k})/(2 − e^{−k})` with `k = t·q`.  What matters is
  `k`, not `t`.  In fragmented classes `q` is small (around 10⁻³ for 16-slot
  spans about half full), so `t = 64` gives `k ≈ 0.06`: almost nothing.
- **Why not `t = k/q` from an estimate of `q`.** Per-class estimates
  under-provision.  Most spans in a fragmented class are dense enough that
  they can't mesh with each other, so the class-wide `q` is tiny even when a
  sparse minority meshes readily.  Even restricted to possible pairs, `q`
  varies so much between pairs that the returns from more probes stay smooth.
  So the depth is set by the time budget, with a cap.

## Simulation (Redis/Valkey traces, `bench/hugepage/meshsim.cc`)

Final memory:

| | today (t = 64) | filter, t = 64 | filter, t = 256 | filter, t = 1024 |
|---|---|---|---|---|
| Redis, high-key | 169.7 MiB | 164.5 | 161.9 | 160.0 |
| Valkey, high-key | 152.9 | 147.3 | 144.7 | 142.9 |
| Redis/Valkey, low-key | 114.8 / 114.0 | 114.5 / 113.3 | 114.4 / 113.2 | 114.4 / 113.0 |

Probes, Redis high-key: 186M today, then 99M, 332M and 1,205M with the
filter.

## Measured (bench/kv)

EC2 t3.2xlarge, mstat, 3 trials.  The variants are interleaved in random
order within each trial, so drift on the machine affects them equally.
Medians:

| | today | filter, t = 64 | filter, t = 256 (default) |
|---|---|---|---|
| Redis high-key: final / mean | 186.4 / 231.2 MiB | 184.1 / 238.2 | **180.8 / 224.5** |
| Valkey high-key: final / mean | 169.7 / 207.6 | 166.9 / 211.5 | **163.4 / 205.2** |
| Redis low-key: final | 129.8 | 130.1 | 129.2 |
| Valkey low-key: final | 127.8 | 128.2 | 126.9 |
| Redis high-key: probe p99 / p99.9 | 7.5 / 16.4 ms | 7.6 / 16.5 | 6.8 / 17.1 |
| Valkey high-key: probe p99 / p99.9 | 6.7 / 17.0 | 6.0 / 15.3 | 5.6 / 14.5 |

- **Fragmented heaps:** 3–4% less final memory, and 1–3% less on average
  over the run.
- **Dense heaps:** unchanged.
- **Latency:** the same within noise.  Absolute latencies were higher than in
  earlier sessions on this machine for every variant, including today's code.
- **Without the per-class cap** (`kMaxProbesPerClass`), deeper searches were
  much worse (Redis high-key 198–243 MiB).  One large class used up the
  pass's time budget searching and starved the others.  Moving meshing off
  the request path would let the search run deeper.
