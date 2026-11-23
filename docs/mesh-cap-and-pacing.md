# The mesh cap and mesh pacing

## Why there is a cap

Meshing remaps a virtual span onto another span's physical pages with
`mmap(MAP_FIXED)` at a different offset of the arena's memfd. The kernel
keeps one VMA per run of virtual pages that map consecutive file pages, so a
remapped span usually becomes its own VMA, and splits its neighbor's. A
process may have at most `vm.max_map_count` VMAs. Beyond that, `mmap` fails
and Mesh aborts.

## The old cap and its problems

Mesh used to stop meshing once more than `0.33 × vm.max_map_count` pages were
meshed. That rule caused three problems:

- **Too conservative.** At the cap, a Redis process actually used 34,926 of
  65,530 mappings (53%). Adjacent remapped spans often line up and merge
  (1.51 VMAs per meshed page), and the application had only 57 other
  mappings.
- **Stale.** The meshed-page count was refreshed only when scavenging, so a
  large pass overshot the cap.
- **It stranded empty memory.** Once the cap was reached, every pass returned
  before freeing empty miniheaps, leaving 33 MiB of empty spans resident in
  the Redis benchmark.

The cap also interacts badly with bursty freeing. Redis evicts incrementally
for about 2.5 s after a bulk insert. Unpaced passes spent the whole cap during
that eviction, on spans that kept emptying afterwards. The result was about
12k meshed spans under 25% full, each still holding its share of the cap. The
cap applies to the whole process, so where it is spent matters.

## Tracking VMAs (Linux)

`MeshableArena` keeps one bit per arena page, set where a VMA starts. Arena
VMAs = 1 + boundaries.

- A span is always mapped as a unit onto consecutive file pages, so
  (re)mapping span `[a, a+n)` can only change the boundaries at `a` and `a+n`.
  `retrackVmaBoundaries` runs after every remap (`finalizeMesh`) and identity
  reset (`resetSpanMapping`).
- Page `a-1` is the last page of the preceding span and `a+n` the first page
  of the next. A meshed neighbor's file page therefore follows from its
  owner's physical span, with no per-page offset table.
- If a meshed neighbor's owner has already been freed (its span is waiting in
  `_toReset`), a boundary is assumed. The count may run high until that span
  is reset, but never low.
- `VmaTrackingTest` checks the count against `/proc/self/maps` for adjacent
  meshes (which the kernel merges), random-order meshes, chains, and
  free/reset.

A mesh that remaps a chain of `s` spans is allowed only if

    arena VMAs + 2·s + reserve ≤ vm.max_map_count

The `2·s` term is the worst case: each armed span is split off by the write
barrier and then remapped. `reserve = max(1024, vm.max_map_count / 8)` is left
for the application's own mappings. The check runs on every mesh, so a pass
cannot overshoot. Without a known `vm.max_map_count` (non-Linux), Mesh keeps
the page-based cap.

## Pacing

A mesh pass holds every heap lock, and it runs on whichever thread triggers it
(for Redis, the main thread inside `epoll_wait`). Its whole duration is
therefore a pause. Three parts of a pass had no bound:

- **The mesh loop:** up to 2,500 meshes per size class at 14–21 µs each.
- **Free-triggered `partialScavenge`/`scavenge`:** once more than 64 MiB was
  dirty, these released everything at once, about 74 ms.
- **The end-of-pass `scavenge`:** resets and releases.

Now:

- **The mesh loop** stops starting new meshes after `kMeshPassBudget` (10 ms,
  override with `MESH_PASS_BUDGET_MS`). The next pass resumes at the size
  class where it stopped, and a pass cut short counts as effective, so meshing
  continues.
- **Scavenges** stop resetting meshed spans and releasing dirty spans after
  `kScavengeBudget` (5 ms, `MESH_SCAVENGE_BUDGET_MS`). Leftovers stay queued
  and remain off the clean lists.
- **Frees** trigger at most one scavenge per 10 ms, and none during a pass.
- **Every pass frees empty miniheaps first**, before the "ineffective" and
  "at the cap" early returns.

## Meshing only settled spans

A time budget alone made the cap's spend rate depend on CPU speed. On a faster
machine, each 10 ms pass did more meshes during the post-insert eviction,
reached the cap mid-eviction, and ended 40–60 MiB higher.

A first fix deferred whole passes while many miniheaps were being freed. That
signal missed Valkey, whose eviction frees in bursts on a background thread
(`lazyfree-lazy-eviction yes`).

Instead, each pass records every partial span's in-use count, in a side table
indexed by miniheap ID. A pair is meshed only if both spans are unchanged
since they were last seen, so spans that are still emptying wait until they
settle.

- **Starvation guard:** if a size class has had only unsettled pairs for 5 s,
  they are meshed anyway, so constant churn can't starve meshing.
- **Deferral keeps meshing on:** a pass that deferred pairs counts as
  effective, so passes continue after the application stops freeing.
- **No fast-path cost:** nothing is added to malloc/free.

## Benchmark caveat: Valkey's eviction is bimodal

Valkey ends the benchmark with either ~191k or ~273k keys (same 100 MB of
data), in every configuration. The 273k-key state is more fragmented:

| | ~191k keys | ~273k keys |
|---|---|---|
| jemalloc | ~224 MiB | ~298 MiB |
| Mesh | ~128 MiB | ~200 MiB, still dropping at 15 s |
| jemalloc + activedefrag | ~113 MiB | ~113 MiB |
