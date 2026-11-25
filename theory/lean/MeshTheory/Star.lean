import MeshTheory.Uniform
/-!
# Star independence in the string model

The paper's Lemma 5.3 proof uses that mesh events on acyclic sets of pairs are
independent.  The basic case, a star: for a fixed span `u` (`r` ones out of
`b`) and `m` fresh, independent, uniformly placed partners with `r'` objects
each, the number of partner tuples none of which meshes with `u` is
`(C(b, r') - C(b - r, r'))^m`, out of `C(b, r')^m`.  So the probability is
exactly `(1 - q)^m` with `q = C(b - r, r')/C(b, r')`, whatever `u`'s
arrangement (`star_none_mesh`).

What doesn't carry over to SplitMesher (`docs/colour-placement.md`,
`theory/lean/README.md`): a right span probed by several left spans is no longer
fresh.  Conditioning on its earlier failures changes the next probe's odds.
-/

namespace MeshTheory

/-- All length-`m` tuples (lists) with entries from `L`. -/
def tuples (L : List (List Bool)) : Nat → List (List (List Bool))
  | 0 => [[]]
  | m + 1 => L.flatMap fun x => (tuples L m).map (x :: ·)

private theorem sum_ite_const (L : List (List Bool)) (P : List Bool → Bool) (N : Nat) :
    (L.map fun x => if P x = true then N else 0).sum = (L.filter P).length * N := by
  induction L with
  | nil => simp
  | cons y L ih =>
    simp only [List.map_cons, List.sum_cons, List.filter_cons, ih]
    cases P y <;> simp [Nat.succ_mul] <;> omega

theorem tuples_filter_all (L : List (List Bool)) (P : List Bool → Bool) :
    ∀ m, ((tuples L m).filter (·.all P)).length = ((L.filter P).length) ^ m
  | 0 => rfl
  | m + 1 => by
    simp only [tuples, List.filter_flatMap, List.length_flatMap]
    have : ∀ x, ((List.map (x :: ·) (tuples L m)).filter (·.all P)).length =
        if P x = true then ((tuples L m).filter (·.all P)).length else 0 := by
      intro x
      rw [List.filter_map, List.length_map]
      cases hx : P x
      · simp [Function.comp_def, hx]
      · simp [Function.comp_def, hx]
    rw [List.map_congr_left (fun x _ => this x), sum_ite_const, tuples_filter_all L P m, Nat.pow_succ,
      Nat.mul_comm]

theorem tuples_length (L : List (List Bool)) : ∀ m, (tuples L m).length = L.length ^ m := by
  intro m
  have := tuples_filter_all L (fun _ => true) m
  rw [List.filter_eq_self.2 (by intro x _; simp), List.filter_eq_self.2 (by intro x _; simp)] at this
  exact this

/-- Strings with `r'` ones that do **not** mesh with `u`. -/
def notMeshing (u : List Bool) (r' : Nat) : List (List Bool) :=
  (withOnes u.length r').filter fun v => !meshes v u

private theorem filter_split (L : List (List Bool)) (P : List Bool → Bool) :
    (L.filter P).length + (L.filter (fun x => !P x)).length = L.length := by
  induction L with
  | nil => rfl
  | cons y L ih => simp only [List.filter_cons, List.length_cons]; cases P y <;> simp <;> omega

theorem notMeshing_length (u : List Bool) (r' : Nat) :
    (notMeshing u r').length = choose u.length r' - choose (u.length - ones u) r' := by
  have htot := withOnes_length u.length r'
  have hav := avoiding_length u r'
  have hsplit := filter_split (withOnes u.length r') (fun v => meshes v u)
  have : (withOnes u.length r').filter (fun v => meshes v u) = avoiding u r' := by
    unfold withOnes avoiding; rw [List.filter_filter]
    apply List.filter_congr; intro s _; cases meshes s u <;> simp [Bool.and_comm]
  rw [this, hav, htot] at hsplit
  unfold notMeshing
  omega

/-- **Star independence**: of the `C(b, r')^m` tuples of `m` partners, exactly
`(C(b, r') - C(b - r, r'))^m` contain no span meshing with `u` -- the
probability `(1 - q)^m`, as if the `m` events were independent. -/
theorem star_none_mesh (u : List Bool) (r' m : Nat) :
    ((tuples (withOnes u.length r') m).filter (·.all fun v => !meshes v u)).length =
      (choose u.length r' - choose (u.length - ones u) r') ^ m ∧
    (tuples (withOnes u.length r') m).length = (choose u.length r') ^ m := by
  constructor
  · rw [tuples_filter_all, ← notMeshing_length]; rfl
  · rw [tuples_length, withOnes_length]

end MeshTheory
