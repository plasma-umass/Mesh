import MeshTheory.Prob.Lemma53
/-!
# SplitMesher's match count is 1-Lipschitz in each row

`lipschitz`: if two graphs differ only in left vertex `i`'s row (its edges),
SplitMesher's match counts differ by at most 1.

This is the bounded-differences hypothesis of McDiarmid's inequality (or of
Azuma–Hoeffding, applied to the Doob martingale `E[M | rows < i]`, which
Mathlib has as `measure_sum_ge_le_of_hasCondSubgaussianMGF`).  With rows
independent it gives `P(M ≤ E[M] - λ) ≤ exp (-2λ²/h)`: Lemma 5.3 with high
probability, without the paper's Chernoff step, which needs independence the
indicators don't have.  The measure-theoretic plumbing from this finite model
to Mathlib's filtrations is not done here.

**Invariant** (`step_inv`): after any step, the taken sets `S` and `S'` of the
two runs satisfy `|S \ S'| ≤ 1` and `|S' \ S| ≤ 1`.  When both runs process
the same row, if they pick different vertices `x ≠ x'`, then one of the picks
is already taken in the other run (`find_two`), which keeps the invariant.
-/

namespace MeshTheory.Prob

open Finset

variable {h : ℕ}

/-- Two `find?`s on the same list with different predicates: if they return
different results, the first's result fails the second predicate, or vice
versa. -/
theorem find_two {α : Type*} (p p' : α → Bool) :
    ∀ (L : List α) (x x' : Option α), L.find? p = x → L.find? p' = x' → x ≠ x' →
      (∀ a, x = some a → p' a = false) ∨ (∀ b, x' = some b → p b = false)
  | [], x, x', hx, hx', _ => by simp at hx hx'; subst hx; subst hx'; simp
  | a :: L, x, x', hx, hx', hne => by
    simp only [List.find?_cons] at hx hx'
    cases hpa : p a <;> cases hp'a : p' a <;> simp only [hpa, hp'a] at hx hx'
    · exact find_two p p' L x x' hx hx' hne
    · -- p a false, p' a true: x' = a
      subst hx'; right; intro b hb; cases hb; exact hpa
    · subst hx; left; intro b hb; cases hb; exact hp'a
    · subst hx; subst hx'; exact absurd rfl hne

/-- The picked vertex, if any, is free. -/
theorem pick_free (t : ℕ) (E : Fin h → Fin h → Bool) (S : Finset (Fin h)) (i x : Fin h)
    (hp : pick t E S i = some x) : x ∉ S := (pick_some_mem t E S i x hp).2

/-- If the two runs, on the same row, pick differently, one pick is taken in
the other run. -/
theorem pick_diff (t : ℕ) (E : Fin h → Fin h → Bool) (S S' : Finset (Fin h)) (i : Fin h)
    (hne : pick t E S i ≠ pick t E S' i) :
    (∀ a, pick t E S i = some a → a ∈ S') ∨ (∀ b, pick t E S' i = some b → b ∈ S) := by
  rcases find_two (fun j => E i j && decide (j ∉ S)) (fun j => E i j && decide (j ∉ S'))
    ((List.range t).map (probe i)) _ _ rfl rfl hne with H | H
  · left; intro a ha
    have h1 := H a ha
    have h2 := List.find?_some ha
    simp only [Bool.and_eq_true, decide_eq_true_eq] at h2
    simp only [h2.1, Bool.true_and, decide_eq_false_iff_not, not_not] at h1
    exact h1
  · right; intro b hb
    have h1 := H b hb
    have h2 := List.find?_some hb
    simp only [Bool.and_eq_true, decide_eq_true_eq] at h2
    simp only [h2.1, Bool.true_and, decide_eq_false_iff_not, not_not] at h1
    exact h1

/-- One step from sets `S, S'` with `o, o'` the picks. -/
def addPick (S : Finset (Fin h)) : Option (Fin h) → Finset (Fin h)
  | some j => insert j S
  | none => S

theorem mem_addPick {S : Finset (Fin h)} {o : Option (Fin h)} {a : Fin h} :
    a ∈ addPick S o ↔ a ∈ S ∨ o = some a := by
  cases o <;> simp [addPick, or_comm, eq_comm]

/-- `|A| ≤ 1` iff any two elements are equal. -/
theorem diff_le_one {A B : Finset (Fin h)} : (A \ B).card ≤ 1 ↔ ∀ a ∈ A, a ∉ B → ∀ b ∈ A, b ∉ B → a = b := by
  rw [card_le_one]; simp only [mem_sdiff, and_imp]

theorem pick_some_facts (t : ℕ) (E : Fin h → Fin h → Bool) (S : Finset (Fin h)) (i b : Fin h)
    (hp : pick t E S i = some b) : b ∈ window t i ∧ E i b = true ∧ b ∉ S := by
  have h1 := pick_some_mem t E S i b hp
  have h2 := List.find?_some hp
  simp only [Bool.and_eq_true, decide_eq_true_eq] at h2
  exact ⟨h1.1, h2.1, h1.2⟩

/-- The invariant is preserved by a step on the same row. -/
theorem step_inv (t : ℕ) (E : Fin h → Fin h → Bool) (S S' : Finset (Fin h)) (i : Fin h)
    (h1 : (S \ S').card ≤ 1) (h2 : (S' \ S).card ≤ 1) :
    (addPick S (pick t E S i) \ addPick S' (pick t E S' i)).card ≤ 1 ∧
    (addPick S' (pick t E S' i) \ addPick S (pick t E S i)).card ≤ 1 := by
  have F1 := pick_some_facts t E S i
  have F2 := pick_some_facts t E S' i
  have N1 : pick t E S i = none → ∀ j ∈ window t i, j ∉ S → E i j = false := (pick_none_iff t E S i).1
  have N2 : pick t E S' i = none → ∀ j ∈ window t i, j ∉ S' → E i j = false := (pick_none_iff t E S' i).1
  have D := pick_diff t E S S' i
  rw [diff_le_one] at h1 h2
  generalize pick t E S i = o at *
  generalize pick t E S' i = o' at *
  constructor <;> rw [diff_le_one] <;> intro a ha hna b hb hnb <;> simp only [mem_addPick] at ha hna hb hnb <;>
    cases o <;> cases o' <;> grind

theorem takenAt_succ' (t : ℕ) (E : Fin h → Fin h → Bool) (n : ℕ) (hn : n < h) :
    takenAt t E (n + 1) = addPick (takenAt t E n) (pick t E (takenAt t E n) ⟨n, hn⟩) := by
  rw [takenAt_succ t E n hn]
  cases pick t E (takenAt t E n) ⟨n, hn⟩ <;> rfl

/-- The invariant holds at every step for graphs differing only in row `i`. -/
theorem inv_all (t : ℕ) (E E' : Fin h → Fin h → Bool) (i : Fin h) (hE : ∀ k, k ≠ i → E k = E' k) :
    ∀ n, (takenAt t E n \ takenAt t E' n).card ≤ 1 ∧ (takenAt t E' n \ takenAt t E n).card ≤ 1
  | 0 => by simp [takenAt]
  | n + 1 => by
    obtain ⟨ih1, ih2⟩ := inv_all t E E' i hE n
    by_cases hn : n < h
    · rw [takenAt_succ' t E n hn, takenAt_succ' t E' n hn]
      by_cases hni : (⟨n, hn⟩ : Fin h) = i
      · -- the changed row: before it the runs agree
        have hsame : takenAt t E n = takenAt t E' n :=
          takenAt_congr t E E' n (fun k hk => hE k (by intro hki; subst hki; simp [← hni] at hk))
        rw [hsame]
        generalize pick t E (takenAt t E' n) ⟨n, hn⟩ = o
        generalize pick t E' (takenAt t E' n) ⟨n, hn⟩ = o'
        constructor <;> rw [diff_le_one] <;> intro a ha hna b hb hnb <;>
          simp only [mem_addPick] at ha hna hb hnb <;> cases o <;> cases o' <;> grind
      · have hrow : E ⟨n, hn⟩ = E' ⟨n, hn⟩ := hE _ hni
        have hp : pick t E' (takenAt t E' n) ⟨n, hn⟩ = pick t E (takenAt t E' n) ⟨n, hn⟩ := by
          unfold pick; rw [hrow]
        rw [hp]
        exact step_inv t E _ _ ⟨n, hn⟩ ih1 ih2
    · have e1 : takenAt t E (n + 1) = takenAt t E n := by simp [takenAt, hn]
      have e2 : takenAt t E' (n + 1) = takenAt t E' n := by simp [takenAt, hn]
      rw [e1, e2]; exact ⟨ih1, ih2⟩


/-- **SplitMesher is 1-Lipschitz in each row.** -/
theorem lipschitz (t : ℕ) (E E' : Fin h → Fin h → Bool) (i : Fin h) (hE : ∀ k, k ≠ i → E k = E' k) :
    |(M t E : ℝ) - M t E'| ≤ 1 := by
  obtain ⟨h1, h2⟩ := inv_all t E E' i hE h
  rw [M_eq_card, M_eq_card]
  have a := card_sdiff_add_card_inter (takenAt t E h) (takenAt t E' h)
  have b := card_sdiff_add_card_inter (takenAt t E' h) (takenAt t E h)
  rw [inter_comm] at b
  have a' : ((takenAt t E h).card : ℝ) = (takenAt t E h \ takenAt t E' h).card + (takenAt t E h ∩ takenAt t E' h).card := by
    exact_mod_cast a.symm
  have b' : ((takenAt t E' h).card : ℝ) = (takenAt t E' h \ takenAt t E h).card + (takenAt t E h ∩ takenAt t E' h).card := by
    exact_mod_cast b.symm
  have h1' : ((takenAt t E h \ takenAt t E' h).card : ℝ) ≤ 1 := by exact_mod_cast h1
  have h2' : ((takenAt t E' h \ takenAt t E h).card : ℝ) ≤ 1 := by exact_mod_cast h2
  have p1 : (0 : ℝ) ≤ (takenAt t E h \ takenAt t E' h).card := Nat.cast_nonneg _
  have p2 : (0 : ℝ) ≤ (takenAt t E' h \ takenAt t E h).card := Nat.cast_nonneg _
  rw [abs_le]; constructor <;> linarith

end MeshTheory.Prob
