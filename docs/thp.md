# THP mode: Mesh on transparent huge pages

`MESH_THP=1` backs Mesh's arena with 2 MiB transparent huge pages where that
doesn't cost meshing.

## How it works

- **Arena:** 2 MiB aligned and `MADV_HUGEPAGE`. The kernel backs each 2 MiB
  region with one huge page until a page in it is punched (memory returned)
  or remapped (meshed). Either splits only that region's huge page.
- **Two zones:** spans of size classes of 512 B and up, and large allocations,
  get their own regions (zone 1). These tend to stay dense, which keeps their
  huge pages intact. Smaller classes use zone 0. The arena grows in whole
  regions, and free-span lists never mix zones.
- **Protect rule:** a mesh that would split an intact region at least 90% full
  (`MESH_THP_PROTECT`) is done the other way round if the other side isn't in
  one, and skipped otherwise.
- **Statistics:** `MALLOCSTATS` prints intact and split regions, protected
  pairs, and the kernel's huge-page-mapped total (`ShmemPmdMapped`).
  `mallctl("mesh.thp_intact_regions")` reports the intact count.

## Requirement

Shmem THP must be enabled for `madvise`:

```sh
echo advise | sudo tee /sys/kernel/mm/transparent_hugepage/shmem_enabled
```

Many distributions (AL2023 among them) default to `never`, and then the
arena simply stays on 4 KiB pages.

## Why this design

Phase 0 simulated meshing whole huge pages, with chunk-level bitmaps (see
`bench/hugepage/README.md`). On Redis and Valkey it never meshes: LRU
survivors keep almost every chunk non-empty. Today's 4 KiB meshing does the
memory work; THP adds huge pages wherever memory stays dense.

## Results

`bench/kv`, EC2 t3.2xlarge, kernel 6.1, mstat, median of 3 trials, 40 s
window. Final memory, with the part the kernel maps with 2 MiB pages:

| | Mesh | Mesh + THP |
|---|---|---|
| Redis, low-key (dense) | 129.9 MiB, 0% | 131.1 MiB, **73%** |
| Valkey, low-key | 128.0 MiB, 0% | 130.3 MiB, **72%** |
| Redis, high-key (fragmented) | 186.6 MiB, 0% | 188.3 MiB, **38%** |
| Valkey, high-key | 169.7 MiB, 0% | 172.9 MiB, **43%** |

- **Means over the run** are within ±2 MiB of plain Mesh.
- **Latency:** p99 is 2.1–4.3 ms with THP against 2.4–4.6 ms without; p99.9
  is about the same.
- **TLB misses weren't measured:** the t3 instance has no hardware TLB
  counters.
