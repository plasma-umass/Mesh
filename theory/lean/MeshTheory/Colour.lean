import MeshTheory.Hedged
import MeshTheory.ChooseFast
/-!
# Colour placement: where it dominates uniform placement, and where it doesn't

A colour-placed span with an even number `b = 2h` of slots picks one half
(its colour) uniformly at random, fills free slots of that half first, uniformly
at random, and only then the other half.  Its state is a profile `(x, y)`: `x`
objects in its own half, `y` in the other, each half's set uniform given its
count (`uniform_vs_any`, within a half).  Two independent spans with profiles
`(x1, y1)`, `(x2, y2)` mesh with probability

    q_c = (1/2) [d(x1,x2) d(y1,y2) + d(x1,y2) d(y1,x2)]

(same colour, opposite colours), where `d(a, c) = choose (h - a) c / choose h c`
is the probability that random `a`- and `c`-subsets of an `h`-set are disjoint.
Both terms share the denominator `choose h x2 * choose h y2`.

Results, checked exhaustively (`native_decide`) over every even slot count a
Mesh span has with 4 KiB pages:
* `canonical_dominates`: for spans in their canonical shape (own half filled
  first), `q_c ≥ q_u` for every pair of occupancies.
* `reachable_counterexample`: an oblivious adversary (frees by allocation
  order, never by address) reaches profiles with `q_c = 0 < q_u`;
  `population_counterexample`: a population whose global `q_c` is less than a
  fifth of `q_u`.  Pure colour placement can therefore lose Mesh's guarantee;
  hedged placement (`hedged_pair_bound`) cannot.
Odd slot counts are left to uniform placement: no split into two equal halves
exists, and with a leftover slot the canonical shape loses to uniform placement
(e.g. b = 85, one object each: 83/84 < 84/85).
-/

namespace MeshTheory

/-- Probability that random `a`- and `c`-subsets of an `h`-set are disjoint. -/
def dNum (h a c : Nat) : Nat := chooseFast (h - a) c
def dDen (h c : Nat) : Nat := chooseFast h c

/-- `q_c` for profiles `(x1, y1)`, `(x2, y2)` in a span of `2h` slots, as
numerator / denominator. -/
def qcNum (h x1 y1 x2 y2 : Nat) : Nat := dNum h x1 x2 * dNum h y1 y2 + dNum h x1 y2 * dNum h y1 x2
def qcDen (h x2 y2 : Nat) : Nat := 2 * (dDen h x2 * dDen h y2)

/-- Uniform placement: `q_u = choose (b - r2) r1 / choose b r1`. -/
def quNumF (b r1 r2 : Nat) : Nat := chooseFast (b - r2) r1
def quDenF (b r1 : Nat) : Nat := chooseFast b r1

/-- The canonical profile of a colour-placed span with `r` objects. -/
def canon (h r : Nat) : Nat × Nat := (min r h, r - min r h)

/-- `q_c ≥ q_u`, cross-multiplied. -/
def colourGe (h x1 y1 x2 y2 r1 r2 : Nat) : Bool :=
  quNumF (2 * h) r1 r2 * qcDen h x2 y2 ≤ qcNum h x1 y1 x2 y2 * quDenF (2 * h) r1

def canonicalDominatesAt (h : Nat) : Bool :=
  (List.range (2 * h + 1)).all fun r1 => (List.range (2 * h + 1)).all fun r2 =>
    let (x1, y1) := canon h r1
    let (x2, y2) := canon h r2
    colourGe h x1 y1 x2 y2 r1 r2

/-- Even objects-per-span counts of Mesh's size classes with 4 KiB pages
(`min (max (4096 / size) 8) 256`). -/
def evenSlotCounts : List Nat := [256, 128, 64, 42, 36, 32, 18, 16, 12, 10, 8]

/-- **Canonical dominance**: in the canonical shape, colour placement never
meshes less often than uniform placement, for any occupancies, at every even
slot count Mesh uses. -/
theorem canonical_dominates : (evenSlotCounts.all fun b => canonicalDominatesAt (b / 2)) = true := by
  native_decide

/-- How much better: two half-full 64-slot spans mesh with probability 1/2
under colour placement, against `1 / choose 64 32` (about 5e-19) uniformly. -/
theorem half_full_64 : qcNum 32 32 0 32 0 * 2 = qcDen 32 32 0 ∧ quNumF 64 32 32 = 1 := by
  native_decide

/-! ### An oblivious adversary steers colour placement

The adversary sees only its own allocation order.  Objects are referred to by
allocation order; each records whether it landed in the span's own half. -/

/-- One allocation: own half while it has room, then the other half. -/
def allocObj (h : Nat) (objs : List Bool) : List Bool :=
  let own := objs.count true
  if own < h then objs ++ [true] else if objs.length - own < h then objs ++ [false] else objs

def allocN (h : Nat) : Nat → List Bool → List Bool
  | 0, objs => objs
  | n + 1, objs => allocN h n (allocObj h objs)

/-- Profile `(own, other)`. -/
def profileOf (objs : List Bool) : Nat × Nat := (objs.count true, objs.count false)

/-- Allocate 8 objects into an 8-slot span, free the first 4: profile (0, 4). -/
theorem reach_0_4 : profileOf ((allocN 4 8 []).drop 4) = (0, 4) := by decide
/-- Allocate 5, free the first 3: profile (1, 1). -/
theorem reach_1_1 : profileOf ((allocN 4 5 []).drop 3) = (1, 1) := by decide

/-- **Reachable counterexample**: those two spans never mesh under colour
placement, while uniform placement meshes them with probability 3/14. -/
theorem reachable_counterexample :
    qcNum 4 1 1 0 4 = 0 ∧ quNumF 8 2 4 * 14 = 3 * quDenF 8 2 := by decide

/-- `(1, 8)` in a 16-slot span: allocate 16, free the first 7. -/
theorem reach_1_8 : profileOf ((allocN 8 16 []).drop 7) = (1, 8) := by decide

/-- **Population counterexample** (b = 16): 10% of spans at profile (1, 1)
and 90% at (1, 8).  Over random pairs, colour placement meshes less than a
fifth as often as uniform placement would: `Σ w_i w_j q_c(i,j) < (1/5) Σ w_i
w_j q_u(i,j)` with weights 1 and 9. -/
def popColour : List (Nat × Nat × Nat × Nat) := [(1, 1, 1, 2), (1, 8, 9, 9)]   -- (x, y, weight, r)

def pairSum (f : Nat × Nat × Nat × Nat → Nat × Nat × Nat × Nat → Nat) : Nat :=
  (popColour.map fun i => (popColour.map fun j => f i j).sum).sum

theorem population_counterexample :
    -- scale every term to a common denominator: L = lcm-free product of all denominators
    let L := (popColour.map fun j => qcDen 8 j.1 j.2.1 * quDenF 16 j.2.2.2).foldl (· * ·) 1
    let qc := pairSum fun i j => i.2.2.1 * j.2.2.1 * (qcNum 8 i.1 i.2.1 j.1 j.2.1 * (L / qcDen 8 j.1 j.2.1))
    let qu := pairSum fun i j => i.2.2.1 * j.2.2.1 * (quNumF 16 i.2.2.2 j.2.2.2 * (L / quDenF 16 i.2.2.2))
    5 * qc < qu := by
  native_decide

end MeshTheory
