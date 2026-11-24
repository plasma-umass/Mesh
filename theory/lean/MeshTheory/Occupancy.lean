import MeshTheory.Paper
/-!
# The occupancy filter in SplitMesher

Mesh's SplitMesher probes a span with `r` objects only against partners with
at most `b - r` (`shiftedSplitting`, sorted right half).

* `meshes_ones_le`: two meshing spans hold at most `b` objects between them,
  so the filter never skips a pair that could mesh.
* `qu_antitone`: the uniform mesh probability `C(b - r', r) / C(b, r)` falls as
  the partner's occupancy `r'` rises: lower-occupancy partners are the better
  probes.
-/

namespace MeshTheory

theorem meshes_ones_le (s t : List Bool) (hl : s.length = t.length) (h : meshes s t = true) :
    ones s + ones t ≤ s.length := by
  rw [← merge_ones s t h, ← merge_length s t hl]
  exact ones_le_length _

/-- **Filter soundness**: a pair with more than `b` objects between them
never meshes. -/
theorem filter_sound (s t : List Bool) (hl : s.length = t.length) (h : s.length < ones s + ones t) :
    meshes s t = false := by
  cases hm : meshes s t
  · rfl
  · exact absurd (meshes_ones_le s t hl hm) (by omega)

theorem choose_mono_left (k : Nat) : ∀ n, choose n k ≤ choose (n + 1) k := by
  intro n
  cases k with
  | zero => simp
  | succ k => rw [choose_succ_succ n k]; omega

theorem choose_mono (k : Nat) {n m : Nat} (h : n ≤ m) : choose n k ≤ choose m k := by
  induction m with
  | zero => have : n = 0 := by omega
            subst this; exact Nat.le_refl _
  | succ m ih =>
    rcases Nat.lt_or_ge n (m + 1) with hl | hl
    · exact Nat.le_trans (ih (by omega)) (choose_mono_left k m)
    · have : n = m + 1 := by omega
      subst this; exact Nat.le_refl _

/-- **Lower-occupancy partners mesh more often**: `q_u(r, r'+1) ≤ q_u(r, r')`
(same denominator `C(b, r)`). -/
theorem qu_antitone (b r r' : Nat) : quNum b r (r' + 1) ≤ quNum b r r' := by
  unfold quNum
  exact choose_mono r (by omega)

end MeshTheory
