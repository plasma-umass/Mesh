# Mesh theory in Lean

Lean 4 core only (no Mathlib), toolchain in `lean-toolchain`.  Build with
`lake build`.  See `docs/colour-placement.md` for what the results mean.

| File | Contents |
|---|---|
| `Choose.lean` | `choose` (Pascal), spans as `List Bool`, `meshes`, `strings n` |
| `ChooseFast.lean` | `chooseFast = choose` (the multiplicative formula used for computation) |
| `Uniform.lean` | `uniform_vs_any`: a uniformly placed span avoids any fixed partner with `r'` ones in exactly `choose (b - r') r` of its `choose b r` placements |
| `Hedged.lean` | `choose_symm` (double counting), `hedged_pair_bound`, `hedged_population_bound` |
| `Colour.lean` | colour placement: `canonical_dominates` (exhaustive over Mesh's even slot counts), the oblivious-adversary counterexamples |
| `Shuffle.lean` | `shuffle_vector_uniform`: Mesh's LIFO shuffle vector keeps occupancy uniform (4 slots, all sequences up to 10 operations), with a negative control |

General theorems use only `propext`, `Classical.choice` and `Quot.sound`. The
exhaustive checks (`native_decide`) also trust the compiler.
