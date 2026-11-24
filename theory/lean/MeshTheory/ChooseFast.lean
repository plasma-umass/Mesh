import MeshTheory.Choose
/-!
# `chooseFast = choose`

The computational checks use the multiplicative formula; this connects them to
the Pascal-rule `choose` that the counting theorems are about.
-/

namespace MeshTheory

theorem choose_eq_zero_of_lt : ∀ {n k : Nat}, n < k → choose n k = 0
  | 0, _ + 1, _ => rfl
  | n + 1, k + 1, h => by
    rw [choose_succ_succ, choose_eq_zero_of_lt (by omega), choose_eq_zero_of_lt (by omega)]
  | _, 0, h => by omega

theorem choose_one : ∀ n : Nat, choose n 1 = n
  | 0 => rfl
  | n + 1 => by rw [choose_succ_succ, choose_zero_right, choose_one n]; omega

/-- `C(n, i) (n - i) = (i + 1) C(n, i + 1)`. -/
theorem choose_mul_sub : ∀ (n i : Nat), choose n i * (n - i) = (i + 1) * choose n (i + 1)
  | 0, i => by cases i <;> simp [choose]
  | n + 1, 0 => by
    simp only [choose_zero_right, Nat.one_mul, Nat.sub_zero, Nat.zero_add]
    exact (choose_one (n + 1)).symm
  | n + 1, i + 1 => by
    -- C(n+1,i+1)(n-i) = [C(n,i) + C(n,i+1)](n-i)
    have h1 := choose_mul_sub n i          -- C(n,i)(n-i) = (i+1) C(n,i+1)
    have h2 := choose_mul_sub n (i + 1)    -- C(n,i+1)(n-i-1) = (i+2) C(n,i+2)
    rw [choose_succ_succ n i, choose_succ_succ n (i + 1)]
    have hs : n + 1 - (i + 1) = n - i := by omega
    rw [hs, Nat.add_mul, h1]
    rcases Nat.lt_or_ge n (i + 1) with hlt | hge
    · rw [choose_eq_zero_of_lt hlt, choose_eq_zero_of_lt (by omega : n < i + 2)]
      simp
    · have : n - i = (n - (i + 1)) + 1 := by omega
      rw [this, Nat.mul_add, Nat.mul_one, h2]
      simp only [Nat.add_mul, Nat.mul_add]
      omega

theorem foldl_step (n : Nat) : ∀ k : Nat,
    (List.range k).foldl (fun acc i => acc * (n - i) / (i + 1)) 1 = choose n k
  | 0 => by simp
  | k + 1 => by
    rw [List.range_succ, List.foldl_append, foldl_step n k]
    simp only [List.foldl_cons, List.foldl_nil]
    rw [choose_mul_sub n k, Nat.mul_div_cancel_left _ (Nat.succ_pos k)]

theorem chooseFast_eq (n k : Nat) : chooseFast n k = choose n k := by
  unfold chooseFast
  split
  · exact (choose_eq_zero_of_lt (by omega)).symm
  · exact foldl_step n k

end MeshTheory
