# Colour placement: more meshing without losing Mesh's guarantees

Mesh's guarantees (PLDI '19, §5) rest on random placement: a span with `r`
objects is a uniformly random `r`-subset of its `b` slots, so two spans mesh
with probability `q_u = C(b - r', r) / C(b, r)`, and SplitMesher finds a
near-half-optimal matching whenever `q` isn't negligible.  That's also where
Mesh stops working.  Two half-full 64-slot spans mesh with probability
`1 / C(64, 32) ≈ 5e-19`; in the Redis/Valkey high-key runs most stranded
memory sits in spans 25–50% full.

**Colour placement**: a span picks one half of its slots at random (its colour)
and fills free slots of that half first, uniformly at random, then the other
half.  Two spans at most half full with opposite colours always mesh.

This note covers what can be proved about colour placement, what can't, and the
design this leads to.  The proofs are in [`theory/lean`](../theory/lean) (Lean 4
core, no Mathlib). The simulation is in
[`theory/colour/colour_sim.py`](../theory/colour/colour_sim.py).

## The paper's assumptions, made explicit

1. **Uniform given occupancy.** A span's occupied set, given its size, is
   uniform.  This holds for Mesh as built (`RANDOMIZATION=1`: shuffle when the
   shuffle vector is filled, reuse freed slots last in, first out) against an
   oblivious adversary, by symmetry.  `shuffle_vector_uniform` checks it
   exhaustively for a 4-slot span: all 24 initial orders, against all 13,232
   oblivious operation sequences of up to 10 steps.  `unshuffled_not_uniform`
   shows the check catches a non-uniform policy.
2. **Pair probability.** `uniform_vs_any` (proved for all `b`): a uniformly
   placed span with `r` objects avoids **any fixed** partner with `r'` ones in
   exactly `C(b - r', r)` of its `C(b, r)` placements.  `choose_symm` gives
   `C(b, r') C(b - r', r) = C(b, r) C(b - r, r')`, so it doesn't matter which
   span of the pair is the uniform one.
3. **Independence across spans**, used by Lemma 5.3 for acyclic edge sets.
   This needs `P(mesh | one span's string)` to be the same for every string in
   the support.  By (2) that holds for uniform placement.

The paper also leaves out one dependence, and so does this note: Mesh's own
meshing decisions depend on positions, so a span that survived unmeshed is
conditioned on that.

## What holds and what doesn't for colour placement

| Result | Lean | Status |
|---|---|---|
| **Canonical dominance**: in the canonical shape (own half first), `q_c ≥ q_u` for every pair of occupancies | `canonical_dominates` | exhaustive over Mesh's even slot counts (8–256) |
| Half-full 64-slot spans: `q_c = 1/2` against `q_u = 1/C(64,32)` | `half_full_64` | |
| **Odd slot counts** (85, 51, 25, 21, 9 with 4 KiB pages): halves can't be equal, and with a leftover slot dominance fails (b = 85, one object each: 83/84 < 84/85) | (doc) | keep uniform for odd `b` |
| **History breaks dominance**: an oblivious adversary that frees by allocation order reaches spans that never mesh under colour placement, where uniform placement gives 3/14 | `reach_0_4`, `reach_1_1`, `reachable_counterexample` | |
| **…and the global `q`**: a population (b = 16, 10% at profile (1,1), 90% at (1,8)) where colour placement meshes less than a fifth as often as uniform | `reach_1_8`, `population_counterexample` | |
| **Hedged placement bound**: each span uniform with probability `α`, else colour (or anything else): every pair, and so the global `q`, is at least `(1 - (1-α)²) q_u` | `hedged_pair_bound`, `hedged_population_bound` | proved for all `b`, any other policy, any history |

Colour placement's weak spot is that it ties a slot to the time of allocation.
The first allocations of a span always land in its own half, so an adversary
that frees the oldest objects controls the span's shape.  Uniform placement
has no such link.  That is why a uniformly placed span meshes with any partner
at exactly `q_u` (`uniform_vs_any`), and why mixing in uniform spans protects
the guarantee.

## Design: hedged colour placement

- **Policy:** each span, when it is created, is uniform with probability `α`
  and coloured otherwise; spans with odd `b` are always uniform.
- **Guarantee:** at least `(2α - α²) q_u` per pair, whatever the workload.
  With `α = 1/2` that is `3/4 q_u`.  Lemma 5.3 then goes through with
  `t = k/q` probes scaled by `1/(2α - α²)`.  Restated for heterogeneous edge
  probabilities, it is a remaining proof obligation (see below).
- **Gain:** a coloured pair, each at most half full, with opposite colours
  always meshes.

### Simulation

[`colour_sim.py`](../theory/colour/colour_sim.py): 3,000 spans per run, each
filled to a random count between half and full, then freed down to a target
occupancy at random, oldest first (LRU-like, the adversarial pattern for
colour placement) or newest first.  Then SplitMesher (`t = 64`) runs until a
pass finds nothing.  The table shows spans released, as a share of the ideal
(total objects / `b` spans), mean of 2 trials:

| b | occupancy | frees | uniform | colour | hedged, α = ½ |
|---|---|---|---|---|---|
| 16 | 50% | random | 10% | 32% | 20% |
| 16 | 50% | oldest | 11% | 45% | 24% |
| 32 | 40% | random | 9% | 31% | 18% |
| 32 | 40% | oldest | 11% | 46% | 26% |
| 32 | 50% | newest | 0% | 98% | 48% |
| 64 | 40% | oldest | 0% | 27% | 13% |
| 64 | 50% | newest | 0% | 97% | 49% |
| 64 | 10–60% | oldest | 31% | 46% | 38% |
| 8 | any | any | 78–96% | 79–99% | 79–96% |
| any | 25% | any | 60–96% | 61–97% | 60–96% |

- **Where uniform placement already meshes** (b = 8, or 25% full), colour
  placement matches it.
- **Where uniform placement can't** (b ≥ 16, 40–50% full), colour placement
  releases up to 98% of the ideal, and hedged placement about half that.
- **Colour placement was never worse than uniform by more than one point**
  (noise), including with oldest-first frees.  The adversarial populations above are possible but
  rare; hedging is insurance against them, and `α` trades gain for the
  worst-case bound.

## Open obligations

- **Lemma 5.3 with heterogeneous edge probabilities.** Hedged populations have
  pairs at `q_u` and pairs at other values.  The good-match argument needs a
  lower bound on each edge's probability and independence for acyclic sets.
  Edges incident to a uniform span satisfy both; coloured–coloured edges
  satisfy the lower bound only if their shapes are canonical.  The clean
  statement is the paper's lemma applied to the uniform subpopulation, a
  fraction `α`; the mixed statement is still to be written.
- **Multiple spans per shuffle vector, and refills**, are relabelling-invariant
  but are not modelled in `Shuffle.lean`.
- **Real traces:** the Redis/Valkey traces (`bench/hugepage`, branch
  `hugepage-sim`) are on the EC2 dev box; replaying them with colour placement
  is the next check before implementing.
