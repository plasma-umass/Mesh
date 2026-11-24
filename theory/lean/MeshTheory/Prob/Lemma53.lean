import MeshTheory.Prob.Bounds
/-!
# Lemma 5.3 in expectation, formally

**Model.** `h` left and `h` right vertices; each pair `(i, j)` is an edge
independently with probability `q`.  The probability space is the finite set
of graphs `Ω = Fin h → Fin h → Bool` with weight
`w E = Π_{i,j} (if E i j then q else 1 - q)`.

**Algorithm.** SplitMesher as Mesh implements it: left vertex `i` probes right
vertices `i, i+1, …, i+t-1` (mod `h`) and takes the first free one it has an
edge to.

**Theorem** (`lemma53_expectation`): for `1 ≤ t ≤ h`,
`E[M] ≥ h (1 - (1-q)^t) / (2 - (1-q)^t)`, and with `t ≥ k/q`,
`E[M] / h ≥ (1 - e^{-k}) / (2 - e^{-k})` (`lemma53_exp`).

**Proof.**
* `takenAt E i`, the right vertices taken before `i` is processed, depends only
  on rows `< i` (`takenAt_congr`).
* Rows are independent.  Conditioning on the other rows, `u_i` is unmatched iff
  its row has no edge into the free part of its window: probability
  `(1-q)^(free_i)` (`row_avoid`, `expect_unmatched`).
* Chord bound (`chord`) and `Σ_i T_i = t |taken| = t M` (each right vertex is in
  exactly `t` windows).
-/

namespace MeshTheory.Prob

open Finset BigOperators

variable {h : ℕ}

/-! ### The algorithm -/

/-- Right vertex probed by left vertex `i` at step `d`. -/
def probe (i : Fin h) (d : ℕ) : Fin h :=
  ⟨(i.val + d) % h, Nat.mod_lt _ (Nat.lt_of_le_of_lt (Nat.zero_le _) i.isLt)⟩

/-- `i`'s window: the right vertices it probes. -/
def window (t : ℕ) (i : Fin h) : Finset (Fin h) := (range t).image (probe i)

/-- The first free neighbour of `i` in probe order. -/
def pick (t : ℕ) (E : Fin h → Fin h → Bool) (taken : Finset (Fin h)) (i : Fin h) : Option (Fin h) :=
  ((List.range t).map (probe i)).find? fun j => E i j && decide (j ∉ taken)

/-- Right vertices taken before left vertex `n` (`n ≤ h`) is processed. -/
def takenAt (t : ℕ) (E : Fin h → Fin h → Bool) : ℕ → Finset (Fin h)
  | 0 => ∅
  | n + 1 =>
    if hn : n < h then
      match pick t E (takenAt t E n) ⟨n, hn⟩ with
      | some j => insert j (takenAt t E n)
      | none => takenAt t E n
    else takenAt t E n

/-- `i` is matched. -/
def matched (t : ℕ) (E : Fin h → Fin h → Bool) (i : Fin h) : Prop :=
  (pick t E (takenAt t E i.val) i).isSome

instance (t : ℕ) (E : Fin h → Fin h → Bool) (i : Fin h) : Decidable (matched t E i) := by
  unfold matched; infer_instance

/-- Number of matches. -/
def M (t : ℕ) (E : Fin h → Fin h → Bool) : ℕ := (univ.filter (matched t E)).card

/-! ### Deterministic facts -/

theorem pick_none_iff (t : ℕ) (E : Fin h → Fin h → Bool) (taken : Finset (Fin h)) (i : Fin h) :
    pick t E taken i = none ↔ ∀ j ∈ window t i, j ∉ taken → E i j = false := by
  unfold pick window
  rw [List.find?_eq_none]
  simp only [List.mem_map, List.mem_range, mem_image, mem_range, Bool.and_eq_true,
    decide_eq_true_eq, not_and, forall_exists_index, and_imp]
  constructor
  · intro H j d hd hj hnt; subst hj
    by_contra hE; exact (H _ d hd rfl (by simpa using hE)) hnt
  · intro H j d hd hj hE hnt; subst hj
    rw [H _ d hd rfl hnt] at hE; exact absurd hE (by simp)

theorem pick_some_mem (t : ℕ) (E : Fin h → Fin h → Bool) (taken : Finset (Fin h)) (i j : Fin h)
    (hp : pick t E taken i = some j) : j ∈ window t i ∧ j ∉ taken := by
  unfold pick at hp
  have hm := List.mem_of_find?_eq_some hp
  have hpj := List.find?_some hp
  simp only [Bool.and_eq_true, decide_eq_true_eq] at hpj
  refine ⟨?_, hpj.2⟩
  unfold window
  simp only [List.mem_map, List.mem_range] at hm
  obtain ⟨d, hd, rfl⟩ := hm
  exact mem_image.2 ⟨d, mem_range.2 hd, rfl⟩

/-- Taken sets depend only on the rows processed so far. -/
theorem takenAt_congr (t : ℕ) (E E' : Fin h → Fin h → Bool) :
    ∀ n, (∀ i : Fin h, i.val < n → E i = E' i) → takenAt t E n = takenAt t E' n
  | 0, _ => rfl
  | n + 1, hE => by
    have ih := takenAt_congr t E E' n (fun i hi => hE i (by omega))
    simp only [takenAt]
    split
    · rename_i hn
      have hrow := hE ⟨n, hn⟩ (by simp)
      have : pick t E (takenAt t E n) ⟨n, hn⟩ = pick t E' (takenAt t E' n) ⟨n, hn⟩ := by
        unfold pick; rw [ih, hrow]
      rw [this, ih]
    · exact ih

theorem takenAt_succ (t : ℕ) (E : Fin h → Fin h → Bool) (n : ℕ) (hn : n < h) :
    takenAt t E (n + 1) = match pick t E (takenAt t E n) ⟨n, hn⟩ with
      | some j => insert j (takenAt t E n)
      | none => takenAt t E n := by
  simp only [takenAt, hn, dite_true]

/-- Each step takes one more vertex iff it matches. -/
theorem card_takenAt_succ (t : ℕ) (E : Fin h → Fin h → Bool) (n : ℕ) (hn : n < h) :
    (takenAt t E (n + 1)).card = (takenAt t E n).card + if matched t E ⟨n, hn⟩ then 1 else 0 := by
  rw [takenAt_succ t E n hn]
  unfold matched
  split
  · rename_i j hp
    have hj := (pick_some_mem t E _ _ j hp).2
    simp [hp, Finset.card_insert_of_notMem hj]
  · rename_i hp
    simp [hp]

theorem takenAt_mono (t : ℕ) (E : Fin h → Fin h → Bool) : ∀ n k, takenAt t E n ⊆ takenAt t E (n + k)
  | _, 0 => subset_refl _
  | n, k + 1 => by
    refine subset_trans (takenAt_mono t E n k) ?_
    rw [show n + (k + 1) = (n + k) + 1 by omega]
    by_cases hn : n + k < h
    · rw [takenAt_succ t E _ hn]
      split
      · exact subset_insert _ _
      · exact subset_refl _
    · simp [takenAt, hn]

theorem card_takenAt (t : ℕ) (E : Fin h → Fin h → Bool) :
    ∀ n, n ≤ h → ((takenAt t E n).card : ℝ) = ∑ m ∈ range n, if hm : m < h then (if matched t E ⟨m, hm⟩ then 1 else 0) else 0
  | 0, _ => by simp [takenAt]
  | n + 1, hn => by
    rw [card_takenAt_succ t E n (by omega), sum_range_succ, ← card_takenAt t E n (by omega)]
    simp [show n < h by omega]

theorem M_eq_card (t : ℕ) (E : Fin h → Fin h → Bool) : (M t E : ℝ) = (takenAt t E h).card := by
  rw [card_takenAt t E h le_rfl, M, Finset.card_filter, Nat.cast_sum]
  rw [← Fin.sum_univ_eq_sum_range (fun m => if hm : m < h then (if matched t E ⟨m, hm⟩ then (1 : ℝ) else 0) else 0)]
  apply Finset.sum_congr rfl
  intro i _
  simp [i.isLt]

theorem mod_two (x n : ℕ) (hx : x < 2 * n) : x % n = if x < n then x else x - n := by
  split
  · exact Nat.mod_eq_of_lt (by assumption)
  · rw [Nat.mod_eq_sub_mod (by omega), Nat.mod_eq_of_lt (by omega)]

theorem mem_window_iff (t : ℕ) (ht : t ≤ h) (i j : Fin h) :
    j ∈ window t i ↔ (j.val + h - i.val) % h < t := by
  have hi := i.isLt; have hj := j.isLt
  rw [mod_two _ _ (by omega)]
  simp only [window, mem_image, mem_range, probe]
  constructor
  · rintro ⟨d, hd, hjd⟩
    have hv : (i.val + d) % h = j.val := by rw [← hjd]
    rw [mod_two _ _ (by omega)] at hv
    split at hv <;> split <;> omega
  · intro hlt
    split at hlt
    · refine ⟨j.val + h - i.val, hlt, Fin.ext ?_⟩
      simp only; rw [mod_two _ _ (by omega)]; split <;> omega
    · refine ⟨j.val + h - i.val - h, hlt, Fin.ext ?_⟩
      simp only; rw [mod_two _ _ (by omega)]; split <;> omega

/-- Each right vertex lies in exactly `t` windows (`t ≤ h`, wrap-around):
`i ↦ (j - i) mod h` maps those `i` bijectively onto `range t`. -/
theorem card_windows_containing (t : ℕ) (ht : t ≤ h) (j : Fin h) :
    (univ.filter fun i : Fin h => j ∈ window t i).card = t := by
  have hj := j.isLt
  refine Eq.trans ?_ (card_range t)
  apply Finset.card_bij (fun i _ => (j.val + h - i.val) % h)
  · intro i hi
    simp only [mem_filter, mem_univ, true_and] at hi
    exact mem_range.2 ((mem_window_iff t ht i j).1 hi)
  · intro a _ b _ hab
    have ha := a.isLt; have hb := b.isLt
    rw [mod_two _ _ (by omega), mod_two _ _ (by omega)] at hab
    apply Fin.ext
    split at hab <;> split at hab <;> omega
  · intro d hd
    have hd' := mem_range.1 hd
    refine ⟨⟨(j.val + h - d) % h, Nat.mod_lt _ (by omega)⟩, ?_, ?_⟩
    · simp only [mem_filter, mem_univ, true_and]
      rw [mem_window_iff t ht]
      simp only
      rw [mod_two (j.val + h - d) h (by omega)]
      split
      · rw [mod_two _ _ (by omega)]; split <;> omega
      · rw [mod_two _ _ (by omega)]; split <;> omega
    · simp only
      rw [mod_two (j.val + h - d) h (by omega)]
      split
      · rw [mod_two _ _ (by omega)]; split <;> omega
      · rw [mod_two _ _ (by omega)]; split <;> omega

/-- `Σ_i |window_i ∩ S| = t |S|`. -/
theorem sum_window_inter (t : ℕ) (ht : t ≤ h) (S : Finset (Fin h)) :
    ∑ i : Fin h, ((window t i ∩ S).card : ℝ) = t * S.card := by
  have : ∀ i : Fin h, ((window t i ∩ S).card : ℝ) = ∑ j ∈ S, if j ∈ window t i then 1 else 0 := by
    intro i
    rw [show window t i ∩ S = S.filter (· ∈ window t i) by ext; simp [and_comm]]
    rw [Finset.card_filter]; push_cast; rfl
  simp_rw [this]
  rw [Finset.sum_comm]
  have : ∀ j ∈ S, (∑ i : Fin h, if j ∈ window t i then (1 : ℝ) else 0) = t := by
    intro j _
    rw [← Finset.sum_filter, Finset.sum_const, card_windows_containing t ht j]; simp
  rw [Finset.sum_congr rfl this]
  simp [mul_comm]

/-! ### The probability space -/

variable (q : ℝ)

def rowWeight (r : Fin h → Bool) : ℝ := ∏ j, if r j then q else 1 - q
def weight (E : Fin h → Fin h → Bool) : ℝ := ∏ i, rowWeight q (E i)

/-- Expectation over random graphs. -/
noncomputable def expect (f : (Fin h → Fin h → Bool) → ℝ) : ℝ := ∑ E, weight q E * f E

theorem sum_rowWeight_avoid (S : Finset (Fin h)) :
    ∑ r : Fin h → Bool, rowWeight q r * (if ∀ j ∈ S, r j = false then 1 else 0) = (1 - q) ^ S.card := by
  have : ∀ r : Fin h → Bool, rowWeight q r * (if ∀ j ∈ S, r j = false then 1 else 0) =
      ∏ j, (if r j then q else 1 - q) * (if j ∈ S ∧ r j = true then 0 else 1) := by
    intro r
    unfold rowWeight
    rw [Finset.prod_mul_distrib]
    congr 1
    split
    · rename_i hall
      symm; apply Finset.prod_eq_one
      intro j _; split
      · rename_i hj; exact absurd (hall j hj.1) (by simp [hj.2])
      · rfl
    · rename_i hall
      push Not at hall
      obtain ⟨j, hjS, hj⟩ := hall
      symm; apply Finset.prod_eq_zero (mem_univ j)
      simp [hjS, show r j = true by simpa using hj]
  simp_rw [this]
  rw [← Fintype.prod_sum (fun j (b : Bool) => (if b then q else 1 - q) * (if j ∈ S ∧ b = true then 0 else 1))]
  rw [← Finset.prod_filter_mul_prod_filter_not univ (· ∈ S)]
  have h1 : ∀ j ∈ univ.filter (· ∈ S), (∑ b : Bool, (if b then q else 1 - q) * (if j ∈ S ∧ b = true then 0 else 1)) = 1 - q := by
    intro j hj; simp at hj; simp [hj]
  have h2 : ∀ j ∈ univ.filter (· ∉ S), (∑ b : Bool, (if b then q else 1 - q) * (if j ∈ S ∧ b = true then 0 else 1)) = 1 := by
    intro j hj; simp at hj; simp [hj]
  rw [Finset.prod_congr rfl h1, Finset.prod_congr rfl h2]
  simp [Finset.prod_const]

theorem sum_rowWeight : ∑ r : Fin h → Bool, rowWeight q r = 1 := by
  have := sum_rowWeight_avoid q (∅ : Finset (Fin h))
  simpa using this

/-- Weight of all rows but `i`. -/
def restWeight (i : Fin h) (rest : {k // k ≠ i} → Fin h → Bool) : ℝ := ∏ k, rowWeight q (rest k)

theorem weight_split (i : Fin h) (E : Fin h → Fin h → Bool) :
    weight q E = rowWeight q (E i) * ∏ k : {k // k ≠ i}, rowWeight q (E k) := by
  unfold weight; exact Fintype.prod_eq_mul_prod_subtype_ne _ i

/-- Sum over graphs, split into row `i` and the other rows. -/
theorem sum_split (i : Fin h) (F : (Fin h → Fin h → Bool) → ℝ) :
    ∑ E, F E = ∑ r : Fin h → Bool, ∑ rest : {k // k ≠ i} → Fin h → Bool,
      F ((Equiv.funSplitAt i (Fin h → Bool)).symm (r, rest)) := by
  rw [← Fintype.sum_prod_type']
  exact Fintype.sum_equiv (Equiv.funSplitAt i (Fin h → Bool)) _ _
    (fun E => by simp only [Prod.mk.eta, Equiv.symm_apply_apply])

/-- **Row independence.**  If `S` and `g` don't depend on row `i`, then
`E[[row i avoids S] g] = E[(1-q)^|S| g]`. -/
theorem expect_row_avoid (i : Fin h) (S : (Fin h → Fin h → Bool) → Finset (Fin h))
    (g : (Fin h → Fin h → Bool) → ℝ)
    (hS : ∀ E r, S (Function.update E i r) = S E) (hg : ∀ E r, g (Function.update E i r) = g E) :
    expect q (fun E => (if ∀ j ∈ S E, E i j = false then 1 else 0) * g E) =
      expect q (fun E => (1 - q) ^ (S E).card * g E) := by
  unfold expect
  rw [sum_split i, sum_split i]
  set e := (Equiv.funSplitAt i (Fin h → Bool)).symm
  -- everything but row i, evaluated with row i reset to a fixed row
  have hrow : ∀ r rest, e (r, rest) i = r := by intro r rest; simp [e, Equiv.funSplitAt_symm_apply]
  have hupd : ∀ r rest, e (r, rest) = Function.update (e (fun _ => false, rest)) i r := by
    intro r rest; funext k
    by_cases hk : k = i
    · subst hk; simp [e, Equiv.funSplitAt_symm_apply]
    · simp [e, Equiv.funSplitAt_symm_apply, hk]
  have hW : ∀ r rest, weight q (e (r, rest)) = rowWeight q r * restWeight q i rest := by
    intro r rest
    rw [weight_split q i, hrow]
    congr 1
    apply Finset.prod_congr rfl; intro k _
    simp [e, Equiv.funSplitAt_symm_apply, k.2]
  simp_rw [hW]
  conv_lhs => rw [Finset.sum_comm]
  conv_rhs => rw [Finset.sum_comm]
  apply Finset.sum_congr rfl; intro rest _
  have hSr : ∀ r, S (e (r, rest)) = S (e (fun _ => false, rest)) := by
    intro r; rw [hupd r rest, hS]
  have hgr : ∀ r, g (e (r, rest)) = g (e (fun _ => false, rest)) := by
    intro r; rw [hupd r rest, hg]
  simp_rw [hSr, hgr, hrow]
  have h1 := sum_rowWeight_avoid q (S (e (fun _ => false, rest)))
  have h2 := sum_rowWeight q (h := h)
  calc ∑ r, rowWeight q r * restWeight q i rest * ((if ∀ j ∈ S (e (fun _ => false, rest)), r j = false then 1 else 0) * g (e (fun _ => false, rest)))
      = restWeight q i rest * g (e (fun _ => false, rest)) *
          ∑ r, rowWeight q r * (if ∀ j ∈ S (e (fun _ => false, rest)), r j = false then 1 else 0) := by
        rw [Finset.mul_sum]; apply Finset.sum_congr rfl; intro r _; ring
    _ = restWeight q i rest * g (e (fun _ => false, rest)) * (1 - q) ^ (S (e (fun _ => false, rest))).card := by rw [h1]
    _ = ∑ r, rowWeight q r * restWeight q i rest * ((1 - q) ^ (S (e (fun _ => false, rest))).card * g (e (fun _ => false, rest))) := by
        rw [← Finset.sum_mul, ← Finset.sum_mul, h2]; ring

theorem weight_nonneg (hq0 : 0 ≤ q) (hq1 : q ≤ 1) (E : Fin h → Fin h → Bool) : 0 ≤ weight q E := by
  unfold weight rowWeight
  apply Finset.prod_nonneg; intro i _
  apply Finset.prod_nonneg; intro j _
  split <;> linarith

theorem sum_weight : ∑ E : Fin h → Fin h → Bool, weight q E = 1 := by
  unfold weight
  rw [← Fintype.prod_sum (fun _ (r : Fin h → Bool) => rowWeight q r)]
  simp [sum_rowWeight]

theorem expect_const (c : ℝ) : expect q (h := h) (fun _ => c) = c := by
  unfold expect; rw [← Finset.sum_mul, sum_weight]; ring

theorem expect_add (f g : (Fin h → Fin h → Bool) → ℝ) :
    expect q (fun E => f E + g E) = expect q f + expect q g := by
  unfold expect; rw [← Finset.sum_add_distrib]; apply Finset.sum_congr rfl; intro _ _; ring

theorem expect_sum (f : Fin h → (Fin h → Fin h → Bool) → ℝ) :
    expect q (fun E => ∑ i, f i E) = ∑ i, expect q (f i) := by
  unfold expect; simp_rw [Finset.mul_sum]; rw [Finset.sum_comm]

theorem expect_smul (c : ℝ) (f : (Fin h → Fin h → Bool) → ℝ) :
    expect q (fun E => c * f E) = c * expect q f := by
  unfold expect; rw [Finset.mul_sum]; apply Finset.sum_congr rfl; intro _ _; ring

theorem expect_mono (hq0 : 0 ≤ q) (hq1 : q ≤ 1) (f g : (Fin h → Fin h → Bool) → ℝ) (hfg : ∀ E, f E ≤ g E) :
    expect q f ≤ expect q g := by
  unfold expect
  apply Finset.sum_le_sum; intro E _
  exact mul_le_mul_of_nonneg_left (hfg E) (weight_nonneg q hq0 hq1 E)

/-! ### The theorem -/

theorem unmatched_iff (t : ℕ) (E : Fin h → Fin h → Bool) (i : Fin h) :
    ¬ matched t E i ↔ ∀ j ∈ window t i \ takenAt t E i.val, E i j = false := by
  unfold matched
  rw [Option.not_isSome_iff_eq_none, pick_none_iff]
  simp only [mem_sdiff, and_imp]

theorem takenAt_indep (t : ℕ) (E : Fin h → Fin h → Bool) (i : Fin h) (r : Fin h → Bool) :
    takenAt t (Function.update E i r) i.val = takenAt t E i.val :=
  takenAt_congr t _ _ i.val (fun i' hi' => by
    rw [Function.update_of_ne (Fin.ne_of_lt hi')])

theorem card_window (t : ℕ) (ht : t ≤ h) (i : Fin h) : (window t i).card = t := by
  unfold window
  rw [card_image_of_injOn, card_range]
  intro a ha b hb hab
  simp only [coe_range, Set.mem_Iio] at ha hb
  simp only [probe, Fin.mk.injEq] at hab
  have := Nat.ModEq.add_left_cancel' i.val hab
  have := Nat.mod_eq_of_lt (show a < h by omega)
  have := Nat.mod_eq_of_lt (show b < h by omega)
  unfold Nat.ModEq at *
  omega

/-- **Lemma 5.3 in expectation.** -/
theorem lemma53_expectation (t : ℕ) (ht0 : 0 < t) (ht : t ≤ h) (hq0 : 0 ≤ q) (hq1 : q ≤ 1) :
    (1 - (1 - q) ^ t) / (2 - (1 - q) ^ t) ≤
      expect q (fun E : Fin h → Fin h → Bool => (M t E : ℝ)) / h := by
  have hh : (0 : ℝ) < h := by exact_mod_cast (show 0 < h by omega)
  set e := (1 - q) ^ t
  have he0 : 0 ≤ e := pow_nonneg (by linarith) t
  have he1 : e ≤ 1 := pow_le_one₀ (by linarith) (by linarith)
  -- T_i: taken vertices in i's window when i is processed
  let T : Fin h → (Fin h → Fin h → Bool) → ℕ := fun i E => (window t i ∩ takenAt t E i.val).card
  -- unmatched_i = [row i avoids the free part of i's window]
  have hU : ∀ i : Fin h, expect q (fun E => if matched t E i then 0 else 1) =
      expect q (fun E => (1 - q) ^ (t - T i E)) := by
    intro i
    have hrow := expect_row_avoid q i (fun E => window t i \ takenAt t E i.val) (fun _ => 1)
      (fun E r => by simp only [takenAt_indep]) (fun _ _ => rfl)
    simp only [mul_one] at hrow
    have hcard : ∀ E, (window t i \ takenAt t E i.val).card = t - T i E := by
      intro E
      have := Finset.card_sdiff_add_card_inter (window t i) (takenAt t E i.val)
      rw [card_window t ht] at this
      show _ = t - (window t i ∩ takenAt t E i.val).card
      omega
    simp_rw [hcard] at hrow
    rw [← hrow]
    unfold expect; apply Finset.sum_congr rfl; intro E _
    congr 1
    beta_reduce
    by_cases hm : matched t E i
    · have : ¬ ∀ j ∈ window t i \ takenAt t E i.val, E i j = false := fun H => ((unmatched_iff t E i).2 H) hm
      simp only [hm, this, ite_true, ite_false]
    · simp only [hm, ite_false]
      rw [if_pos ((unmatched_iff t E i).1 hm)]
  -- chord bound pointwise
  have hchord : ∀ i E, (1 - q) ^ (t - T i E) ≤ e + ((T i E : ℝ) / t) * (1 - e) := by
    intro i E
    have hT : T i E ≤ t := by
      calc T i E ≤ (window t i).card := card_le_card inter_subset_left
        _ = t := card_window t ht i
    exact chord (1 - q) (by linarith) (by linarith) t (T i E) hT ht0
  -- Σ_i T_i ≤ t M pointwise
  have hsumT : ∀ E, (∑ i, (T i E : ℝ)) ≤ t * M t E := by
    intro E
    rw [M_eq_card, ← sum_window_inter t ht]
    apply Finset.sum_le_sum; intro i _
    exact_mod_cast card_le_card (inter_subset_inter_left
      (by have := takenAt_mono t E i.val (h - i.val); rwa [show i.val + (h - i.val) = h by omega] at this))
  -- unmatched count
  have hUM : ∀ E, (∑ i : Fin h, (if matched t E i then (0 : ℝ) else 1)) = h - M t E := by
    intro E
    have hM : (M t E : ℝ) = ∑ i : Fin h, (if matched t E i then (1 : ℝ) else 0) := by
      rw [M, Finset.card_filter, Nat.cast_sum]; simp
    rw [hM, eq_sub_iff_add_eq, ← Finset.sum_add_distrib]
    rw [Finset.sum_congr rfl (fun i _ => by split <;> ring : ∀ i ∈ (univ : Finset (Fin h)),
      ((if matched t E i then (0 : ℝ) else 1) + if matched t E i then 1 else 0) = 1)]
    simp
  -- E[h - M] ≤ h e + (1 - e) E[M]
  have hEM : expect q (fun E : Fin h → Fin h → Bool => (h : ℝ) - M t E) = h - expect q (fun E : Fin h → Fin h → Bool => (M t E : ℝ)) := by
    unfold expect
    simp_rw [mul_sub, Finset.sum_sub_distrib, ← Finset.sum_mul, sum_weight, one_mul]
  have hbound : h - expect q (fun E : Fin h → Fin h → Bool => (M t E : ℝ)) ≤ h * e + (1 - e) * expect q (fun E : Fin h → Fin h → Bool => (M t E : ℝ)) := by
    rw [← hEM]
    calc expect q (fun E : Fin h → Fin h → Bool => (h : ℝ) - M t E)
        = expect q (fun E => ∑ i : Fin h, (if matched t E i then (0 : ℝ) else 1)) := by
          unfold expect; apply Finset.sum_congr rfl; intro E _; beta_reduce; rw [hUM]
      _ = ∑ i, expect q (fun E => (1 - q) ^ (t - T i E)) := by rw [expect_sum]; simp_rw [hU]
      _ ≤ ∑ i, expect q (fun E => e + ((T i E : ℝ) / t) * (1 - e)) := by
          apply Finset.sum_le_sum; intro i _; exact expect_mono q hq0 hq1 _ _ (hchord i)
      _ = expect q (fun E : Fin h → Fin h → Bool => h * e + (1 - e) / t * ∑ i, (T i E : ℝ)) := by
          rw [← expect_sum]; unfold expect; apply Finset.sum_congr rfl; intro E _
          congr 1
          beta_reduce
          rw [Finset.sum_add_distrib, Finset.sum_const, Finset.card_univ, Fintype.card_fin, nsmul_eq_mul,
            Finset.mul_sum]
          congr 1; apply Finset.sum_congr rfl; intro i _; ring
      _ ≤ expect q (fun E : Fin h → Fin h → Bool => h * e + (1 - e) * M t E) := by
          apply expect_mono q hq0 hq1; intro E
          have ht' : (0 : ℝ) < t := by exact_mod_cast ht0
          have hc : 0 ≤ (1 - e) / t := div_nonneg (by linarith) ht'.le
          calc h * e + (1 - e) / t * ∑ i, (T i E : ℝ) ≤ h * e + (1 - e) / t * (t * M t E) := by
                gcongr; exact hsumT E
            _ = h * e + (1 - e) * M t E := by field_simp
      _ = h * e + (1 - e) * expect q (fun E : Fin h → Fin h → Bool => (M t E : ℝ)) := by
          unfold expect
          simp_rw [mul_add, Finset.sum_add_distrib, ← Finset.sum_mul, sum_weight, one_mul]
          congr 1; rw [Finset.mul_sum]; apply Finset.sum_congr rfl; intro _ _; ring
  exact matches_bound h _ e hh he1 hbound

theorem lemma53_exp (t : ℕ) (ht0 : 0 < t) (ht : t ≤ h) (hq0 : 0 ≤ q) (hq1 : q ≤ 1)
    (k : ℝ) (hk0 : 0 ≤ k) (hk : k ≤ q * t) :
    (1 - Real.exp (-k)) / (2 - Real.exp (-k)) ≤
      expect q (fun E : Fin h → Fin h → Bool => (M t E : ℝ)) / h := by
  refine le_trans ?_ (lemma53_expectation q t ht0 ht hq0 hq1)
  apply bound_antitone
  · exact le_trans (one_sub_pow_le_exp q hq1 t) (Real.exp_le_exp.2 (by linarith))
  · rw [Real.exp_le_one_iff]; linarith

end MeshTheory.Prob
