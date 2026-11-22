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

`mesh-nomesh` keeps Mesh's allocator and randomization and only disables meshing.

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

## `--check`

`--check` fails unless, for each server, Mesh performed meshes and its final
memory is at least `--min-reduction` (default 15%) below `mesh-nomesh`. CI runs
it in the `Ubuntu_kv_fragmentation` job.
