import MeshTheory.Hedged
import MeshTheory.ChooseFast
/-!
# The paper's §5.1–5.2 (Powers, Tench, Berger, McGregor, PLDI '19)

* Definition 5.1 and Problem 1: strings, meshing a set (pairwise), merging,
  releases.  `merge_ones`: meshing keeps every object at its offset, so the
  merged span holds exactly the objects of both.
* The graph formulation: a meshing is a partition into cliques of the meshing
  graph; releasing `k - 1` strings per clique of `k`.
* The structural facts in the proof of Theorem 5.2: identical non-zero strings
  never mesh (so each `N_i` is a clique of the complement graph), the zero
  string meshes with everything, equal strings have equal neighbourhoods.
* Observation 1: edges aren't 3-wise independent (b = 4, two ones each).
* The triangle probability (§5.2): for uniform strings with occupancies
  `r1, r2, r3`, `P(all three mesh) = C(b-r1, r2)/C(b, r2) * C(b-r1-r2, r3)/C(b, r3)`,
  proved by counting, and the paper's example (b = 32, r = 10, n = 1000:
  under 2 expected triangles, against 167 were edges independent).
-/

namespace MeshTheory

/-! ### Definition 5.1, Problem 1 -/

/-- A set of strings meshes iff every pair meshes. -/
def meshesAll : List (List Bool) → Bool
  | [] => true
  | s :: ss => ss.all (meshes s) && meshesAll ss

/-- The string of the span left after meshing two (objects keep their offsets). -/
def merge : List Bool → List Bool → List Bool
  | a :: s, b :: t => (a || b) :: merge s t
  | s, [] => s
  | [], t => t

theorem merge_length : ∀ s t : List Bool, s.length = t.length → (merge s t).length = s.length
  | [], [], _ => rfl
  | a :: s, b :: t, h => by simp [merge, merge_length s t (by simpa using h)]

/-- Meshing moves every object, at its offset, into one span: the merged span
holds exactly `ones s + ones t` objects. -/
theorem merge_ones : ∀ s t : List Bool, meshes s t = true → ones (merge s t) = ones s + ones t
  | [], [], _ => rfl
  | [], _ :: _, _ => by simp [merge, ones]
  | _ :: _, [], _ => by simp [merge, ones]
  | a :: s, b :: t, h => by
    simp only [meshes, Bool.and_eq_true, Bool.not_eq_true'] at h
    have ih := merge_ones s t h.2
    cases a <;> cases b <;> simp_all [merge, ones] <;> omega

/-- `u` meshes with the merge of `s` and `t` iff it meshes with both. -/
theorem meshes_merge : ∀ u s t : List Bool, u.length = s.length → s.length = t.length →
    meshes u (merge s t) = (meshes u s && meshes u t)
  | [], [], [], _, _ => rfl
  | c :: u, a :: s, b :: t, h1, h2 => by
    simp only [List.length_cons, Nat.add_right_cancel_iff] at h1 h2
    simp only [merge, meshes, meshes_merge u s t h1 h2]
    cases a <;> cases b <;> cases c <;> simp

/-- A meshing: groups of indices into the string list, each group meshing. -/
def validMeshing (S : List (List Bool)) (groups : List (List Nat)) : Bool :=
  groups.all fun g => meshesAll (g.filterMap (S[·]?))

/-- Meshing a group of `k` strings releases `k - 1` of them. -/
def releases (groups : List (List Nat)) : Nat := (groups.map fun g => g.length - 1).sum

/-! ### The graph formulation (§5.1) -/

/-- The meshing graph: an edge iff the strings mesh. -/
def meshGraph (S : List (List Bool)) (i j : Nat) : Bool :=
  match S[i]?, S[j]? with
  | some s, some t => meshes s t
  | _, _ => false

/-- A set of strings meshes iff it is a clique of the meshing graph
(Definition 5.1 restated): `meshesAll` is pairwise. -/
theorem meshesAll_pairwise : ∀ (ss : List (List Bool)),
    meshesAll ss = true ↔ ss.Pairwise (fun s t => meshes s t = true)
  | [] => by simp [meshesAll]
  | s :: ss => by
    simp only [meshesAll, Bool.and_eq_true, List.all_eq_true, List.pairwise_cons, meshesAll_pairwise ss]

/-! ### Facts used in the proof of Theorem 5.2 -/

/-- A non-zero string doesn't mesh with itself: the `N_i` (copies of one
string) are cliques of the complement graph. -/
theorem not_meshes_self : ∀ s : List Bool, 0 < ones s → meshes s s = false
  | [], h => by simp [ones] at h
  | true :: _, _ => by simp [meshes]
  | false :: s, h => by simp only [ones] at h; simp [meshes, not_meshes_self s h]

/-- The zero string meshes with every string. -/
theorem zero_meshes_all : ∀ s t : List Bool, ones s = 0 → meshes s t = true
  | [], _, _ => rfl
  | true :: _, _, h => by simp [ones] at h
  | false :: _, [], _ => rfl
  | false :: s, _ :: t, h => by simp only [ones] at h; simp [meshes, zero_meshes_all s t h]

/-! ### Observation 1: edges are not three-wise independent -/

def strings2of4 : List (List Bool) := (strings 4).filter fun s => ones s == 2

/-- Three length-4 strings with two ones each: if `s1, s2` mesh and `s2, s3`
mesh, then `s1, s3` don't (they equal the complement of `s2`). -/
theorem observation1 :
    (strings2of4.all fun s1 => strings2of4.all fun s2 => strings2of4.all fun s3 =>
      !(meshes s1 s2 && meshes s2 s3) || !meshes s1 s3) = true := by
  decide

/-! ### The triangle probability (§5.2) -/

private theorem sum_const' (L : List (List Bool)) (f : List Bool → Nat) (c : Nat)
    (h : ∀ x ∈ L, f x = c) : (L.map f).sum = L.length * c := by
  induction L with
  | nil => simp
  | cons x L ih =>
    simp only [List.map_cons, List.sum_cons, List.length_cons]
    rw [h x (by simp), ih (fun y hy => h y (by simp [hy]))]
    rw [Nat.succ_mul]; omega

/-- Pairs `(s2, s3)` with `r2`, `r3` ones such that `s1, s2, s3` mesh pairwise
(`s1` fixed), counted as a sum over `s2`. -/
def trianglePairs (s1 : List Bool) (r2 r3 : Nat) : Nat :=
  ((avoiding s1 r2).map fun s2 =>
    ((withOnes s1.length r3).filter fun s3 => meshes s3 s1 && meshes s3 s2).length).sum

theorem trianglePairs_eq (s1 : List Bool) (r2 r3 : Nat) :
    trianglePairs s1 r2 r3 =
      choose (s1.length - ones s1) r2 * choose (s1.length - ones s1 - r2) r3 := by
  unfold trianglePairs
  rw [sum_const' _ _ (choose (s1.length - ones s1 - r2) r3), avoiding_length]
  intro s2 hs2
  unfold avoiding at hs2
  rw [List.mem_filter] at hs2
  obtain ⟨hmem, hp⟩ := hs2
  have hl := mem_strings_length hmem
  simp only [Bool.and_eq_true, beq_iff_eq] at hp
  obtain ⟨ho, hm⟩ := hp
  -- s3 meshes with s1 and s2 iff it meshes with their merge, which has r1 + r2 ones
  have hmerge_len : (merge s1 s2).length = s1.length := merge_length s1 s2 hl.symm
  have hmerge_ones : ones (merge s1 s2) = ones s1 + r2 := by
    rw [merge_ones s1 s2 (by rw [meshes_comm]; exact hm), ho]
  have key := avoiding_length (merge s1 s2) r3
  rw [hmerge_len, hmerge_ones] at key
  rw [show s1.length - ones s1 - r2 = s1.length - (ones s1 + r2) by omega, ← key]
  unfold avoiding withOnes
  rw [hmerge_len, List.filter_filter]
  congr 1
  apply List.filter_congr
  intro s3 h3
  have h3l := mem_strings_length h3
  rw [meshes_merge s3 s1 s2 (by omega) hl.symm]
  cases ones s3 == r3 <;> simp [Bool.and_comm]

/-- The paper's example: b = 32, r1 = r2 = r3 = 10, 1000 strings.  Expected
triangles `C(1000, 3) P(triangle)` are under 2; with independent edges,
`C(1000, 3) q^3` would be about 167 (between 166 and 168). -/
def tri32Num : Nat := chooseFast 22 10 * chooseFast 12 10   -- P(triangle) = this / C(32,10)^2
def tri32Den : Nat := chooseFast 32 10 * chooseFast 32 10
def q32Num : Nat := chooseFast 22 10                         -- q = this / C(32,10)

theorem paper_triangle_example :
    chooseFast 1000 3 * tri32Num < 2 * tri32Den ∧
    166 * (chooseFast 32 10) ^ 3 < chooseFast 1000 3 * q32Num ^ 3 ∧
    chooseFast 1000 3 * q32Num ^ 3 < 168 * (chooseFast 32 10) ^ 3 := by
  native_decide

end MeshTheory
