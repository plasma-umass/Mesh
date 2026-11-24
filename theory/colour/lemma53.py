# Lemma 5.3 check: random bipartite graph, |L| = |R| = n/2, independent edges w.p. q,
# SplitMesher with t = k/q probes (u_i probes v_i .. v_{i+t-1}, mod |R| as Mesh does,
# or without wrap as in the paper).  Compare matches / (n/2) with the claimed (1 - e^{-2k})/2.
import random, math, sys
def run(n, q, k, wrap, seed):
    rng = random.Random(seed); h = n // 2; t = max(1, round(k / q))
    taken = [False] * h; m = 0
    for i in range(h):
        for d in range(min(t, h)):
            j = i + d
            if j >= h:
                if not wrap: break
                j %= h
            if not taken[j] and rng.random() < q:   # edges sampled lazily: each pair probed at most once
                taken[j] = True; m += 1; break
    return m / h
for q in (0.01, 0.001):
    for k in (0.5, 1, 2, 4):
        n = 200000 if q == 0.001 else 40000
        for wrap in (False, True):
            r = sum(run(n, q, k, wrap, s) for s in range(3)) / 3
            print(f"q={q} k={k} wrap={wrap}: matched fraction of left {r:.3f}; paper's bound (1-e^-2k)/2 = {(1-math.exp(-2*k))/2:.3f}; corrected proof k e^-k = {k*math.exp(-k):.3f}")
