import MeshTheory.Choose
/-!
# Uniform placement against an arbitrary partner

Mesh's analysis models a span with `r` objects placed uniformly at random as a
uniformly random string with `r` ones.  The key fact behind the hedged
placement bound: such a span meshes with **any fixed** partner string `t` with
probability exactly `choose (b - ones t) r / choose b r`, however `t`'s ones
are arranged.  So the probability that a uniformly placed span meshes with an
independent partner depends only on the two occupancies, whatever placement
policy (and history) produced the partner.
-/

namespace MeshTheory

/-- Strings of length `t.length` with `r` ones that mesh with `t`. -/
def avoiding (t : List Bool) (r : Nat) : List (List Bool) :=
  (strings t.length).filter (fun s => ones s == r && meshes s t)

/-- Strings of length `n` with `r` ones. -/
def withOnes (n r : Nat) : List (List Bool) :=
  (strings n).filter (fun s => ones s == r)

theorem ones_le_length : ∀ s : List Bool, ones s ≤ s.length
  | [] => Nat.le_refl 0
  | true :: s => by simp [ones]; exact ones_le_length s
  | false :: s => by simp [ones]; exact Nat.le_succ_of_le (ones_le_length s)

private theorem len_filter_map (l : List (List Bool)) (f : List Bool → List Bool) (p : List Bool → Bool) :
    ((l.map f).filter p).length = (l.filter (p ∘ f)).length := by
  rw [List.filter_map, List.length_map]

private theorem filter_false (l : List (List Bool)) : (l.filter (fun _ => false)).length = 0 := by
  simp

theorem avoiding_length (t : List Bool) (r : Nat) :
    (avoiding t r).length = choose (t.length - ones t) r := by
  induction t generalizing r with
  | nil =>
    cases r <;> simp [avoiding, strings, ones, meshes]
  | cons b t ih =>
    have hsplit : ∀ p : List Bool → Bool, ((strings (t.length + 1)).filter p).length =
        ((strings t.length).filter (p ∘ (false :: ·))).length +
        ((strings t.length).filter (p ∘ (true :: ·))).length := by
      intro p; simp only [strings, List.filter_append, List.length_append, len_filter_map]
    have hle := ones_le_length t
    unfold avoiding at ih ⊢
    simp only [List.length_cons]
    rw [hsplit]
    cases b with
    | true =>
      -- an occupied partner slot: s must leave it empty
      have h1 : ((strings t.length).filter
          ((fun s => ones s == r && meshes s (true :: t)) ∘ (true :: ·))).length = 0 := by
        have : ((fun s => ones s == r && meshes s (true :: t)) ∘ (true :: ·)) = fun _ => false := by
          funext s; simp [meshes]
        rw [this]; exact filter_false _
      have h0 : ((fun s => ones s == r && meshes s (true :: t)) ∘ (false :: ·)) =
          fun s => ones s == r && meshes s t := by funext s; simp [ones, meshes]
      rw [h1, h0, ih]
      simp [ones]
    | false =>
      have h0 : ((fun s => ones s == r && meshes s (false :: t)) ∘ (false :: ·)) =
          fun s => ones s == r && meshes s t := by funext s; simp [ones, meshes]
      rw [h0, ih]
      have hsub : t.length + 1 - ones (false :: t) = (t.length - ones t) + 1 := by
        simp [ones]; omega
      rw [hsub]
      cases r with
      | zero =>
        have h1 : ((fun s => ones s == 0 && meshes s (false :: t)) ∘ (true :: ·)) = fun _ => false := by
          funext s; simp [ones]
        rw [h1, filter_false]; simp [choose]
      | succ r =>
        have h1 : ((fun s => ones s == r + 1 && meshes s (false :: t)) ∘ (true :: ·)) =
            fun s => ones s == r && meshes s t := by funext s; simp [ones, meshes]
        rw [h1, ih r, choose_succ_succ]; omega

/-- The number of strings of length `n` with `r` ones is `choose n r`. -/
theorem withOnes_length (n r : Nat) : (withOnes n r).length = choose n r := by
  have h := avoiding_length (List.replicate n false) r
  have hl : (List.replicate n false).length = n := List.length_replicate
  have hones : ∀ m, ones (List.replicate m false) = 0 := by
    intro m; induction m with
    | zero => rfl
    | succ m ih => simp [List.replicate_succ, ones, ih]
  have hmesh : ∀ s : List Bool, meshes s (List.replicate s.length false) = true := by
    intro s; induction s with
    | nil => rfl
    | cons a s ih => simp [List.replicate_succ, meshes, ih]
  rw [hones, hl, Nat.sub_zero] at h
  rw [← h]; unfold avoiding withOnes; rw [hl]
  congr 1
  apply List.filter_congr
  intro s hs
  have := mem_strings_length hs
  subst this
  simp [hmesh]

/-- **Uniform placement is partner-oblivious.**  Of the `choose b r` equally
likely placements of a span with `r` objects, exactly `choose (b - r') r` mesh
with a given partner string `t` of length `b` with `r'` ones -- the same count
for every such `t`.  Averaging over any distribution of partners (from any
placement policy, after any history) gives mesh probability
`choose (b - r') r / choose b r`, which depends only on `r` and `r'`. -/
theorem uniform_vs_any (t : List Bool) (r : Nat) :
    (avoiding t r).length = choose (t.length - ones t) r ∧
    (withOnes t.length r).length = choose t.length r :=
  ⟨avoiding_length t r, withOnes_length t.length r⟩

/-- Two partners with the same length and occupancy are met by a uniform span
equally often, regardless of where their objects sit. -/
theorem uniform_partner_invariant (t t' : List Bool) (r : Nat)
    (hl : t.length = t'.length) (ho : ones t = ones t') :
    (avoiding t r).length = (avoiding t' r).length := by
  rw [avoiding_length, avoiding_length, hl, ho]

end MeshTheory
