#!/usr/bin/env python3
"""Synthetic check of colour placement (see theory/lean, docs/colour-placement.md).

Each of n spans (b slots) gets a = random fill in [lo, b] allocations, then
frees down to a target occupancy by a rule: random (oblivious), oldest first
(FIFO / LRU-like: frees each span's own-half objects first under colour
placement, the adversarial case), or newest first (LIFO).  Then SplitMesher
(t = 64) meshes repeatedly until a pass finds nothing.  Reports the fraction of
spans released and how close that is to the ideal (total objects / b spans).

Policies: uniform (Mesh today), colour (own half first), hedged (each span
uniform with probability alpha, else colour).
"""
import argparse
import random


def place(rng, b, policy, alpha, n_alloc):
    """Slots of n_alloc objects, in allocation order."""
    free = list(range(b))
    if policy == "uniform" or (policy == "hedged" and rng.random() < alpha) or b % 2:
        rng.shuffle(free)
        return free[:n_alloc]
    h = b // 2
    own = list(range(h)) if rng.random() < 0.5 else list(range(h, b))
    other = [s for s in range(b) if s not in own]
    rng.shuffle(own)
    rng.shuffle(other)
    return (own + other)[:n_alloc]


def span(rng, b, policy, alpha, fill_lo, target, rule):
    n_alloc = rng.randint(max(fill_lo, target), b)
    live = place(rng, b, policy, alpha, n_alloc)  # allocation order
    k = n_alloc - target
    if rule == "random":
        rng.shuffle(live)
        live = live[k:]
    elif rule == "oldest":
        live = live[k:]
    elif rule == "newest":
        live = live[:target]
    m = 0
    for s in live:
        m |= 1 << s
    return m


def split_mesher(rng, spans, t=64):
    """One SplitMesher pass; returns the surviving spans (meshed pairs OR'd)."""
    rng.shuffle(spans)
    half = len(spans) // 2
    left, right = spans[:half], spans[half:]
    used = [False] * len(right)
    out = []
    meshed = 0
    for j, a in enumerate(left):
        if not right:
            out.append(a)
            continue
        for i in range(min(t, len(right))):
            idx = (j + i) % len(right)
            if not used[idx] and a & right[idx] == 0:
                used[idx] = True
                a |= right[idx]
                meshed += 1
                break
        out.append(a)
    out.extend(r for r, u in zip(right, used) if not u)
    return out, meshed


def run(b, policy, alpha, rule, occ, n, seed, fill_lo_frac):
    rng = random.Random(seed)
    spans = []
    for _ in range(n):
        target = occ(rng)
        spans.append(span(rng, b, policy, alpha, int(fill_lo_frac * b), target, rule))
    objects = sum(bin(m).count("1") for m in spans)
    for _ in range(50):
        spans, meshed = split_mesher(rng, spans)
        if meshed == 0:
            break
    ideal = n - -(-objects // b)
    return (n - len(spans)) / n, (n - len(spans)) / ideal if ideal else 1.0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--n", type=int, default=4000)
    ap.add_argument("--trials", type=int, default=3)
    ap.add_argument("--alpha", type=float, default=0.5)
    a = ap.parse_args()

    occs = {
        "25% full": lambda b: (lambda r: round(0.25 * b)),
        "40% full": lambda b: (lambda r: round(0.40 * b)),
        "50% full": lambda b: (lambda r: b // 2),
        "10-60% full": lambda b: (lambda r: r.randint(max(1, b // 10), round(0.6 * b))),
    }
    print(f"spans released (fraction of the ideal); n={a.n}, {a.trials} trials, alpha={a.alpha}\n")
    print("| b | occupancy | frees | uniform | colour | hedged |")
    print("|---|---|---|---|---|---|")
    for b in (8, 16, 32, 64):
        for oname, ofn in occs.items():
            for rule in ("random", "oldest", "newest"):
                cells = []
                for policy in ("uniform", "colour", "hedged"):
                    rel, frac = zip(*(run(b, policy, a.alpha, rule, ofn(b), a.n, 1000 * s + b, 0.5)
                                      for s in range(a.trials)))
                    cells.append(f"{sum(rel) / len(rel):.2f} ({sum(frac) / len(frac):.0%})")
                print(f"| {b} | {oname} | {rule} | " + " | ".join(cells) + " |", flush=True)


if __name__ == "__main__":
    main()
