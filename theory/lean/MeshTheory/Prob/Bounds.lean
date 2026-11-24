import Mathlib
/-!
# Real-valued bounds for Lemma 5.3

* `one_sub_pow_le_exp`: `(1 - q)^t ≤ exp (-(q t))`.
* `chord`: for `0 ≤ y ≤ 1` and `T ≤ t`, `y^(t-T) ≤ y^t + (T/t)(1 - y^t)` (convexity).
* `matches_bound`: if `h - m ≤ h e + (1 - e) m` then `m / h ≥ (1 - e) / (2 - e)`.
* `bound_antitone`: `(1 - e)/(2 - e)` decreases in `e`, so `e ≤ exp (-k)`
  turns the bound into `(1 - exp (-k)) / (2 - exp (-k))`.
-/

namespace MeshTheory.Prob

open Real

theorem one_sub_pow_le_exp (q : ℝ) (hq1 : q ≤ 1) (t : ℕ) :
    (1 - q) ^ t ≤ exp (-(q * t)) := by
  have h1 : 1 - q ≤ exp (-q) := by linarith [add_one_le_exp (-q)]
  calc (1 - q) ^ t ≤ (exp (-q)) ^ t := pow_le_pow_left₀ (by linarith) h1 t
    _ = exp (-(q * t)) := by rw [← exp_nat_mul]; ring_nf

/-- Chord bound from convexity of `s ↦ y^(t - s)`. -/
theorem chord (y : ℝ) (hy0 : 0 ≤ y) (hy1 : y ≤ 1) (t T : ℕ) (hT : T ≤ t) (ht : 0 < t) :
    y ^ (t - T) ≤ y ^ t + (T / t) * (1 - y ^ t) := by
  rcases eq_or_lt_of_le hy0 with h0 | hpos
  · -- y = 0
    subst h0
    rcases eq_or_lt_of_le hT with hTt | hTt
    · subst hTt; simp [Nat.sub_self, zero_pow ht.ne', div_self (by exact_mod_cast ht.ne' : (T : ℝ) ≠ 0)]
    · rw [zero_pow (by omega), zero_pow ht.ne']; simp; positivity
  · -- y > 0: y^(t-s) = exp ((t - s) log y), convex in s
    have hconv : ConvexOn ℝ Set.univ (fun s : ℝ => exp ((t - s) * log y)) := by
      have : (fun s : ℝ => exp ((t - s) * log y)) = exp ∘ (fun s => (t - s) * log y) := rfl
      rw [this]
      exact convexOn_exp.comp_affineMap
        ⟨fun s => (t - s) * log y, (-(log y)) • LinearMap.id, by intro p v; simp; ring⟩
    set θ : ℝ := T / t
    have hθ0 : 0 ≤ θ := by positivity
    have hθ1 : θ ≤ 1 := by rw [div_le_one (by exact_mod_cast ht)]; exact_mod_cast hT
    have key := hconv.2 (Set.mem_univ (0 : ℝ)) (Set.mem_univ (t : ℝ)) (by linarith : 0 ≤ 1 - θ) hθ0
      (by ring)
    simp only [smul_eq_mul, mul_zero, zero_add] at key
    have hpt : θ * (t : ℝ) = T := by
      simp only [θ]; field_simp
    rw [hpt] at key
    have e1 : exp (((t : ℝ) - T) * log y) = y ^ (t - T) := by
      rw [mul_comm, ← rpow_def_of_pos hpos, show ((t : ℝ) - T) = ((t - T : ℕ) : ℝ) by push_cast [hT]; ring,
        rpow_natCast]
    have e2 : exp (((t : ℝ) - 0) * log y) = y ^ t := by
      rw [sub_zero, mul_comm, ← rpow_def_of_pos hpos, rpow_natCast]
    have e3 : exp (((t : ℝ) - t) * log y) = 1 := by simp
    rw [e1, e2, e3] at key
    linarith

theorem matches_bound (h m e : ℝ) (hh : 0 < h) (he1 : e ≤ 1)
    (hU : h - m ≤ h * e + (1 - e) * m) : (1 - e) / (2 - e) ≤ m / h := by
  rw [div_le_div_iff₀ (by linarith) hh]
  nlinarith

theorem bound_antitone (e e' : ℝ) (he : e ≤ e') (he' : e' ≤ 1) :
    (1 - e') / (2 - e') ≤ (1 - e) / (2 - e) := by
  rw [div_le_div_iff₀ (by linarith) (by linarith)]
  nlinarith

/-- Where the repaired bound sits: with `e = exp (-k)`,
`(1 - e)/2 ≤ (1 - e)/(2 - e) ≤ (1 - e^2)/2`, the upper end being the paper's
`(1 - exp (-2k))/2`.  Both ends tend to `1/2`. -/
theorem repaired_vs_paper (e : ℝ) (he0 : 0 ≤ e) (he1 : e ≤ 1) :
    (1 - e) / 2 ≤ (1 - e) / (2 - e) ∧ (1 - e) / (2 - e) ≤ (1 - e ^ 2) / 2 := by
  constructor
  · rw [div_le_div_iff₀ (by norm_num) (by linarith)]; nlinarith
  · rw [div_le_div_iff₀ (by linarith) (by norm_num)]
    nlinarith [mul_nonneg (mul_nonneg he0 (sub_nonneg.2 he1)) (sub_nonneg.2 he1)]

end MeshTheory.Prob
