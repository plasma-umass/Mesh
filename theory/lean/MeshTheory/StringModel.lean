import MeshTheory.Star
/-!
# Lemma 5.3 in the string model: where the independent-edge proof breaks

In the paper's model each span is a uniformly random string (`r` ones out of
`b`), and mesh events between different pairs are correlated.  The formal
proof of Lemma 5.3 (`Prob.lemma53_expectation`) assumes independent edges.
Its key step is that, given the history, `u_i` stays unmatched with
probability `(1-q)^(free_i)`.

* **What survives:** a single probe of a fresh span succeeds with probability
  exactly `q` whatever the partner looks like (`uniform_vs_any`), and probes of
  `m` fresh partners all fail with probability `(1-q)^m` (`star_none_mesh`).
* **What fails** (`step_fails`, exhaustive over all 1,296 configurations,
  b = 4, r = 2, two left and two right spans, window 2): the second left span
  is unmatched with probability 20/27, more than the independent-edge value
  `E[(1-q)^(free)]` (q = 1/6).  Right spans that both failed against `u_0` are
  correlated, so `u_1`'s two probes are correlated too.
* **The bound itself still holds there** (`bound_holds_small`):
  `E[M]/h ≥ (1-e)/(2-e)`.

So the string-model version of Lemma 5.3 needs a different argument from the
independent-edge one.  The paper's acyclic-independence argument covers the
good-match events only under the printed (insufficient) condition (3).
-/

namespace MeshTheory

/-- Length-4 strings with two ones. -/
def S42 : List (List Bool) := withOnes 4 2

/-- SplitMesher on two left and two right spans, window 2 (wrap-around):
returns (u_0 matched, u_1 matched, free slots in u_1's window). -/
def run2 (u0 u1 v0 v1 : List Bool) : Bool × Bool × Nat :=
  -- u_0 probes v_0 then v_1
  let t0 := if meshes u0 v0 then some 0 else if meshes u0 v1 then some 1 else none
  -- u_1 probes v_1 then v_0, skipping the one u_0 took
  let free1 := match t0 with | some _ => 1 | none => 2
  let m1 := match t0 with
    | some 0 => meshes u1 v1
    | some _ => meshes u1 v0
    | none => meshes u1 v1 || meshes u1 v0
  (t0.isSome, m1, free1)

def configs : List (List Bool × List Bool × List Bool × List Bool) :=
  S42.flatMap fun a => S42.flatMap fun b => S42.flatMap fun c => S42.map fun d => (a, b, c, d)

/-- Configurations where u_1 is unmatched. -/
def unmatched1 : Nat := (configs.filter fun x => !(run2 x.1 x.2.1 x.2.2.1 x.2.2.2).2.1).length
/-- `Σ 36 (5/6)^(free_1) = Σ 5^f 6^(2-f)`: the independent-edge prediction, scaled by 36. -/
def predicted36 : Nat := (configs.map fun x =>
  let f := (run2 x.1 x.2.1 x.2.2.1 x.2.2.2).2.2
  5 ^ f * 6 ^ (2 - f)).sum
/-- Total matches over all configurations. -/
def totalM : Nat := (configs.map fun x =>
  let r := run2 x.1 x.2.1 x.2.2.1 x.2.2.2
  (if r.1 then 1 else 0) + (if r.2.1 then 1 else 0)).sum

theorem q_is_one_sixth : choose (4 - 2) 2 * 6 = choose 4 2 := by decide

/-- **The independent-edge step fails**: `P(u_1 unmatched) = 20/27`, larger than
`E[(1-q)^(free_1)]`. -/
theorem step_fails :
    configs.length = 1296 ∧ unmatched1 * 27 = 20 * 1296 ∧ 36 * unmatched1 > predicted36 := by
  native_decide

/-- **The bound still holds**: `E[M]/h ≥ (1-e)/(2-e)` with `e = (5/6)^2 = 25/36`,
i.e. `E[M]/2 ≥ 11/47`. -/
theorem bound_holds_small : 47 * totalM ≥ 11 * (2 * 1296) := by native_decide

end MeshTheory
