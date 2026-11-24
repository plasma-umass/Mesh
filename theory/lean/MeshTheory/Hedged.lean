import MeshTheory.Uniform
/-!
# Hedged placement keeps a constant fraction of Mesh's guarantee

Symmetry: counting disjoint pairs `(s, t)` (with `r` and `r'` ones) both ways
gives `choose b r' * choose (b - r') r = choose b r * choose (b - r) r'`, so a
uniformly placed span meshes with an independent partner with the same
probability whichever of the two is the uniform one.

Hedged placement: each span independently uses uniform placement with
probability `α = a / d`, otherwise any other policy (colour placement, or one
an adversary steers).  A pair meshes with probability exactly `q_u` whenever
at least one of the two is uniform, and with some probability `q_cc ≥ 0`
otherwise, so the pair's mesh probability is at least `(1 - (1 - α)^2) q_u`.
By linearity this carries over to the expected number of meshable pairs of any
population, and so to the global edge density `q` in Lemma 5.3.
-/

namespace MeshTheory

private theorem sum_map_zero (M : List (List Bool)) : (M.map fun _ => (0 : Nat)).sum = 0 := by
  induction M with
  | nil => rfl
  | cons _ M ih => simp [ih]

private theorem sum_map_add (M : List (List Bool)) (f g : List Bool → Nat) :
    (M.map fun s => f s + g s).sum = (M.map f).sum + (M.map g).sum := by
  induction M with
  | nil => rfl
  | cons x M ih => simp only [List.map_cons, List.sum_cons, ih]; omega

private theorem sum_indicator (M : List (List Bool)) (P : List Bool → Bool) :
    (M.map fun s => if P s then 1 else 0).sum = (M.filter P).length := by
  induction M with
  | nil => rfl
  | cons x M ih =>
    simp only [List.map_cons, List.sum_cons, List.filter_cons]
    cases h : P x <;> simp [ih] <;> omega

/-- `Σ_{t ∈ L} |{s ∈ M : R s t}| = Σ_{s ∈ M} |{t ∈ L : R s t}|` (double counting). -/
theorem count_swap (R : List Bool → List Bool → Bool) :
    ∀ (L M : List (List Bool)),
      (L.map fun t => (M.filter fun s => R s t).length).sum =
      (M.map fun s => (L.filter fun t => R s t).length).sum
  | [], M => by simp only [List.map_nil, List.sum_nil, List.filter_nil, List.length_nil]; rw [sum_map_zero]
  | t0 :: L, M => by
    rw [List.map_cons, List.sum_cons, count_swap R L M]
    have : (M.map fun s => ((t0 :: L).filter fun t => R s t).length) =
        M.map fun s => (if R s t0 then 1 else 0) + (L.filter fun t => R s t).length := by
      apply List.map_congr_left
      intro s _
      simp only [List.filter_cons]
      cases R s t0 <;> simp; omega
    rw [this, sum_map_add, sum_indicator]

theorem meshes_comm : ∀ s t : List Bool, meshes s t = meshes t s
  | [], [] => rfl
  | [], _ :: _ => rfl
  | _ :: _, [] => rfl
  | a :: s, b :: t => by simp [meshes, meshes_comm s t, Bool.and_comm]

private theorem sum_const (L : List (List Bool)) (f : List Bool → Nat) (c : Nat)
    (h : ∀ x ∈ L, f x = c) : (L.map f).sum = L.length * c := by
  induction L with
  | nil => simp
  | cons x L ih =>
    simp only [List.map_cons, List.sum_cons, List.length_cons]
    rw [h x (by simp), ih (fun y hy => h y (by simp [hy]))]
    rw [Nat.succ_mul]; omega

private theorem mem_withOnes {n r : Nat} {t : List Bool} (h : t ∈ withOnes n r) :
    t.length = n ∧ ones t = r := by
  unfold withOnes at h
  rw [List.mem_filter] at h
  exact ⟨mem_strings_length h.1, by simpa using h.2⟩

/-- Pairs `(s, t)` of length-`b` strings with `r` and `r'` ones that mesh, as a
sum over `t` of the number of `s` meshing with it. -/
def disjointPairs (b r r' : Nat) : Nat :=
  ((withOnes b r').map fun t => ((withOnes b r).filter fun s => meshes s t).length).sum

private theorem filter_withOnes_eq_avoiding (t : List Bool) (r : Nat) :
    ((withOnes t.length r).filter fun s => meshes s t).length = (avoiding t r).length := by
  unfold withOnes avoiding
  rw [List.filter_filter]
  congr 1
  apply List.filter_congr
  intro s _
  cases meshes s t <;> simp

theorem disjointPairs_eq (b r r' : Nat) :
    disjointPairs b r r' = choose b r' * choose (b - r') r := by
  unfold disjointPairs
  rw [sum_const _ _ (choose (b - r') r), withOnes_length]
  intro t ht
  obtain ⟨hl, ho⟩ := mem_withOnes ht
  have := filter_withOnes_eq_avoiding t r
  rw [hl] at this
  rw [this, avoiding_length, hl, ho]

/-- **Symmetry of the uniform mesh probability.** -/
theorem choose_symm (b r r' : Nat) :
    choose b r' * choose (b - r') r = choose b r * choose (b - r) r' := by
  rw [← disjointPairs_eq, ← disjointPairs_eq]
  unfold disjointPairs
  rw [count_swap]
  congr 1
  apply List.map_congr_left
  intro s _
  congr 1
  apply List.filter_congr
  intro t _
  rw [meshes_comm]

/-- The uniform pair mesh probability `q_u = choose (b - r') r / choose b r`, as
a numerator and a denominator. -/
def quNum (b r r' : Nat) : Nat := choose (b - r') r
def quDen (b r : Nat) : Nat := choose b r

/-- Cross-multiplied, `q_u(r, r') = q_u(r', r)`: it doesn't matter which span of
the pair is the uniform one. -/
theorem qu_symm (b r r' : Nat) :
    quNum b r r' * quDen b r' = quNum b r' r * quDen b r := by
  unfold quNum quDen
  have := choose_symm b r r'
  rw [Nat.mul_comm (choose (b - r') r), Nat.mul_comm (choose (b - r) r')]
  exact this

/-- **Hedged placement bound**, for one pair.  With `α = a / d`, write the
pair's mesh probability as `P = N / (d^2 * D)` where the four policy cases
(both uniform, one uniform -- twice --, neither) have weights `a^2`, `a (d - a)`
twice, `(d - a)^2`.  In the first three the conditional probability is
`q_u = U / D` (`uniform_vs_any`, `qu_symm`); in the last it is some `C / D`.
Then `N ≥ (d^2 - (d - a)^2) U`: at least a `1 - (1 - α)^2` fraction of the
uniform-placement probability, whatever the other policy does. -/
theorem hedged_pair_bound (a d U C : Nat) (ha : a ≤ d) :
    let N := a * a * U + a * (d - a) * U + (d - a) * a * U + (d - a) * (d - a) * C
    (d * d - (d - a) * (d - a)) * U ≤ N := by
  intro N
  have hsq : d * d = a * a + a * (d - a) + (d - a) * a + (d - a) * (d - a) := by
    have : d = a + (d - a) := by omega
    conv => lhs; rw [this]
    simp only [Nat.add_mul, Nat.mul_add]
    omega
  have : d * d - (d - a) * (d - a) = a * a + a * (d - a) + (d - a) * a := by omega
  rw [this]
  simp only [Nat.add_mul]
  exact Nat.le_add_right _ _

/-- Over a whole population the bound is linear: if every pair's numerator is
at least `w * U_i`, the total is at least `w * Σ U_i`. -/
theorem hedged_population_bound (w : Nat) :
    ∀ (pairs : List (Nat × Nat)), (∀ p ∈ pairs, w * p.1 ≤ p.2) →
      w * (pairs.map Prod.fst).sum ≤ (pairs.map Prod.snd).sum
  | [], _ => by simp
  | p :: ps, h => by
    simp only [List.map_cons, List.sum_cons, Nat.mul_add]
    exact Nat.add_le_add (h p (by simp))
      (hedged_population_bound w ps (fun q hq => h q (by simp [hq])))

end MeshTheory
