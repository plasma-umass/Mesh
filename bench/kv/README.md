# Redis / Valkey fragmentation benchmark

The Mesh paper's Redis experiment (PLDI'19, §6.2.2), run on current Redis and
Valkey. It is adapted from Redis's own active-defrag test
(`tests/unit/memefficiency.tcl`):

- configure the server as an LRU cache with `maxmemory 100mb`;
- insert 700,000 keys with 240-byte values;
- insert 170,000 keys with 492-byte values, which evicts most of the first
  batch and leaves sparse, fragmented pages;
- let the server idle while sampling memory.

```sh
bench/kv/build-servers.sh                      # Redis 8.0.2, Valkey 9.1.0; jemalloc and MALLOC=libc builds
go install github.com/bpowers/mstat@latest     # the paper's memory-measurement tool
sudo python3 bench/kv/fragmentation.py --libmesh build/lib/libmesh.so --mstat ~/go/bin/mstat --check
```

| Config | Build and allocator |
|---|---|
| `jemalloc` | default build (bundled jemalloc) |
| `jemalloc+activedefrag` | default build, with the server's active defragmentation enabled after insertion |
| `glibc` | `MALLOC=libc` build |
| `mesh-nomesh` | `MALLOC=libc` build, `LD_PRELOAD=libmesh.so`, `MESH_PERIOD_MS=0` |
| `mesh` | `MALLOC=libc` build, `LD_PRELOAD=libmesh.so` |
| `mesh-thp` (opt-in, `--configs`) | as `mesh`, with `MESH_THP=1`; needs `shmem_enabled=advise` (see `docs/thp.md`) |

`mesh-nomesh` keeps Mesh's allocator and randomization and only disables meshing.

## Plotting

```sh
python3 bench/kv/fragmentation.py ... --json low.json --populate-gap 1.5
python3 bench/kv/fragmentation.py ... --json high.json --policy allkeys-random
python3 bench/kv/plot.py low.json high.json -o memory.html
```

`plot.py` writes one self-contained HTML page with no dependencies. For each
server and eviction state it shows:

- **memory over time:** the median across trials, from the start of insertion,
  as in the paper's Figure 7;
- **share of time at or below each memory level:** a CDF of memory over the
  run, so averages compare at the distribution level;
- **a table:** mean over the paper's 7.5 s window, mean, median, p90, peak and
  final memory.

CI uploads the page as part of the `kv-fragmentation` artifact. On pull requests
from this repository, `post-pr-comment.sh` also posts the tables and one chart
image per panel as a PR comment, which later runs update in place. The images
live on the `ci-results` branch.

## Eviction is bimodal

How many keys survive eviction varies from run to run. LRU timestamps have
1-second resolution, so the outcome depends on whether the clock ticks
between the two inserted batches:

- **Low-key state:** the first batch looks older, and eviction removes mostly
  its 240-byte keys (Redis ~178k keys left, Valkey ~191k).
- **High-key state:** both batches look the same age, and eviction picks
  among both, leaving more and smaller keys and a much more fragmented heap
  (Redis ~255–264k, Valkey ~273–282k).

Results are therefore grouped by eviction state (`eviction state` and `n`
columns). Two options pin the state; key counts were measured on EC2:

| Option | Redis keys left | Valkey keys left |
|---|---|---|
| default (the paper's workload) | 178k ×6, 260k ×2 | 191k ×6, 273k ×2 |
| `--populate-gap 1.5` (wait between the batches) | always ~178k | always ~191k |
| `--policy allkeys-random` | always ~264k | always ~282k |

CI checks the deterministic low-key workload (`--populate-gap 1.5`). It also
reports the high-key state, without a check, using a longer window.

In the high-key state Mesh is limited by meshability, not by the mesh cap. On
EC2, with a 60 s window, it levels off at ~185 MiB (Redis) and ~169 MiB
(Valkey) whether `vm.max_map_count` is 65,530 or 1,048,576 (~36k meshes, ~53k
VMAs either way). Active defragmentation reaches ~115 MiB, because it can move
objects to any free slot; meshing needs non-overlapping offsets. Paced Mesh
takes about 30–40 s to level off. Unpaced Mesh gets there in 5–10 s, with
130–166 ms pauses.

## Mesh vs. active defragmentation

Redis 8.0.2 on EC2 t3.2xlarge, `vm.max_map_count` 65,530, mstat, median of 3
trials, 40 s idle window (MiB, ms):

| | low-key: Mesh | low-key: activedefrag | high-key: Mesh | high-key: activedefrag |
|---|---|---|---|---|
| mean, paper's first 7.5 s | **243.0** | 274.2 | **278.3** | 291.0 |
| mean, whole run | **158.6** | 164.1 | 216.5 | **155.3** |
| peak | 328.6 | 328.1 | 328.0 | 328.1 |
| final | 129.7 | **115.2** | 186.6 | **114.4** |
| probe latency p99 | **8.4** | 10.5 | **4.3** | 4.9 |
| probe latency p99.9 | **20.5** | 23.3 | 10.2 | **6.1** |
| server CPU while idle (s) | **3.8** | 5.9 | **3.6** | 4.5 |

- **Within the paper's 7.5 s window, Mesh uses less memory**, as in its
  Figure 7. Mesh starts compacting as soon as insertion ends; active
  defragmentation starts a few seconds later.
- **Peaks are the same**: they're set by insertion, before compaction.
- **Active defragmentation ends lower**: ~15 MiB lower in the low-key state,
  ~70 MiB lower in the high-key state.
- **Mesh's latency tail is similar or better up to p99**, and Mesh uses less
  CPU.
- **Valkey behaves the same way.**

### Why Mesh ends higher: the non-relocating limit

Where the memory goes at the end of a low-key Redis run (MiB; Mesh from
`MALLOCSTATS=2`, jemalloc from Redis's `allocator_*` statistics):

| | jemalloc + activedefrag | Mesh |
|---|---|---|
| live objects (`used_memory`) | 100 | 100 |
| free slots inside allocated spans | 3.4 | ~17 |
| other (allocator metadata, kernel memory for mappings, binary) | ~12 | ~13 |
| total | 115.2 | 129.7 |

Two spans can mesh only if no slot is occupied in both, so their combined
occupancy must be at most 100%. Free slots in spans more than half full
therefore cannot be reclaimed by meshing at any span size; only moving objects
can recover them, which is what active defragmentation does.

Of Mesh's ~17 MiB of free slots:

| Spans | Free slots | Recoverable by meshing? |
|---|---|---|
| 75–100% full (mostly 512 B objects, 8 per 4 KiB span, ~7/8 full) | 7.7 MiB | no (only 2 of 21,250 such spans meshed) |
| 50–75% full | 2.7 MiB | no |
| under 50% full, already mostly meshed | 4.5 MiB | in principle |

Plain jemalloc, which neither meshes nor moves objects, ends at 215 MiB. In
the high-key state Mesh runs out of meshable pairs (see above) and the gap is
larger for the same reason.

## Hardware counters

`--perf` attaches `perf stat` to each server from the start of insertion to
the end of the window and stores the counts in the JSON (`perf`). With no
argument it counts `dTLB-load-misses,dTLB-store-misses,cycles,instructions`;
pass a comma-separated list to choose others. On Intel,
`dtlb_load_misses.walk_active,dtlb_store_misses.walk_active` add the cycles
spent walking page tables. It needs `perf` and a PMU: EC2 exposes one on
metal and whole-socket instances, not on t3. Results for THP mode are in
`docs/thp.md`.

## Measuring memory

With `--mstat`, each server runs under [mstat](https://github.com/bpowers/mstat),
the tool used in the paper. mstat puts the server in its own memory cgroup and
records cgroup memory usage, which counts each physical page once and includes
kernel memory such as page tables, plus HugeTLB usage.

Without `--mstat`, a built-in sampler reads the same cgroup v2 counters
(`memory.current` plus `hugetlb.*.current`). On EC2 its results matched mstat
within run-to-run noise. Both methods need root. Without root, the script falls
back to PSS.

**Do not use RSS for Mesh.** A meshed physical page is mapped at several
virtual addresses, and RSS counts it once per mapping. In this benchmark, RSS
shows Mesh saving about 2% when it actually saves about 30%.

## Latency

The paper reports insertion time and time spent compacting (§6.2.2). Each
configuration is measured the same way:

- **insert**: wall time of the two `DEBUG POPULATE` commands.
- **idle CPU**: server CPU time (user + system) during the idle window. This
  covers meshing, active defragmentation, and jemalloc purging alike.
- **probe p50/p99/max**: a client sends `GET` for a random second-batch key
  every 10 ms during the idle window. Meshing (run from Mesh's `epoll_wait`
  wrapper) and active defragmentation both run on the server's main thread,
  so their pauses show up here. The probe runs in Python, which adds about a
  millisecond of its own jitter.
- **mesh passes / meshing total / longest pass**: Mesh's own accounting
  (`mesh.pass_count`, `mesh.pass_total_ns`, `mesh.pass_max_ns`; also printed
  by `MALLOCSTATS`). A pass holds every heap lock, so the longest pass is the
  longest pause meshing imposes. Passes that return early count too.

Each of `--trials` trials (default 5) runs every (server, config) once, in a
new random order. Tables report medians across trials, and the final-memory
range.

## `--check`

`--check` fails unless, for each server, Mesh meshed in every trial and its
median final memory is at least `--min-reduction` (default 15%) below
`mesh-nomesh`'s. CI runs it in the `Ubuntu_kv_fragmentation` job (3 trials).
