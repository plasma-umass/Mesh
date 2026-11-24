# Mesh theory in Lean

Lean 4 core only (no Mathlib), toolchain in `lean-toolchain`.  Build with
`lake build`.

## The paper's §5 (PLDI '19), result by result

| Paper | Lean | Notes |
|---|---|---|
| Definition 5.1: strings mesh iff disjoint; a set meshes iff pairwise | `meshes`, `meshesAll`, `meshesAll_pairwise` | |
| Meshing keeps objects at their offsets; meshing `k` strings releases `k − 1` | `merge`, `merge_ones`, `meshes_merge`, `releases`, `validMeshing` | |
| Problem 1 as clique cover of the meshing graph | `meshGraph`, `meshesAll_pairwise` | |
| Theorem 5.2, structural facts: each `N_i` is a clique of the complement graph; the zero string meshes with everything | `not_meshes_self`, `zero_meshes_all` | the polynomial-time enumeration itself is not formalized |
| Random model: `P(two strings mesh) = C(b−r₁, r₂)/C(b, r₂)` | `uniform_vs_any`, `choose_symm` | stronger than the paper: holds against **any** fixed partner |
| Observation 1: edges aren't 3-wise independent | `observation1` | exhaustive for b = 4, two ones each |
| §5.2 triangle probability `C(b−r₁,r₂)/C(b,r₂) · C(b−r₁−r₂,r₃)/C(b,r₃)` | `trianglePairs_eq` | proved for all `b` by counting |
| §5.2 example: b = 32, r = 10, n = 1000: under 2 triangles, against 167 if independent | `paper_triangle_example` | |
| SplitMesher (Figure 2) | `firstFree`, `run`, `splitMesher` | |
| Lemma 5.3: every good match is found | `good_match_found` | with condition (3) corrected; `printed_condition_insufficient` is a counterexample to the printed one |
| Lemma 5.3: the bound on matches | `taken_in_windows`, `chord_bound`, `lemma53_core` | **repaired argument**, see below |
| The random-placement assumption, for Mesh's actual shuffle vector | `shuffle_vector_uniform`, `unshuffled_not_uniform` | exhaustive, 4 slots, all sequences up to 10 ops |

### Lemma 5.3

The proof calls `(u_i, v_j)` good if (1) the edge is present, (2) `u_i` has no
edge to `v_j'` for `i ≤ j' < j`, and (3) "no edges between `u_i'` and `v_j` for
`i < i' ≤ j`".

- **Condition (3) points the wrong way.** What keeps `v_j` free for `u_i` is
  that no *earlier* `u_i'` (`j − t < i' < i`) took it.
  `printed_condition_insufficient` is a two-vertex counterexample to the
  printed condition, and `good_match_found` proves that the corrected one
  suffices.
- **With the correction the counting breaks.** A good match at offset `d`
  has probability `q(1−q)^(t−1)`, not `q(1−q)^(2d)`. So the argument yields
  only `t q (1−q)^(t−1) ≈ k e^{−k}`, which vanishes as `k` grows.
- **The stated bound still holds empirically.** SplitMesher's matched fraction
  is 0.34, 0.52, 0.72 and 0.88 for k = ½, 1, 2 and 4, against the claimed
  (1−e^{−2k})/2 = 0.32, 0.43, 0.49 and 0.50 (`theory/colour/lemma53.py`).

**Repaired argument**, giving `E[M]/(n/2) ≥ (1−(1−q)^t)/(2−(1−q)^t) ≥
(1−e^{−k})/(2−e^{−k})`.  That is close to the paper's bound and also tends to
1/2.

1. `taken_in_windows`: each right vertex is in at most `t` windows, so
   `Σ_i T_i ≤ t M`, where `T_i` counts taken vertices in `u_i`'s window.
   `run_taken_mono` shows taken sets only grow.
2. *(modelling step)* Edges are revealed only when probed, and each pair at
   most once, so `u_i` stays unmatched with probability `(1−q)^(t−T_i)`.  This
   holds for independent edges.  Mesh's strings are independent only on
   acyclic edge sets, the same caveat as the paper's.
3. `chord_bound`: `(1−q)^(t−T) ≤ (1−q)^t + (T/t)(1−(1−q)^t)` (convexity).
4. `lemma53_core`: summing gives the bound.

The high-probability part (Chernoff and union bounds) isn't formalized; it
would need Mathlib.

## Extensions: colour placement

`Colour.lean`, `Hedged.lean`; see `docs/colour-placement.md`.

| File | Contents |
|---|---|
| `Choose.lean`, `ChooseFast.lean` | `choose` (Pascal), strings, `chooseFast = choose` |
| `Uniform.lean` | `uniform_vs_any` |
| `Hedged.lean` | `choose_symm`, `hedged_pair_bound`, `hedged_population_bound` |
| `Colour.lean` | `canonical_dominates`, oblivious-adversary counterexamples |
| `Shuffle.lean` | `shuffle_vector_uniform` |
| `Paper.lean` | §5.1–5.2 |
| `SplitMesher.lean` | §5.3 |

General theorems use only `propext`, `Classical.choice` and `Quot.sound`. The
exhaustive checks (`native_decide`) also trust the compiler.
