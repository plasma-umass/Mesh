# THP mode: Mesh on transparent huge pages

THP mode puts Mesh's arena on 2 MiB transparent huge pages (THP) wherever
that doesn't cost meshing. Meshing still works at 4 KiB, as before. Memory
that stays dense keeps its huge pages; memory that fragments gets meshed and
falls back to 4 KiB pages, one 2 MiB region at a time.

On Redis and Valkey it cuts data-TLB misses by 83–94% and server CPU cycles
by 7–10%, for 1–5 MiB of extra memory (see [Results](#results)).

## Turning it on

THP mode is on by default wherever the kernel gives huge pages to a
madvised memfd: when `/sys/kernel/mm/transparent_hugepage/shmem_enabled`
selects `advise`, `always`, `within_size` or `force`. `MESH_THP=1` forces it
on and `MESH_THP=0` off. (With `always` or `force` the kernel uses huge
pages even with `MESH_THP=0`, just without the zones and protect rule.)

Many distributions (AL2023 among them) default to `never`, which leaves
Mesh on 4 KiB pages. To allow huge pages:

```sh
echo advise | sudo tee /sys/kernel/mm/transparent_hugepage/shmem_enabled
```

## The algorithm

### Huge-page regions

Mesh's arena is a `MAP_SHARED` mapping of a memfd; meshing remaps virtual
pages onto other file pages. THP mode maps it 2 MiB aligned and marks it
`MADV_HUGEPAGE` (and again after `fork`, which remaps the arena). The kernel
then backs each 2 MiB **region** with one huge page on first touch.

A huge page survives until a 4 KiB page inside it is punched (returned to
the OS) or remapped (meshed). Either one splits that region's huge page only,
and touching the pages again doesn't restore it. So the problem is choosing
which regions meshing and scavenging may split.

Mesh tracks, per region:

- **used pages:** pages held by live spans (added in `pageAlloc`, removed
  when a span is freed);
- **touched:** some page has been allocated;
- **split:** some page was punched (`freePhys`) or remapped (`finalizeMesh`).

A region is **intact** if touched and not split. The kernel's own count
(`ShmemPmdMapped` in `/proc/self/smaps_rollup`) matches Mesh's intact count
in every run measured.

### Zones: keep dense memory together

Spans are placed in one of two zones, each made of whole regions:

| Zone | Holds | Why |
|---|---|---|
| 1 | spans of size classes ≥ 512 B, and large allocations | few objects per span, so spans stay full or empty as a whole; regions stay dense and rarely need meshing |
| 0 | size classes < 512 B | many objects per span; this is where fragmentation and meshing happen |

The arena grows by whole regions tagged with a zone, each zone has its own
clean and dirty free-span lists, and scavenge never joins free runs across a
zone boundary. With THP off everything is zone 0, as before.

### Protect rule: mesh around dense huge pages

Meshing a pair keeps `dst`'s physical pages and remaps every span of `src`
onto them, splitting `src`'s regions. Before meshing a pair, Mesh checks
whether any of `src`'s spans lies in a **dense intact region** (at least
`MESH_THP_PROTECT` of its pages used, default 0.9):

- if not, mesh as usual;
- if so and `dst` isn't in one, swap them, splitting `dst`'s region instead;
- if both are, the pair can't be meshed without splitting one. The mesh
  search (SplitMesher) rejects such pairs, so each span keeps probing for a
  partner outside a dense region. A pair that becomes both-dense after the
  search is skipped (counted as *pairs protected*).

Regions below the threshold are fair game: they are mostly empty, so meshing
them saves more memory than their huge page is worth. `MESH_THP_PROTECT=0`
turns the rule off.

### Remapped pages stay off huge pages

Meshing remaps virtual pages with `mmap(MAP_FIXED)`, and so does restoring a
freed meshed span to its own file pages. The new mapping doesn't carry
`MADV_HUGEPAGE`, and Mesh leaves it that way. Otherwise the kernel's
`khugepaged` would collapse split regions back into huge pages, allocating
zeroed pages for the holes meshing and scavenging had released.

The cost is VMAs: a remapped range can't merge with its advised neighbors.
Mesh's VMA count (which caps meshing below `vm.max_map_count`) keeps one bit
per page for "remapped", and counts a boundary wherever it changes as well as
wherever file pages stop being consecutive.

### Where the extra memory goes

The kernel makes a region's whole 2 MiB resident at its first touch. The
arena fills regions in order, so each zone's newest region is partly used
but fully resident. That is up to 2 MiB per zone, 1.4–1.8 MiB in total in
the Redis/Valkey runs, and doesn't grow with the heap. The rest of the gap
to plain Mesh (up to about 2 MiB) is memory in regions the protect rule
keeps intact, which then isn't meshed.

Measured on EC2 t3.2xlarge (median of 3 trials), THP mode against plain
Mesh:

| | final | mean over the run | peak |
|---|---|---|---|
| Redis, low-key | +1.1 MiB | +0.6 MiB | +0.7 MiB |
| Valkey, low-key | +3.2 MiB | +3.5 MiB | +2.3 MiB |
| Redis, high-key | +2.0 MiB | −2.9 MiB | +0.9 MiB |
| Valkey, high-key | +3.8 MiB | +1.6 MiB | +2.4 MiB |

### Why not mesh whole huge pages

The first design meshed whole huge pages, each a bitmap of chunks: chunks
chosen at random, filled densely, freed whole. A simulator replaying
Redis/Valkey allocation traces found no meshable pairs at any chunk count from 8 to 512 per huge page.
Keys that survive LRU eviction are scattered, so almost every chunk keeps at
least one live object; and two huge pages mesh only if no chunk is live in
both. The hybrid keeps 4 KiB meshing for the memory work and puts huge pages
wherever memory stays dense.

## Statistics

`MALLOCSTATS=1` prints a `THP:` line: regions, intact and split regions (with
MiB in use), the protect threshold, pairs protected, and the kernel's
huge-page-mapped total. `mallctl("mesh.thp_intact_regions")` returns the
intact count.

## Results

`bench/kv` (Redis 8.0.2, Valkey 9.1.0), EC2 m7i.metal-24xl, kernel 6.1, mstat,
median of 3 trials, counters from insertion to the end of the 40 s window:

```sh
sudo python3 bench/kv/fragmentation.py --libmesh build/lib/libmesh.so --mstat ~/go/bin/mstat \
  --trials 3 --settle 40 --configs mesh,mesh-thp \
  --perf dTLB-load-misses,dTLB-store-misses,dtlb_load_misses.walk_active,dtlb_store_misses.walk_active,cycles,instructions \
  --populate-gap 1.5          # low-key state; --policy allkeys-random for high-key
```

| | dTLB load misses | dTLB store misses | cycles walking page tables | server cycles | IPC |
|---|---|---|---|---|---|
| Redis, low-key | 13.6M → 1.3M (**−91%**) | −85% | 9.1% → 1.7% | **−7%** | 1.76 → 1.85 |
| Valkey, low-key | 6.2M → 1.0M (**−83%**) | −85% | 5.9% → 1.4% | **−9%** | 1.94 → 2.09 |
| Redis, high-key | 8.8M → 0.57M (**−94%**) | −78% | 7.3% → 1.1% | **−9%** | 1.76 → 1.87 |
| Valkey, high-key | 3.5M → 0.49M (**−86%**) | −75% | 4.5% → 1.0% | **−10%** | 1.95 → 2.09 |

Memory at the end of the window, and the part mapped with 2 MiB pages:

| | Mesh | Mesh + THP |
|---|---|---|
| Redis, low-key (dense) | 129.9 MiB, 0 | 131.2 MiB, 96 MiB on huge pages |
| Valkey, low-key | 128.7 MiB, 0 | 130.8 MiB, 94 MiB |
| Redis, high-key (fragmented) | 188.5 MiB, 0 | 190.5 MiB, 66 MiB |
| Valkey, high-key | 171.8 MiB, 0 | 176.8 MiB, 80 MiB |

- **Memory:** +1.3 to +2.1 MiB, except Valkey high-key at +5 MiB. An earlier
  run on t3.2xlarge gave +1.2 to +3.3 MiB.
- **Latency:** probe p99 is about the same (1.1–1.3 ms in both); insertion is
  4–22% faster with THP.
- **Fragmented heaps gain too:** with a third to a half of memory on huge
  pages, the high-key state sees the largest cut in misses, because the
  regions kept intact are the dense, frequently used ones.
- Plain Mesh has nothing on huge pages: the arena isn't advised.
