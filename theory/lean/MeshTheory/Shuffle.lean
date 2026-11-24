import MeshTheory.Colour
/-!
# Mesh's shuffle vector keeps occupancy uniform (small cases)

The paper's analysis assumes a span with `r` objects is a uniformly random
`r`-subset of its slots.  Mesh's default build (`RANDOMIZATION=1`) shuffles a
span's free offsets only when the shuffle vector is filled; a freed offset is
pushed on top and reused first (LIFO, no shuffle on free).  The assumption
still holds against an oblivious adversary, by symmetry: every operation
commutes with relabelling the slots, and the initial order is a uniformly
random permutation.

Here that is checked exhaustively for a 4-slot span: over all 24 initial
orders, after **every** sequence of up to 10 oblivious operations (allocate, or
free the `i`-th live object in allocation order), each occupied set of a given
size is equally likely.  (The full allocator also refills shuffle vectors from
bitmaps and attaches several spans per vector; those steps are
relabelling-invariant too, but are not modelled here.  Nor is the dependence
introduced by Mesh's own meshing decisions, which the paper also leaves out.)
-/

namespace MeshTheory

/-- Shuffle-vector state: free offsets (head = next to allocate) and live
objects' offsets in allocation order. -/
structure SV where
  free : List Nat
  live : List Nat

inductive Op | alloc | freeAt (i : Nat)

def step (s : SV) : Op → SV
  | .alloc => match s.free with
    | [] => s
    | o :: fs => ⟨fs, s.live ++ [o]⟩
  | .freeAt i => match s.live[i]? with
    | none => s
    | some o => ⟨o :: s.free, s.live.eraseIdx i⟩

/-- Occupied set as a bitmask. -/
def mask (l : List Nat) : Nat := l.foldl (fun m o => m ||| (1 <<< o)) 0

/-- All ways to insert `x` into `l`. -/
def insertions (x : Nat) : List Nat → List (List Nat)
  | [] => [[x]]
  | y :: l => (x :: y :: l) :: (insertions x l).map (y :: ·)

def perms : List Nat → List (List Nat)
  | [] => [[]]
  | x :: l => (perms l).flatMap (insertions x)

def popcount (m : Nat) : Nat := (List.range 8).countP fun i => m &&& (1 <<< i) != 0

/-- After running `ops` from every initial order of `b` slots, is every
occupied set of each size equally frequent? -/
def uniformFrom (starts : List (List Nat)) (b : Nat) (ops : List Op) : Bool :=
  let finals := starts.map fun p => mask (ops.foldl step ⟨p, []⟩).live
  (List.range (b + 1)).all fun r =>
    let sets := (List.range (2 ^ b)).filter fun m => popcount m == r
    let counts := sets.map fun m => finals.count m
    match counts with
    | [] => true
    | c :: cs => cs.all (· == c)

def uniformAfter (b : Nat) (ops : List Op) : Bool := uniformFrom (perms (List.range b)) b ops

/-- All oblivious operation sequences of length `n` (frees only of live objects). -/
def opSeqs : Nat → Nat → List (List Op)
  | 0, _ => [[]]
  | n + 1, live =>
    ((opSeqs n (live + 1)).map (Op.alloc :: ·)) ++
    ((List.range live).flatMap fun i => (opSeqs n (live - 1)).map (Op.freeAt i :: ·))

def allSeqsUpTo (n : Nat) : List (List Op) := (List.range (n + 1)).flatMap fun k => opSeqs k 0

theorem perms_count : (perms (List.range 4)).length = 24 := by decide

theorem shuffle_vector_uniform : ((allSeqsUpTo 10).all fun ops => uniformAfter 4 ops) = true := by
  native_decide

/-- The check is not vacuous: without the initial shuffle (every span starts
in slot order) occupancy is far from uniform. -/
theorem unshuffled_not_uniform :
    ((allSeqsUpTo 10).all fun ops => uniformFrom [List.range 4] 4 ops) = false := by
  native_decide


/-- Number of operation sequences checked. -/
theorem seqs_checked : (allSeqsUpTo 10).length = 13232 := by native_decide

end MeshTheory
