import MeshTheory.Prob.Hetero
import MeshTheory.SplitMesher
/-!
# Lemma 5.3 in the string model (the paper's model), corrected-good-match form

Spans are uniformly random `r`-subsets of `b` slots, all independent: `n` left
spans `u` and `n` right spans `v`.  Two spans mesh iff disjoint.

* `double_star_count`: for the corrected good-match event of `(u_i, v_j)`
  (`good_match_found`), the number of configurations is
  `C · A · (C - A)^(m+l) · C^(n-1-m) · C^(n-1-l)`, where `C = C(b, r)`,
  `A = C(b - r, r)`, `m = |J'|` (earlier probes of `u_i`) and `l = |I'|`
  (earlier left spans whose windows contain `v_j`).  That is probability
  `q (1-q)^(m+l)` with `q = A/C`: on this tree of pairs the mesh events are
  exactly independent, as the paper's acyclic-independence argument says.
-/

namespace MeshTheory.Prob

open Finset BigOperators

variable {b r : ℕ}

/-- A span: an `r`-subset of `b` slots. -/
abbrev Span (b r : ℕ) := {s : Finset (Fin b) // s.card = r}

theorem card_span : Fintype.card (Span b r) = b.choose r := by
  rw [Fintype.card_finset_len, Fintype.card_fin]

/-- Spans disjoint from a fixed set `T`: `C(b - |T|, r)`. -/
theorem card_disjoint (T : Finset (Fin b)) :
    (univ.filter fun w : Span b r => Disjoint w.1 T).card = (b - T.card).choose r := by
  have : (univ.filter fun w : Span b r => Disjoint w.1 T).card = (Tᶜ.powersetCard r).card := by
    apply Finset.card_bij (fun w _ => w.1)
    · intro w hw
      simp only [mem_filter, mem_univ, true_and] at hw
      rw [mem_powersetCard]
      exact ⟨Finset.subset_compl_iff_disjoint_right.2 hw, w.2⟩
    · intro a _ c _ h; exact Subtype.ext h
    · intro s hs
      rw [mem_powersetCard] at hs
      exact ⟨⟨s, hs.2⟩, by simp [Finset.subset_compl_iff_disjoint_right.1 hs.1], rfl⟩
  rw [this, card_powersetCard, card_compl, Fintype.card_fin]

/-- Spans meeting a fixed span `y` (both `r` objects): `C - A`. -/
theorem card_meeting (y : Span b r) :
    (univ.filter fun w : Span b r => ¬ Disjoint w.1 y.1).card = b.choose r - (b - r).choose r := by
  have h := Finset.card_filter_add_card_filter_not (s := (univ : Finset (Span b r)))
    (fun w : Span b r => Disjoint w.1 y.1)
  rw [card_disjoint, y.2, card_univ, card_span] at h
  omega

/-- Product over `Fin n` of a function that is `a` at `i`, `f k` on `I` and `c`
elsewhere. -/
theorem prod_star {α : Type*} [CommMonoid α] {n : ℕ} (i : Fin n) (I : Finset (Fin n)) (hi : i ∉ I)
    (a : Fin n → α) (c : α) (f : Fin n → α) :
    ∏ k, (if k = i then a k else if k ∈ I then f k else c) = a i * (∏ k ∈ I, f k) * c ^ (n - 1 - I.card) := by
  rw [Finset.prod_ite, Finset.filter_eq' univ i, if_pos (mem_univ i), prod_singleton, mul_assoc]
  congr 1
  rw [Finset.prod_ite]
  have h1 : (univ.filter fun k => ¬ k = i).filter (· ∈ I) = I := by
    ext k; simp only [mem_filter, mem_univ, true_and]
    constructor
    · exact fun h => h.2
    · intro hk; exact ⟨fun h => hi (h ▸ hk), hk⟩
  have h2 : ((univ.filter fun k => ¬ k = i).filter fun k => k ∉ I).card = n - 1 - I.card := by
    have e1 : (univ.filter fun k : Fin n => ¬ k = i).card = n - 1 := by
      rw [Finset.filter_ne' univ i, card_erase_of_mem (mem_univ i), card_univ, Fintype.card_fin]
    have e2 := Finset.card_filter_add_card_filter_not (s := univ.filter fun k : Fin n => ¬ k = i)
      (fun k => k ∈ I)
    rw [h1, e1] at e2
    omega
  rw [h1, prod_const, h2]

/-- Disjointness indicator. -/
def mi (x y : Span b r) : ℕ := if Disjoint x.1 y.1 then 1 else 0
/-- Overlap indicator. -/
def ni (x y : Span b r) : ℕ := if Disjoint x.1 y.1 then 0 else 1

theorem sum_mi (y : Span b r) : ∑ w, mi w y = (b - r).choose r := by
  unfold mi; rw [Finset.sum_boole, Nat.cast_id, card_disjoint, y.2]
theorem sum_ni (y : Span b r) : ∑ w, ni w y = b.choose r - (b - r).choose r := by
  unfold ni
  rw [show (fun w => if Disjoint w.1 y.1 then 0 else 1) = (fun w : Span b r => if ¬ Disjoint w.1 y.1 then 1 else 0)
    by funext w; split <;> simp_all]
  rw [Finset.sum_boole, Nat.cast_id, card_meeting]
theorem sum_mi' (x : Span b r) : ∑ w, mi x w = (b - r).choose r := by
  rw [← sum_mi x]; apply Finset.sum_congr rfl; intro w _; unfold mi; simp [disjoint_comm]
theorem sum_ni' (x : Span b r) : ∑ w, ni x w = b.choose r - (b - r).choose r := by
  rw [← sum_ni x]; apply Finset.sum_congr rfl; intro w _; unfold ni; simp [disjoint_comm]

/-- The good-match indicator: `u_i, v_j` mesh, `u_i` meets every `v_j'`
(`j' ∈ J`), and every `u_i'` (`i' ∈ I`) meets `v_j`. -/
def goodInd {n : ℕ} (i j : Fin n) (I J : Finset (Fin n)) (u v : Fin n → Span b r) : ℕ :=
  mi (u i) (v j) * (∏ j' ∈ J, ni (u i) (v j')) * ∏ i' ∈ I, ni (u i') (v j)

/-- **Double-star count**: exact independence on the good-match tree. -/
theorem double_star_count {n : ℕ} (i j : Fin n) (I J : Finset (Fin n)) (hi : i ∉ I) (hj : j ∉ J) :
    ∑ u : Fin n → Span b r, ∑ v : Fin n → Span b r, goodInd i j I J u v =
      (b.choose r) * ((b - r).choose r) * (b.choose r - (b - r).choose r) ^ (J.card + I.card) *
        (b.choose r) ^ (n - 1 - J.card) * (b.choose r) ^ (n - 1 - I.card) := by
  set C := b.choose r
  set A := (b - r).choose r
  rw [Finset.sum_comm]
  -- for fixed v: sum over u factors
  have hu : ∀ v : Fin n → Span b r, ∑ u : Fin n → Span b r, goodInd i j I J u v =
      (∑ w, mi w (v j) * ∏ j' ∈ J, ni w (v j')) * (C - A) ^ I.card * C ^ (n - 1 - I.card) := by
    intro v
    let f : Fin n → Span b r → ℕ := fun k w =>
      if k = i then mi w (v j) * ∏ j' ∈ J, ni w (v j') else if k ∈ I then ni w (v j) else 1
    have hsum : ∀ k, ∑ w, f k w =
        if k = i then ∑ w, mi w (v j) * ∏ j' ∈ J, ni w (v j') else if k ∈ I then C - A else C := by
      intro k
      simp only [f]
      split
      · rfl
      · split
        · exact sum_ni (v j)
        · simp [card_span, C]
    calc ∑ u : Fin n → Span b r, goodInd i j I J u v = ∑ u : Fin n → Span b r, ∏ k, f k (u k) := by
          apply Finset.sum_congr rfl; intro u _
          simp only [f]
          rw [prod_star i I hi (fun k => mi (u k) (v j) * ∏ j' ∈ J, ni (u k) (v j')) 1
            (fun k => ni (u k) (v j)), one_pow, mul_one]
          rfl
      _ = ∏ k, ∑ w, f k w := (Fintype.prod_sum f).symm
      _ = _ := by
          simp_rw [hsum]
          rw [prod_star i I hi (fun _ => ∑ w, mi w (v j) * ∏ j' ∈ J, ni w (v j')) C (fun _ => C - A),
            prod_const]
  simp_rw [hu]
  rw [← Finset.sum_mul, ← Finset.sum_mul, Finset.sum_comm]
  -- for fixed w: sum over v factors
  have hv : ∀ w : Span b r, ∑ v : Fin n → Span b r, mi w (v j) * ∏ j' ∈ J, ni w (v j') =
      A * (C - A) ^ J.card * C ^ (n - 1 - J.card) := by
    intro w
    let f : Fin n → Span b r → ℕ := fun k x => if k = j then mi w x else if k ∈ J then ni w x else 1
    have hsum : ∀ k, ∑ x, f k x = if k = j then A else if k ∈ J then C - A else C := by
      intro k
      simp only [f]
      split
      · exact sum_mi' w
      · split
        · exact sum_ni' w
        · simp [card_span, C]
    calc ∑ v : Fin n → Span b r, mi w (v j) * ∏ j' ∈ J, ni w (v j') = ∑ v : Fin n → Span b r, ∏ k, f k (v k) := by
          apply Finset.sum_congr rfl; intro v _
          simp only [f]
          rw [prod_star j J hj (fun k => mi w (v k)) 1 (fun k => ni w (v k)), one_pow, mul_one]
      _ = ∏ k, ∑ x, f k x := (Fintype.prod_sum f).symm
      _ = _ := by
          simp_rw [hsum]
          rw [prod_star j J hj (fun _ => A) C (fun _ => C - A), prod_const]
  simp_rw [hv]
  rw [Finset.sum_const, card_univ, card_span, smul_eq_mul, pow_add]
  ring

/-! ### Connecting to SplitMesher -/

variable {n : ℕ}

/-- The meshing graph of a configuration, as SplitMesher's edge oracle. -/
def Eof (u v : Fin n → Span b r) : ℕ → ℕ → Bool := fun i j =>
  if h : i < n ∧ j < n then decide (Disjoint (u ⟨i, h.1⟩).1 (v ⟨j, h.2⟩).1) else false

/-- Earlier probes of `u_i` before `v_j`. -/
def Jset (i j : Fin n) : Finset (Fin n) := univ.filter fun j' => i.val ≤ j'.val ∧ j'.val < j.val
/-- Earlier left spans whose windows contain `v_j`. -/
def Iset (t : ℕ) (i j : Fin n) : Finset (Fin n) := univ.filter fun i' => i'.val < i.val ∧ j.val < i'.val + t

theorem goodInd_le_one (i j : Fin n) (I J : Finset (Fin n)) (u v : Fin n → Span b r) :
    goodInd i j I J u v ≤ 1 := by
  unfold goodInd
  have h1 : mi (u i) (v j) ≤ 1 := by unfold mi; split <;> omega
  have h2 : ∏ j' ∈ J, ni (u i) (v j') ≤ 1 := Finset.prod_le_one' (fun _ _ => by unfold ni; split <;> omega)
  have h3 : ∏ i' ∈ I, ni (u i') (v j) ≤ 1 := Finset.prod_le_one' (fun _ _ => by unfold ni; split <;> omega)
  calc _ ≤ 1 * 1 * 1 := Nat.mul_le_mul (Nat.mul_le_mul h1 h2) h3
    _ = 1 := by norm_num

theorem goodInd_pos (t : ℕ) (i j : Fin n) (u v : Fin n → Span b r)
    (hg : 0 < goodInd i j (Iset t i j) (Jset i j) u v) :
    Eof u v i j = true ∧ (∀ j', i.val ≤ j' → j' < j.val → Eof u v i j' = false) ∧
      (∀ i', i' < i.val → j.val < i' + t → Eof u v i' j = false) := by
  unfold goodInd at hg
  have hg' := Nat.pos_iff_ne_zero.1 hg
  rw [mul_ne_zero_iff, mul_ne_zero_iff, Finset.prod_ne_zero_iff, Finset.prod_ne_zero_iff] at hg'
  obtain ⟨⟨hm, hJ⟩, hI⟩ := hg'
  refine ⟨?_, ?_, ?_⟩
  · unfold mi at hm; unfold Eof; simp only [i.isLt, j.isLt, and_self, dite_true]
    split at hm <;> simp_all
  · intro j' h1 h2
    have hj'n : j' < n := by omega
    have := hJ ⟨j', hj'n⟩ (by simp only [Jset, mem_filter, mem_univ, true_and]; exact ⟨h1, h2⟩)
    unfold ni at this; unfold Eof; simp only [i.isLt, hj'n, and_self, dite_true]
    split at this <;> simp_all
  · intro i' h1 h2
    have hi'n : i' < n := by omega
    have := hI ⟨i', hi'n⟩ (by simp only [Iset, mem_filter, mem_univ, true_and]; exact ⟨h1, h2⟩)
    unfold ni at this; unfold Eof; simp only [hi'n, j.isLt, and_self, dite_true]
    split at this <;> simp_all

/-- Window positions of `u_i`. -/
def Wset (t : ℕ) (i : Fin n) : Finset (Fin n) := univ.filter fun j => i.val ≤ j.val ∧ j.val < i.val + t

/-- At most one good match per left span, and it is in the matching. -/
theorem sum_good_le_count (t : ℕ) (i : Fin n) (u v : Fin n → Span b r) :
    ∑ j ∈ Wset t i, goodInd i j (Iset t i j) (Jset i j) u v ≤
      (splitMesher (Eof u v) n t).countP (fun p => p.1 = i.val) := by
  by_cases hex : ∃ j ∈ Wset t i, 0 < goodInd i j (Iset t i j) (Jset i j) u v
  · obtain ⟨j0, hj0W, hj0⟩ := hex
    have hW := hj0W
    simp only [Wset, mem_filter, mem_univ, true_and] at hW
    have huniq : ∀ j ∈ Wset t i, j ≠ j0 → goodInd i j (Iset t i j) (Jset i j) u v = 0 := by
      intro j hjW hne
      by_contra hpos
      have hp := Nat.pos_of_ne_zero hpos
      have g1 := goodInd_pos t i j u v hp
      have g0 := goodInd_pos t i j0 u v hj0
      simp only [Wset, mem_filter, mem_univ, true_and] at hjW
      rcases Nat.lt_or_gt_of_ne (Fin.val_ne_of_ne hne) with hlt | hlt
      · rw [g0.2.1 j.val hjW.1 hlt] at g1; exact absurd g1.1 (by simp)
      · rw [g1.2.1 j0.val hW.1 hlt] at g0; exact absurd g0.1 (by simp)
    rw [Finset.sum_eq_single j0 huniq (fun h => absurd hj0W h)]
    have hmem : (i.val, j0.val) ∈ splitMesher (Eof u v) n t := by
      obtain ⟨e1, e2, e3⟩ := goodInd_pos t i j0 u v hj0
      exact good_match_found (Eof u v) n t i.val j0.val i.isLt j0.isLt hW.1 hW.2 e1 e2 e3
    have : 0 < (splitMesher (Eof u v) n t).countP (fun p => p.1 = i.val) :=
      List.countP_pos_iff.2 ⟨_, hmem, by simp⟩
    exact le_trans (goodInd_le_one _ _ _ _ _ _) this
  · push Not at hex
    rw [Finset.sum_eq_zero (fun j hj => Nat.le_zero.1 (hex j hj))]
    exact Nat.zero_le _

theorem sum_countP_le (L : List (ℕ × ℕ)) :
    ∑ i : Fin n, L.countP (fun p => p.1 = i.val) ≤ L.length := by
  induction L with
  | nil => simp
  | cons p L ih =>
    simp only [List.countP_cons, Finset.sum_add_distrib, List.length_cons]
    have : ∑ i : Fin n, (if decide (p.1 = i.val) = true then 1 else 0) ≤ 1 := by
      simp only [decide_eq_true_eq]
      rw [Finset.sum_boole, Nat.cast_id, Finset.card_le_one]
      intro a ha c hc
      simp only [mem_filter, mem_univ, true_and] at ha hc
      exact Fin.ext (ha ▸ hc ▸ rfl)
    omega

/-- **Pathwise**: good matches are at most the matches. -/
theorem good_le_matches (t : ℕ) (u v : Fin n → Span b r) :
    ∑ i : Fin n, ∑ j ∈ Wset t i, goodInd i j (Iset t i j) (Jset i j) u v ≤ (splitMesher (Eof u v) n t).length :=
  le_trans (Finset.sum_le_sum fun i _ => sum_good_le_count t i u v) (sum_countP_le _)

theorem card_Jset (i j : Fin n) (hij : i.val ≤ j.val) : (Jset i j).card ≤ j.val - i.val := by
  calc (Jset i j).card ≤ (Finset.Ico i.val j.val).card := by
        apply Finset.card_le_card_of_injOn (fun x => x.val)
        · intro x hx; simp only [Jset, coe_filter, mem_univ, true_and, Set.mem_setOf_eq] at hx
          simp [hx.1, hx.2]
        · intro x _ y _ h; exact Fin.ext h
    _ = j.val - i.val := Nat.card_Ico _ _

theorem card_Iset (t : ℕ) (i j : Fin n) : (Iset t i j).card ≤ i.val + t - 1 - j.val := by
  calc (Iset t i j).card ≤ (Finset.Ico (j.val + 1 - t) i.val).card := by
        apply Finset.card_le_card_of_injOn (fun x => x.val)
        · intro x hx; simp only [Iset, coe_filter, mem_univ, true_and, Set.mem_setOf_eq] at hx
          simp only [coe_Ico, Set.mem_Ico]; omega
        · intro x _ y _ h; exact Fin.ext h
    _ = i.val - (j.val + 1 - t) := Nat.card_Ico _ _
    _ ≤ i.val + t - 1 - j.val := by omega

/-- **Lemma 5.3, string model**: summed over all configurations,
`Σ M ≥ Σ_i Σ_{j ∈ window} C A (C-A)^(m+l) C^(n-1-m) C^(n-1-l)`. -/
theorem string_matches_ge (t : ℕ) :
    ∑ i : Fin n, ∑ j ∈ Wset t i,
      (b.choose r) * ((b - r).choose r) * (b.choose r - (b - r).choose r) ^ ((Jset i j).card + (Iset t i j).card) *
        (b.choose r) ^ (n - 1 - (Jset i j).card) * (b.choose r) ^ (n - 1 - (Iset t i j).card) ≤
      ∑ u : Fin n → Span b r, ∑ v : Fin n → Span b r, (splitMesher (Eof u v) n t).length := by
  calc _ = ∑ i : Fin n, ∑ j ∈ Wset t i, ∑ u : Fin n → Span b r, ∑ v : Fin n → Span b r,
        goodInd i j (Iset t i j) (Jset i j) u v := by
        apply Finset.sum_congr rfl; intro i _; apply Finset.sum_congr rfl; intro j _
        rw [double_star_count i j _ _ (by simp [Iset]) (by simp [Jset])]
    _ = ∑ u : Fin n → Span b r, ∑ v : Fin n → Span b r, ∑ i : Fin n, ∑ j ∈ Wset t i,
        goodInd i j (Iset t i j) (Jset i j) u v := by
        symm
        -- Σ_u Σ_v Σ_i Σ_j = Σ_i Σ_j Σ_u Σ_v (the window W i doesn't depend on u, v)
        have e1 : ∀ u : Fin n → Span b r, (∑ v : Fin n → Span b r, ∑ i : Fin n, ∑ j ∈ Wset t i,
            goodInd i j (Iset t i j) (Jset i j) u v) =
            ∑ i : Fin n, ∑ j ∈ Wset t i, ∑ v : Fin n → Span b r, goodInd i j (Iset t i j) (Jset i j) u v := by
          intro u
          rw [Finset.sum_comm]
          apply Finset.sum_congr rfl; intro i _
          rw [Finset.sum_comm]
        simp_rw [e1]
        rw [Finset.sum_comm]
        apply Finset.sum_congr rfl; intro i _
        rw [Finset.sum_comm]
    _ ≤ _ := by
        apply Finset.sum_le_sum; intro u _; apply Finset.sum_le_sum; intro v _
        exact good_le_matches t u v

theorem card_lt_n_of_not_mem (S : Finset (Fin n)) (x : Fin n) (hx : x ∉ S) : S.card ≤ n - 1 := by
  have : S ⊂ univ := Finset.ssubset_iff_subset_ne.2 ⟨subset_univ _, fun h => hx (h ▸ mem_univ x)⟩
  have := Finset.card_lt_card this
  rw [card_univ, Fintype.card_fin] at this
  omega

/-- **Lemma 5.3 in the string model** (the paper's model, corrected good
matches): with `q = C(b-r, r)/C(b, r)` the probability two spans mesh,
`E[M] ≥ q (1-q)^(t-1) Σ_i |window_i|`.  With `t = k/q` that is about
`k e^{-k}` per left span: weaker than the independent-edge bound for large `k`
(the good-match argument requires every earlier window to miss `v_j`), but
proved in the paper's own model. -/
theorem string_lemma53 (t : ℕ) (ht : 0 < t) (hC : 0 < b.choose r) :
    ((b - r).choose r / b.choose r : ℝ) * (1 - (b - r).choose r / b.choose r) ^ (t - 1) *
        ∑ i : Fin n, ((Wset t i).card : ℝ) ≤
      (∑ u : Fin n → Span b r, ∑ v : Fin n → Span b r, ((splitMesher (Eof u v) n t).length : ℝ)) /
        (b.choose r : ℝ) ^ (2 * n) := by
  set C := b.choose r
  set A := (b - r).choose r
  have hAC : A ≤ C := Nat.choose_le_choose r (Nat.sub_le b r)
  have hCr : (0 : ℝ) < C := by exact_mod_cast hC
  set q : ℝ := A / C
  have hq0 : 0 ≤ q := by positivity
  have hq1 : q ≤ 1 := by rw [div_le_one hCr]; exact_mod_cast hAC
  have hmain := string_matches_ge (b := b) (r := r) (n := n) t
  have hmainR : (∑ i : Fin n, ∑ j ∈ Wset t i,
      ((C * A * (C - A) ^ ((Jset i j).card + (Iset t i j).card) * C ^ (n - 1 - (Jset i j).card) *
        C ^ (n - 1 - (Iset t i j).card) : ℕ) : ℝ)) ≤
      ∑ u : Fin n → Span b r, ∑ v : Fin n → Span b r, ((splitMesher (Eof u v) n t).length : ℝ) := by
    exact_mod_cast hmain
  rw [le_div_iff₀ (by positivity)]
  refine le_trans ?_ hmainR
  rw [Finset.mul_sum, Finset.sum_mul]
  apply Finset.sum_le_sum; intro i _
  rw [Finset.card_eq_sum_ones, Nat.cast_sum, Finset.mul_sum, Finset.sum_mul]
  apply Finset.sum_le_sum; intro j hj
  simp only [Wset, mem_filter, mem_univ, true_and] at hj
  set m := (Jset i j).card
  set l := (Iset t i j).card
  have hm := card_Jset i j hj.1
  have hl := card_Iset t i j
  have hmn : m ≤ n - 1 := card_lt_n_of_not_mem _ j (by simp [Jset])
  have hln : l ≤ n - 1 := card_lt_n_of_not_mem _ i (by simp [Iset])
  have hml : m + l ≤ t - 1 := by omega
  have hn : 1 ≤ n := by have := i.isLt; omega
  push_cast
  rw [Nat.cast_sub hAC]
  -- term = q (1-q)^(m+l) C^(2n), and (1-q)^(m+l) ≥ (1-q)^(t-1)
  have hpow : (1 - q) ^ (t - 1) ≤ (1 - q) ^ (m + l) := pow_le_pow_of_le_one (by linarith) (by linarith) hml
  have hexp : (C : ℝ) ^ (2 * n) = C * C * C ^ (m + l) * C ^ (n - 1 - m) * C ^ (n - 1 - l) := by
    rw [← pow_two, ← pow_add, ← pow_add, ← pow_add]; congr 1; omega
  have hq : (A : ℝ) = q * C := by simp only [q]; field_simp
  have h1q : (C : ℝ) - A = (1 - q) * C := by rw [hq]; ring
  rw [hexp, h1q, hq, mul_pow]
  have : 0 ≤ (C : ℝ) := hCr.le
  have hc : 0 ≤ q * (C : ℝ) * C * (C : ℝ) ^ (m + l) * C ^ (n - 1 - m) * C ^ (n - 1 - l) := by positivity
  calc q * (1 - q) ^ (t - 1) * 1 * (C * C * C ^ (m + l) * C ^ (n - 1 - m) * C ^ (n - 1 - l))
      = (1 - q) ^ (t - 1) * (q * C * C * C ^ (m + l) * C ^ (n - 1 - m) * C ^ (n - 1 - l)) := by ring
    _ ≤ (1 - q) ^ (m + l) * (q * C * C * C ^ (m + l) * C ^ (n - 1 - m) * C ^ (n - 1 - l)) :=
        mul_le_mul_of_nonneg_right hpow hc
    _ = C * (q * C) * ((1 - q) ^ (m + l) * C ^ (m + l)) * C ^ (n - 1 - m) * C ^ (n - 1 - l) := by ring

end MeshTheory.Prob
