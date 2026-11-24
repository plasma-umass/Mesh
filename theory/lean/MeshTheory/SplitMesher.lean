import MeshTheory.Paper
/-!
# §5.3: SplitMesher and Lemma 5.3

SplitMesher (Figure 2 of the paper), on an abstract bipartite graph: left
vertices `u_0 … u_{n-1}`, right vertices `v_0 … v_{n-1}`, edge oracle `E`.
Left vertex `i` probes right vertices `i, i+1, …, i+t-1` (those `< n`) and is
matched to the first one that has an edge and is still free.

**Good matches.**  The paper's proof calls `(u_i, v_j)` good if (1) the edge is
present, (2) `u_i` has no edge to `v_{j'}` for `i ≤ j' < j`, and (3) "no edges
between `u_{i'}` and `v_j` for `i < i' ≤ j`".  Condition (3) as printed
constrains *later* left vertices; what keeps `v_j` free when `u_i` reaches it
is that no *earlier* `u_{i'}` whose window contains `j` (`j - t < i' < i`) took
it.  `good_match_found` proves that the corrected condition suffices.  With
it, a good match at offset `d = j - i` has probability `q (1-q)^(d + t-1-d)`,
the same for every `d`, so the proof gives only `r = t q (1-q)^(t-1) ≈ k e^{-k}`.
That isn't the stated `(1 - e^{-2k})/2`, and it vanishes as `k` grows.

**A repaired argument** (`taken_in_windows`, `chord_bound`, `lemma53_core`):
1. Deterministically, every right vertex lies in at most `t` windows, so if
   `T_i` is the number of taken vertices in `u_i`'s window when `u_i` is
   processed, `Σ T_i ≤ t M`, where `M` is the number of matches.
2. With independent edges, revealed only when probed (each pair is probed at
   most once), `u_i` stays unmatched with probability `(1-q)^(t - T_i)`.
3. By convexity, `(1-q)^(t-T) ≤ (1-q)^t + (T/t)(1 - (1-q)^t)` for `0 ≤ T ≤ t`.
4. Summing, `n - E[M] ≤ n (1-q)^t + (1 - (1-q)^t) E[M]`, so
   `E[M] / n ≥ (1 - (1-q)^t) / (2 - (1-q)^t) ≥ (1 - e^{-k}) / (2 - e^{-k})`.
   This is close to the paper's `(1 - e^{-2k})/2` and also tends to 1/2.
Steps 1, 3 and 4 are proved here.  Step 2 is the probabilistic modelling step,
and holds for independent edges.  For Mesh's string model, edges are only
independent on acyclic sets, as in the paper.
-/

namespace MeshTheory

/-! ### The algorithm -/

/-- First free neighbour of `i` among `j, j+1, …` (`fuel` candidates, `< n`). -/
def firstFree (E : Nat → Nat → Bool) (taken : Nat → Bool) (n i : Nat) : Nat → Nat → Option Nat
  | 0, _ => none
  | fuel + 1, j => if j < n ∧ E i j = true ∧ taken j = false then some j else firstFree E taken n i fuel (j + 1)

/-- Process left vertices `0 … k-1`; returns which right vertices are taken and
the matching (as `(i, j)` pairs). -/
def run (E : Nat → Nat → Bool) (n t : Nat) : Nat → (Nat → Bool) × List (Nat × Nat)
  | 0 => (fun _ => false, [])
  | k + 1 =>
    let r := run E n t k
    match firstFree E r.1 n k t k with
    | some j => (fun x => r.1 x || x == j, r.2 ++ [(k, j)])
    | none => r

def splitMesher (E : Nat → Nat → Bool) (n t : Nat) : List (Nat × Nat) := (run E n t n).2

/-! ### Every corrected good match is found -/

theorem firstFree_spec (E : Nat → Nat → Bool) (taken : Nat → Bool) (n i : Nat) :
    ∀ fuel j0 j, firstFree E taken n i fuel j0 = some j →
      j0 ≤ j ∧ j < j0 + fuel ∧ j < n ∧ E i j = true ∧ taken j = false
  | 0, _, _, h => by simp [firstFree] at h
  | fuel + 1, j0, j, h => by
    unfold firstFree at h
    split at h
    · rename_i hc; simp at h; subst h; exact ⟨Nat.le_refl _, by omega, hc.1, hc.2.1, hc.2.2⟩
    · obtain ⟨a, b, c, d, e⟩ := firstFree_spec E taken n i fuel (j0 + 1) j h
      exact ⟨by omega, by omega, c, d, e⟩

/-- `firstFree` returns `j` if `j` is a free neighbour in range and nothing
before it in range is a free neighbour. -/
theorem firstFree_hit (E : Nat → Nat → Bool) (taken : Nat → Bool) (n i j : Nat)
    (hj : j < n) (he : E i j = true) (ht : taken j = false) :
    ∀ fuel j0, j0 ≤ j → j < j0 + fuel → (∀ j', j0 ≤ j' → j' < j → E i j' = false) →
      firstFree E taken n i fuel j0 = some j
  | 0, _, _, h2, _ => by omega
  | fuel + 1, j0, h1, h2, hno => by
    unfold firstFree
    by_cases hj0 : j0 = j
    · subst hj0; simp [hj, he, ht]
    · have hne : E i j0 = false := hno j0 (Nat.le_refl _) (by omega)
      simp only [hne, Bool.false_eq_true, false_and, and_false, ite_false]
      exact firstFree_hit E taken n i j hj he ht fuel (j0 + 1) (by omega) (by omega)
        (fun j' a b => hno j' (by omega) b)

/-- What `run` takes: a right vertex is taken after `k` steps only if some
`i' < k` probed it and has an edge to it. -/
theorem run_taken (E : Nat → Nat → Bool) (n t : Nat) :
    ∀ k j, (run E n t k).1 j = true → ∃ i', i' < k ∧ i' ≤ j ∧ j < i' + t ∧ E i' j = true
  | 0, _, h => by simp [run] at h
  | k + 1, j, h => by
    simp only [run] at h
    cases hff : firstFree E (run E n t k).1 n k t k with
    | none =>
      rw [hff] at h
      obtain ⟨i', a, b, c, d⟩ := run_taken E n t k j h; exact ⟨i', by omega, b, c, d⟩
    | some j1 =>
      rw [hff] at h
      simp only [Bool.or_eq_true, beq_iff_eq] at h
      rcases h with h | h
      · obtain ⟨i', a, b, c, d⟩ := run_taken E n t k j h; exact ⟨i', by omega, b, c, d⟩
      · subst h
        obtain ⟨a, b, _, d, _⟩ := firstFree_spec E _ n k t k _ hff
        exact ⟨k, by omega, a, by omega, d⟩

theorem run_prefix (E : Nat → Nat → Bool) (n t : Nat) :
    ∀ k k', k ≤ k' → ∀ p ∈ (run E n t k).2, p ∈ (run E n t k').2
  | k, k', h, p, hp => by
    induction k' with
    | zero => have : k = 0 := by omega
              subst this; exact hp
    | succ k' ih =>
      rcases Nat.lt_or_ge k (k' + 1) with hlt | hge
      · have := ih (by omega)
        simp only [run]
        cases firstFree E (run E n t k').1 n k' t k' <;> simp_all
      · have : k = k' + 1 := by omega
        subst this; exact hp

/-- **Good matches are found** (the corrected condition (3)). -/
theorem good_match_found (E : Nat → Nat → Bool) (n t i j : Nat)
    (hi : i < n) (hj : j < n) (hij : i ≤ j) (hwin : j < i + t)
    (h1 : E i j = true)
    (h2 : ∀ j', i ≤ j' → j' < j → E i j' = false)
    (h3 : ∀ i', i' < i → j < i' + t → E i' j = false) :
    (i, j) ∈ splitMesher E n t := by
  have hfree : (run E n t i).1 j = false := by
    cases h : (run E n t i).1 j
    · rfl
    · obtain ⟨i', a, _, c, d⟩ := run_taken E n t i j h
      rw [h3 i' a c] at d; exact absurd d (by simp)
  have hstep : (i, j) ∈ (run E n t (i + 1)).2 := by
    simp only [run]
    rw [firstFree_hit E _ n i j hj h1 hfree t i hij hwin h2]
    simp
  exact run_prefix E n t (i + 1) n hi _ hstep

/-- **The printed condition (3) is not sufficient.**  Two left and two right
vertices, `t = 2`; edges `u_0 v_1` and `u_1 v_1`.  `(u_1, v_1)` satisfies (1),
(2) and the printed (3) (no `i'` with `1 < i' ≤ 1`), but `u_0` probes `v_0`,
then `v_1`, and takes it first. -/
def E2 (i j : Nat) : Bool := j == 1 && i ≤ 1

theorem printed_condition_insufficient :
    E2 1 1 = true ∧ splitMesher E2 2 2 = [(0, 1)] := by decide

/-! ### Step 1: taken vertices in windows -/

/-- Taken sets only grow. -/
theorem run_taken_mono (E : Nat → Nat → Bool) (n t : Nat) :
    ∀ k k', k ≤ k' → ∀ j, (run E n t k).1 j = true → (run E n t k').1 j = true
  | k, k', h, j, hj => by
    induction k' with
    | zero => have : k = 0 := by omega
              subst this; exact hj
    | succ k' ih =>
      rcases Nat.lt_or_ge k (k' + 1) with hlt | hge
      · have := ih (by omega)
        simp only [run]
        cases firstFree E (run E n t k').1 n k' t k' <;> simp_all
      · have : k = k' + 1 := by omega
        subst this; exact hj

/-- `#{i < n : a ≤ i < c} = min n c - min n a`. -/
theorem countP_interval (a c : Nat) (hac : a ≤ c) : ∀ n,
    (List.range n).countP (fun i => a ≤ i && i < c) = min n c - min n a
  | 0 => by simp
  | n + 1 => by
    rw [List.range_succ, List.countP_append, countP_interval a c hac n]
    cases hp : (a ≤ n && n < c)
    · rw [List.countP_singleton, hp]; simp at hp; grind
    · rw [List.countP_singleton, hp]; simp at hp; grind

/-- Each right vertex `j` is in at most `t` of the windows `[i, i + t)`, `i < n`. -/
theorem windows_containing (n t j : Nat) :
    (List.range n).countP (fun i => i ≤ j && j < i + t) ≤ t := by
  have : (List.range n).countP (fun i => i ≤ j && j < i + t) =
      (List.range n).countP (fun i => (j + 1 - t) ≤ i && i < j + 1) := by
    apply List.countP_congr; intro i _; simp; omega
  rw [this, countP_interval _ _ (by omega)]
  omega

private theorem sum_countP_swap (P : Nat → Nat → Bool) :
    ∀ (I J : List Nat), (I.map fun i => J.countP (P i)).sum = (J.map fun j => I.countP (P · j)).sum
  | [], J => by
    simp only [List.map_nil, List.sum_nil, List.countP_nil]
    induction J with
    | nil => rfl
    | cons _ J ih => simp only [List.map_cons, List.sum_cons]; omega
  | i :: I, J => by
    simp only [List.map_cons, List.sum_cons, sum_countP_swap P I J, List.countP_cons]
    induction J with
    | nil => simp
    | cons j J ih =>
      simp only [List.map_cons, List.sum_cons, List.countP_cons]
      cases P i j <;> simp <;> omega

/-- **Step 1.** For any set `J` of taken right vertices,
`Σ_{i<n} |J ∩ [i, i+t)| ≤ t |J|`.  Since taken sets only grow
(`run_taken_mono`), `T_i ≤ |J_final ∩ [i, i+t)|`, so `Σ T_i ≤ t M`. -/
theorem taken_in_windows (n t : Nat) (J : List Nat) :
    ((List.range n).map fun i => J.countP fun j => i ≤ j && j < i + t).sum ≤ t * J.length := by
  rw [sum_countP_swap (fun i j => i ≤ j && j < i + t)]
  induction J with
  | nil => simp
  | cons j J ih =>
    simp only [List.map_cons, List.sum_cons, List.length_cons, Nat.mul_succ]
    have := windows_containing n t j
    omega

/-! ### Step 3: the chord bound, `(1-q)^(t-T) ≤ (1-q)^t + (T/t)(1 - (1-q)^t)`

With `1 - q = d / c` (`d ≤ c`) and both sides scaled by `t c^t`:
`t d^(t-T) c^T ≤ t d^t + T (c^t - d^t)`.  Writing `c = d + a`, both
differences are `a` times sums of the nondecreasing `h s = c^s d^(t-1-s)`, and
the average of the first `T` terms of a nondecreasing sequence is at most the
average of all `t`. -/

def sumTo (h : Nat → Nat) : Nat → Nat
  | 0 => 0
  | n + 1 => sumTo h n + h n

/-- Prefix averages of a nondecreasing sequence: `t Σ_{s<T} h ≤ T Σ_{s<t} h`. -/
theorem prefix_average (h : Nat → Nat) (hmono : ∀ s s', s ≤ s' → h s ≤ h s') :
    ∀ T t, T ≤ t → t * sumTo h T ≤ T * sumTo h t := by
  -- Σ_{s<T} h ≤ T h(T)  and  Σ_{T≤s<t} h ≥ (t - T) h(T)
  have lo : ∀ T, sumTo h T ≤ T * h T := by
    intro T; induction T with
    | zero => simp [sumTo]
    | succ T ih =>
      simp only [sumTo, Nat.succ_mul]
      have := hmono T (T + 1) (by omega)
      have := Nat.mul_le_mul_left T this
      omega
  have hi : ∀ T k, sumTo h (T + k) ≥ sumTo h T + k * h T := by
    intro T k; induction k with
    | zero => simp
    | succ k ih =>
      rw [show T + (k + 1) = (T + k) + 1 by omega]
      simp only [sumTo, Nat.succ_mul]
      have := hmono T (T + k) (by omega)
      omega
  intro T t hT
  obtain ⟨k, rfl⟩ : ∃ k, t = T + k := ⟨t - T, by omega⟩
  have h1 := lo T
  have h2 := hi T k
  -- (T + k) A ≤ T (A + k h(T)) ≤ T Σ_{s<T+k}   using  A ≤ T h(T)
  have : (T + k) * sumTo h T ≤ T * (sumTo h T + k * h T) := by
    rw [Nat.add_mul, Nat.mul_add]
    have : k * sumTo h T ≤ T * (k * h T) := by
      rw [← Nat.mul_assoc, Nat.mul_comm T k, Nat.mul_assoc]
      exact Nat.mul_le_mul_left k h1
    omega
  exact Nat.le_trans this (Nat.mul_le_mul_left T h2)

/-- `Σ_{s<m} c^s d^(n-s) = d Σ_{s<m} c^s d^(n-1-s)` for `m ≤ n`. -/
theorem sum_shift (a d n : Nat) : ∀ m, m ≤ n →
    sumTo (fun s => (d + a) ^ s * d ^ (n - s)) m = d * sumTo (fun s => (d + a) ^ s * d ^ (n - 1 - s)) m
  | 0, _ => by simp [sumTo]
  | m + 1, hm => by
    simp only [sumTo]
    rw [sum_shift a d n m (by omega), show n - m = (n - 1 - m) + 1 by omega, Nat.pow_succ]
    grind

/-- `c^n = d^n + a Σ_{s<n} c^s d^(n-1-s)`, with `c = d + a`. -/
theorem pow_diff (a d : Nat) : ∀ n,
    (d + a) ^ n = d ^ n + a * sumTo (fun s => (d + a) ^ s * d ^ (n - 1 - s)) n
  | 0 => by simp [sumTo]
  | n + 1 => by
    have ih := pow_diff a d n
    have hsplit : sumTo (fun s => (d + a) ^ s * d ^ (n + 1 - 1 - s)) (n + 1) =
        d * sumTo (fun s => (d + a) ^ s * d ^ (n - 1 - s)) n + (d + a) ^ n := by
      simp only [Nat.add_sub_cancel, sumTo, Nat.sub_self, Nat.pow_zero, Nat.mul_one]
      rw [sum_shift a d n n (Nat.le_refl n)]
    rw [hsplit, Nat.pow_succ, Nat.pow_succ, ih]
    grind

/-- **Step 3 (chord bound)**, scaled: `t d^(t-T) c^T ≤ t d^t + T (c^t - d^t)` for
`T ≤ t`, `c = d + a`. -/
theorem chord_bound (a d t T : Nat) (hT : T ≤ t) :
    t * (d ^ (t - T) * (d + a) ^ T) + T * d ^ t ≤ t * d ^ t + T * (d + a) ^ t := by
  let h : Nat → Nat := fun s => (d + a) ^ s * d ^ (t - 1 - s)
  have hmono : ∀ s s', s ≤ s' → s' ≤ t - 1 → h s ≤ h s' := by
    intro s s' hss' hs'
    show (d + a) ^ s * d ^ (t - 1 - s) ≤ (d + a) ^ s' * d ^ (t - 1 - s')
    obtain ⟨k, rfl⟩ : ∃ k, s' = s + k := ⟨s' - s, by omega⟩
    rw [show t - 1 - s = (t - 1 - (s + k)) + k by omega, Nat.pow_add, Nat.pow_add]
    rw [Nat.mul_comm ((d + a) ^ s) ((d + a) ^ k), Nat.mul_assoc, Nat.mul_comm (d ^ _) (d ^ k),
      ← Nat.mul_assoc ((d + a) ^ s), Nat.mul_comm ((d + a) ^ s) (d ^ k), Nat.mul_assoc]
    rw [Nat.mul_comm ((d + a) ^ k) _, Nat.mul_comm (d ^ k) _]
    apply Nat.mul_le_mul_left
    exact Nat.pow_le_pow_left (Nat.le_add_right d a) k
  -- clamp h beyond t - 1 so it is nondecreasing everywhere
  let h' : Nat → Nat := fun s => h (min s (t - 1))
  have hmono' : ∀ s s', s ≤ s' → h' s ≤ h' s' := by
    intro s s' hss'; exact hmono _ _ (by omega) (by omega)
  have hsum : ∀ m, m ≤ t → sumTo h' m = sumTo h m := by
    intro m hm; induction m with
    | zero => rfl
    | succ m ih => simp only [sumTo]; rw [ih (by omega)]; simp [h', show min m (t - 1) = m by omega]
  have havg := prefix_average h' hmono' T t hT
  rw [hsum T hT, hsum t (Nat.le_refl t)] at havg
  -- d^(t-T) c^T = d^t + a Σ_{s<T} h s ;  c^t = d^t + a Σ_{s<t} h s
  have hT' : d ^ (t - T) * (d + a) ^ T = d ^ t + a * sumTo h T := by
    have := pow_diff a d T
    rw [this, Nat.mul_add, ← Nat.pow_add, show t - T + T = t by omega]
    congr 1
    -- d^(t-T) * (a * Σ_{s<T} c^s d^(T-1-s)) = a * Σ_{s<T} c^s d^(t-1-s)
    have : ∀ m, m ≤ T → d ^ (t - T) * sumTo (fun s => (d + a) ^ s * d ^ (T - 1 - s)) m = sumTo h m := by
      intro m hm; induction m with
      | zero => simp [sumTo]
      | succ m ihm =>
        simp only [sumTo]; rw [Nat.mul_add, ihm (by omega)]; congr 1
        show d ^ (t - T) * ((d + a) ^ m * d ^ (T - 1 - m)) = (d + a) ^ m * d ^ (t - 1 - m)
        rw [Nat.mul_comm, Nat.mul_assoc, ← Nat.pow_add, show T - 1 - m + (t - T) = t - 1 - m by omega]
    have h2 := this T (Nat.le_refl T)
    grind
  have ht' : (d + a) ^ t = d ^ t + a * sumTo h t := pow_diff a d t
  rw [hT', ht', Nat.mul_add, Nat.mul_add]
  have : a * (t * sumTo h T) ≤ a * (T * sumTo h t) := Nat.mul_le_mul_left a havg
  grind

/-! ### Step 4: the bound on matches -/

/-- **Step 4.** With `h` left vertices, `M` matched, `U = h - M` unmatched, and
`e = E / C` the probability `(1-q)^t`: if `C U ≤ h E + (C - E) M` (Steps 1–3
summed), then `M (2C - E) ≥ h (C - E)`, that is
`M / h ≥ (1 - e) / (2 - e)`. -/
theorem lemma53_core (h M E C : Nat) (hM : M ≤ h) (hE : E ≤ C)
    (hU : C * (h - M) ≤ h * E + (C - E) * M) :
    h * (C - E) ≤ M * (2 * C - E) := by
  obtain ⟨u, rfl⟩ : ∃ u, h = M + u := ⟨h - M, by omega⟩
  obtain ⟨f, rfl⟩ : ∃ f, C = E + f := ⟨C - E, by omega⟩
  simp only [show M + u - M = u by omega, show E + f - E = f by omega,
    show 2 * (E + f) - E = E + 2 * f by omega] at hU ⊢
  grind

end MeshTheory
