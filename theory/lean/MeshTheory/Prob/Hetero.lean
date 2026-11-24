import MeshTheory.Prob.Lemma53
/-!
# Lemma 5.3 with heterogeneous edge probabilities

Each pair `(i, j)` is an edge independently with its own probability
`p i j ∈ [q, 1]`.  The bound of `lemma53_expectation` still holds with `q`, the
smallest of them (`lemma53_hetero`).  This is the setting of hedged placement
(`hedged_pair_bound`: pairs involving a uniform span mesh with probability
`q_u`, others with some other probability) and of size classes whose spans
have different occupancies, for independent edges.

The only change from the homogeneous proof: given the other rows, `u_i` stays
unmatched with probability `Π_{j free} (1 - p i j) ≤ (1-q)^(free_i)`
(`expectP_row_avoid`, `prod_le_pow`).
-/

namespace MeshTheory.Prob

open Finset BigOperators

variable {h : ℕ} (p : Fin h → Fin h → ℝ)

def rowWeightP (i : Fin h) (r : Fin h → Bool) : ℝ := ∏ j, if r j then p i j else 1 - p i j
def weightP (E : Fin h → Fin h → Bool) : ℝ := ∏ i, rowWeightP p i (E i)
noncomputable def expectP (f : (Fin h → Fin h → Bool) → ℝ) : ℝ := ∑ E, weightP p E * f E

theorem sum_rowWeightP_avoid (i : Fin h) (S : Finset (Fin h)) :
    ∑ r : Fin h → Bool, rowWeightP p i r * (if ∀ j ∈ S, r j = false then 1 else 0) = ∏ j ∈ S, (1 - p i j) := by
  have : ∀ r : Fin h → Bool, rowWeightP p i r * (if ∀ j ∈ S, r j = false then 1 else 0) =
      ∏ j, (if r j then p i j else 1 - p i j) * (if j ∈ S ∧ r j = true then 0 else 1) := by
    intro r
    unfold rowWeightP
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
  rw [← Fintype.prod_sum (fun j (b : Bool) => (if b then p i j else 1 - p i j) * (if j ∈ S ∧ b = true then 0 else 1))]
  rw [← Finset.prod_filter_mul_prod_filter_not univ (· ∈ S)]
  have h1 : ∀ j ∈ univ.filter (· ∈ S), (∑ b : Bool, (if b then p i j else 1 - p i j) * (if j ∈ S ∧ b = true then 0 else 1)) = 1 - p i j := by
    intro j hj; simp at hj; simp [hj]
  have h2 : ∀ j ∈ univ.filter (· ∉ S), (∑ b : Bool, (if b then p i j else 1 - p i j) * (if j ∈ S ∧ b = true then 0 else 1)) = 1 := by
    intro j hj; simp at hj; simp [hj]
  rw [Finset.prod_congr rfl h1, Finset.prod_congr rfl h2]
  simp only [Finset.prod_const_one, mul_one]
  rw [Finset.filter_mem_eq_inter, Finset.univ_inter]

theorem sum_rowWeightP (i : Fin h) : ∑ r : Fin h → Bool, rowWeightP p i r = 1 := by
  have := sum_rowWeightP_avoid p i (∅ : Finset (Fin h))
  simpa using this

def restWeightP (i : Fin h) (rest : {k // k ≠ i} → Fin h → Bool) : ℝ := ∏ k : {k // k ≠ i}, rowWeightP p k.val (rest k)

theorem weightP_split (i : Fin h) (E : Fin h → Fin h → Bool) :
    weightP p E = rowWeightP p i (E i) * ∏ k : {k // k ≠ i}, rowWeightP p k (E k) := by
  unfold weightP; exact Fintype.prod_eq_mul_prod_subtype_ne (fun k => rowWeightP p k (E k)) i

theorem expectP_row_avoid (i : Fin h) (S : (Fin h → Fin h → Bool) → Finset (Fin h))
    (g : (Fin h → Fin h → Bool) → ℝ)
    (hS : ∀ E r, S (Function.update E i r) = S E) (hg : ∀ E r, g (Function.update E i r) = g E) :
    expectP p (fun E => (if ∀ j ∈ S E, E i j = false then 1 else 0) * g E) =
      expectP p (fun E => (∏ j ∈ S E, (1 - p i j)) * g E) := by
  unfold expectP
  rw [sum_split i, sum_split i]
  set e := (Equiv.funSplitAt i (Fin h → Bool)).symm
  have hrow : ∀ r rest, e (r, rest) i = r := by intro r rest; simp [e, Equiv.funSplitAt_symm_apply]
  have hupd : ∀ r rest, e (r, rest) = Function.update (e (fun _ => false, rest)) i r := by
    intro r rest; funext k
    by_cases hk : k = i
    · subst hk; simp [e, Equiv.funSplitAt_symm_apply]
    · simp [e, Equiv.funSplitAt_symm_apply, hk]
  have hW : ∀ r rest, weightP p (e (r, rest)) = rowWeightP p i r * restWeightP p i rest := by
    intro r rest
    rw [weightP_split p i, hrow]
    congr 1
    unfold restWeightP
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
  have h1 := sum_rowWeightP_avoid p i (S (e (fun _ => false, rest)))
  have h2 := sum_rowWeightP p i
  calc ∑ r, rowWeightP p i r * restWeightP p i rest * ((if ∀ j ∈ S (e (fun _ => false, rest)), r j = false then 1 else 0) * g (e (fun _ => false, rest)))
      = restWeightP p i rest * g (e (fun _ => false, rest)) *
          ∑ r, rowWeightP p i r * (if ∀ j ∈ S (e (fun _ => false, rest)), r j = false then 1 else 0) := by
        rw [Finset.mul_sum]; apply Finset.sum_congr rfl; intro r _; ring
    _ = restWeightP p i rest * g (e (fun _ => false, rest)) * ∏ j ∈ S (e (fun _ => false, rest)), (1 - p i j) := by rw [h1]
    _ = ∑ r, rowWeightP p i r * restWeightP p i rest * ((∏ j ∈ S (e (fun _ => false, rest)), (1 - p i j)) * g (e (fun _ => false, rest))) := by
        rw [← Finset.sum_mul, ← Finset.sum_mul, h2]; ring

variable {p}

theorem weightP_nonneg (hp0 : ∀ i j, 0 ≤ p i j) (hp1 : ∀ i j, p i j ≤ 1) (E : Fin h → Fin h → Bool) :
    0 ≤ weightP p E := by
  unfold weightP rowWeightP
  apply Finset.prod_nonneg; intro i _
  apply Finset.prod_nonneg; intro j _
  split
  · exact hp0 i j
  · linarith [hp1 i j]

theorem sum_weightP : ∑ E : Fin h → Fin h → Bool, weightP p E = 1 := by
  unfold weightP
  rw [← Fintype.prod_sum (fun i (r : Fin h → Bool) => rowWeightP p i r)]
  simp [sum_rowWeightP]

theorem expectP_mono (hp0 : ∀ i j, 0 ≤ p i j) (hp1 : ∀ i j, p i j ≤ 1) (f g : (Fin h → Fin h → Bool) → ℝ)
    (hfg : ∀ E, f E ≤ g E) : expectP p f ≤ expectP p g := by
  unfold expectP
  apply Finset.sum_le_sum; intro E _
  exact mul_le_mul_of_nonneg_left (hfg E) (weightP_nonneg hp0 hp1 E)

theorem expectP_sum (f : Fin h → (Fin h → Fin h → Bool) → ℝ) :
    expectP p (fun E => ∑ i, f i E) = ∑ i, expectP p (f i) := by
  unfold expectP; simp_rw [Finset.mul_sum]; rw [Finset.sum_comm]

theorem prod_le_pow (q : ℝ) (hq : ∀ i j, q ≤ p i j) (hp1 : ∀ i j, p i j ≤ 1) (i : Fin h) (S : Finset (Fin h)) :
    ∏ j ∈ S, (1 - p i j) ≤ (1 - q) ^ S.card := by
  rw [← Finset.prod_const]
  exact Finset.prod_le_prod₀ (fun j _ => by linarith [hp1 i j]) (fun j _ => by linarith [hq i j])

/-- **Lemma 5.3, heterogeneous edges**: if every pair meshes independently with
probability at least `q`, the homogeneous bound holds with `q`. -/
theorem lemma53_hetero (q : ℝ) (t : ℕ) (ht0 : 0 < t) (ht : t ≤ h) (hq0 : 0 ≤ q) (hq1 : q ≤ 1)
    (hq : ∀ i j, q ≤ p i j) (hp1 : ∀ i j, p i j ≤ 1) :
    (1 - (1 - q) ^ t) / (2 - (1 - q) ^ t) ≤
      expectP p (fun E : Fin h → Fin h → Bool => (M t E : ℝ)) / h := by
  have hh : (0 : ℝ) < h := by exact_mod_cast (show 0 < h by omega)
  set e := (1 - q) ^ t
  have he0 : 0 ≤ e := pow_nonneg (by linarith) t
  have he1 : e ≤ 1 := pow_le_one₀ (by linarith) (by linarith)
  -- T_i: taken vertices in i's window when i is processed
  let T : Fin h → (Fin h → Fin h → Bool) → ℕ := fun i E => (window t i ∩ takenAt t E i.val).card
  -- unmatched_i = [row i avoids the free part of i's window]
  have hp0 : ∀ i j, 0 ≤ p i j := fun i j => le_trans hq0 (hq i j)
  have hU : ∀ i : Fin h, expectP p (fun E => if matched t E i then 0 else 1) ≤
      expectP p (fun E => (1 - q) ^ (t - T i E)) := by
    intro i
    have hrow := expectP_row_avoid p i (fun E => window t i \ takenAt t E i.val) (fun _ => 1)
      (fun E r => by simp only [takenAt_indep]) (fun _ _ => rfl)
    simp only [mul_one] at hrow
    have hcard : ∀ E, (window t i \ takenAt t E i.val).card = t - T i E := by
      intro E
      have := Finset.card_sdiff_add_card_inter (window t i) (takenAt t E i.val)
      rw [card_window t ht] at this
      show _ = t - (window t i ∩ takenAt t E i.val).card
      omega
    have hind : expectP p (fun E => if matched t E i then 0 else 1) =
        expectP p (fun E => if ∀ j ∈ window t i \ takenAt t E i.val, E i j = false then 1 else 0) := by
      unfold expectP; apply Finset.sum_congr rfl; intro E _
      congr 1
      by_cases hm : matched t E i
      · have : ¬ ∀ j ∈ window t i \ takenAt t E i.val, E i j = false := fun H => ((unmatched_iff t E i).2 H) hm
        simp only [hm, this, ite_true, ite_false]
      · simp only [hm, ite_false]
        rw [ite_eq_left_of_eq_true _ _ (eq_true ((unmatched_iff t E i).1 hm))]
    rw [hind, hrow]
    apply expectP_mono hp0 hp1; intro E
    rw [← hcard E]
    exact prod_le_pow q hq hp1 i _

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
  have hEM : expectP p (fun E : Fin h → Fin h → Bool => (h : ℝ) - M t E) = h - expectP p (fun E : Fin h → Fin h → Bool => (M t E : ℝ)) := by
    unfold expectP
    simp_rw [mul_sub, Finset.sum_sub_distrib, ← Finset.sum_mul, sum_weightP, one_mul]
  have hbound : h - expectP p (fun E : Fin h → Fin h → Bool => (M t E : ℝ)) ≤ h * e + (1 - e) * expectP p (fun E : Fin h → Fin h → Bool => (M t E : ℝ)) := by
    rw [← hEM]
    calc expectP p (fun E : Fin h → Fin h → Bool => (h : ℝ) - M t E)
        = expectP p (fun E => ∑ i : Fin h, (if matched t E i then (0 : ℝ) else 1)) := by
          unfold expectP; apply Finset.sum_congr rfl; intro E _; beta_reduce; rw [hUM]
      _ ≤ ∑ i, expectP p (fun E => (1 - q) ^ (t - T i E)) := by
          rw [expectP_sum]; exact Finset.sum_le_sum (fun i _ => hU i)
      _ ≤ ∑ i, expectP p (fun E => e + ((T i E : ℝ) / t) * (1 - e)) := by
          apply Finset.sum_le_sum; intro i _; exact expectP_mono hp0 hp1 _ _ (hchord i)
      _ = expectP p (fun E : Fin h → Fin h → Bool => h * e + (1 - e) / t * ∑ i, (T i E : ℝ)) := by
          rw [← expectP_sum]; unfold expectP; apply Finset.sum_congr rfl; intro E _
          congr 1
          beta_reduce
          rw [Finset.sum_add_distrib, Finset.sum_const, Finset.card_univ, Fintype.card_fin, nsmul_eq_mul,
            Finset.mul_sum]
          congr 1; apply Finset.sum_congr rfl; intro i _; ring
      _ ≤ expectP p (fun E : Fin h → Fin h → Bool => h * e + (1 - e) * M t E) := by
          apply expectP_mono hp0 hp1; intro E
          have ht' : (0 : ℝ) < t := by exact_mod_cast ht0
          have hc : 0 ≤ (1 - e) / t := div_nonneg (by linarith) ht'.le
          calc h * e + (1 - e) / t * ∑ i, (T i E : ℝ) ≤ h * e + (1 - e) / t * (t * M t E) := by
                gcongr; exact hsumT E
            _ = h * e + (1 - e) * M t E := by field_simp
      _ = h * e + (1 - e) * expectP p (fun E : Fin h → Fin h → Bool => (M t E : ℝ)) := by
          unfold expectP
          simp_rw [mul_add, Finset.sum_add_distrib, ← Finset.sum_mul, sum_weightP, one_mul]
          congr 1; rw [Finset.mul_sum]; apply Finset.sum_congr rfl; intro _ _; ring
  exact matches_bound h _ e hh he1 hbound

end MeshTheory.Prob
