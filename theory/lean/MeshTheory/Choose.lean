/-!
# Binomial coefficients and strings (Lean core only)

`choose` by Pascal's rule, and spans as bit strings (`List Bool`, one bit per
object slot, `true` = occupied), with the enumeration of all strings of a
given length.
-/

namespace MeshTheory

/-- Binomial coefficient by Pascal's rule. -/
def choose : Nat → Nat → Nat
  | _, 0 => 1
  | 0, _ + 1 => 0
  | n + 1, k + 1 => choose n k + choose n (k + 1)

@[simp] theorem choose_zero_right (n : Nat) : choose n 0 = 1 := by cases n <;> rfl
@[simp] theorem choose_zero_succ (k : Nat) : choose 0 (k + 1) = 0 := rfl
theorem choose_succ_succ (n k : Nat) : choose (n + 1) (k + 1) = choose n k + choose n (k + 1) := rfl

/-- Faster binomial for computation (multiplicative formula). -/
def chooseFast (n k : Nat) : Nat :=
  if k > n then 0 else (List.range k).foldl (fun acc i => acc * (n - i) / (i + 1)) 1

/-- Number of occupied slots. -/
def ones : List Bool → Nat
  | [] => 0
  | true :: s => ones s + 1
  | false :: s => ones s

/-- Two spans mesh iff no slot is occupied in both (Definition 5.1 of the paper). -/
def meshes : List Bool → List Bool → Bool
  | a :: s, b :: t => !(a && b) && meshes s t
  | _, _ => true

/-- All strings of length `n`. -/
def strings : Nat → List (List Bool)
  | 0 => [[]]
  | n + 1 => (strings n).map (false :: ·) ++ (strings n).map (true :: ·)

theorem mem_strings_length : ∀ {n : Nat} {s : List Bool}, s ∈ strings n → s.length = n
  | 0, s, h => by simp [strings] at h; simp [h]
  | n + 1, s, h => by
    simp only [strings, List.mem_append, List.mem_map] at h
    rcases h with ⟨t, ht, rfl⟩ | ⟨t, ht, rfl⟩ <;> simp [mem_strings_length ht]

end MeshTheory
