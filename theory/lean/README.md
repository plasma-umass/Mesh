# Mesh theory in Lean

Lean 4 with Mathlib (`v4.36.0`), toolchain in `lean-toolchain`.  Build with
`lake exe cache get && lake build`.  The combinatorial files use only Lean
core; the probabilistic ones (`Prob/`) use Mathlib.

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
| Lemma 5.3: the bound on matches | `Prob.lemma53_expectation`, `Prob.lemma53_exp` | **repaired argument, in expectation, fully formal**, see below |
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

**Repaired argument** (`Prob/Lemma53.lean`).  The setting is `h` left and `h`
right vertices, independent edges with probability `q`, and SplitMesher as
Mesh implements it (window `i … i+t-1` mod `h`).  The probability space is the
finite set of graphs with product weights.  It proves
`E[M]/h ≥ (1−(1−q)^t)/(2−(1−q)^t)`, and `≥ (1−e^{−k})/(2−e^{−k})` once
`t ≥ k/q`.  `repaired_vs_paper` places this between `(1−e^{−k})/2` and the
paper's `(1−e^{−2k})/2`; all three tend to 1/2.

1. `takenAt_congr`: the vertices taken before `u_i` depend only on rows `< i`.
2. `expect_row_avoid` (row independence): `P(u_i unmatched) = E[(1−q)^(free_i)]`.
3. `chord`: `(1−q)^(t−T) ≤ (1−q)^t + (T/t)(1−(1−q)^t)`, by convexity.
4. `sum_window_inter`, `card_windows_containing`: every right vertex is in
   exactly `t` windows, so `Σ_i T_i ≤ t M`.
5. `matches_bound`: combining gives the bound.

Extensions, also fully formal:
- **`lemma53_hetero`** (`Prob/Hetero.lean`): the same bound when each pair
  meshes independently with its own probability `p i j ≥ q`.  This is the
  setting of hedged placement and of size classes whose spans differ in
  occupancy.
- **`lipschitz`** (`Prob/Lipschitz.lean`): changing one left span's row changes
  the match count by at most 1.  That is the bounded-differences hypothesis
  for McDiarmid or Azuma–Hoeffding (Mathlib has the latter in measure form),
  which would give the high-probability version.  Connecting this finite model
  to Mathlib's filtrations is not done.
- **The string model** (`Star.lean`, `StringModel.lean`).  In the paper's
  model the spans are random strings, not independent edges.
  - `star_none_mesh`: `m` fresh partners all fail against a fixed span with
    probability exactly `(1−q)^m`, as if independent.
  - `step_fails`: an exhaustive counterexample (b = 4, 1,296 configurations)
    to the independent-edge step.  After earlier failures, `P(u_i unmatched)`
    exceeds `E[(1−q)^(free_i)]` (20/27), because right spans that failed
    against the same left span are correlated.
  - `bound_holds_small`: the bound itself still holds there.
  So `lemma53_expectation` is proved for independent edges; for strings the
  paper's statement remains unproved.
- **`filter_sound`, `qu_antitone`** (`Occupancy.lean`): Mesh's occupancy
  filter (probe only partners with `r + r' ≤ b`) never skips a pair that could
  mesh, and lower-occupancy partners mesh more often.

The Nat versions (`good_match_found`, `taken_in_windows`, `chord_bound`,
`lemma53_core` in `SplitMesher.lean`) are core-only.

Still open:
- **High probability.** The paper's Chernoff argument needs independent
  indicators; here they are dependent, and changing one row can cascade, so
  bounded differences doesn't apply directly.
- **The string model.** In it, edges are independent only on acyclic sets
  (the paper's caveat as well).

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
| `SplitMesher.lean` | §5.3 (algorithm, good matches, core-only pieces) |
| `Prob/Bounds.lean`, `Prob/Lemma53.lean` | §5.3 in expectation (Mathlib) |
| `Prob/Hetero.lean`, `Prob/Lipschitz.lean`, `Occupancy.lean` | extensions above |

General theorems use only `propext`, `Classical.choice` and `Quot.sound`. The
exhaustive checks (`native_decide`) also trust the compiler.
