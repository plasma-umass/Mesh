#!/usr/bin/env python3
"""Plot bench/kv/fragmentation.py results: memory over time, and its distribution.

Writes one self-contained HTML page (inline SVG, no dependencies) with, for
each server and eviction state:

  - memory over time: the median across trials of each configuration, from the
    start of insertion (as in the Mesh paper's Figure 7);
  - the distribution of memory over the run: for each configuration, the
    fraction of time memory was at or below each level (a CDF over time, so
    averages can be compared at the distribution level, not just as means);
  - a table of the time-weighted mean, median, p90, peak and final memory.

Usage: plot.py RESULTS.json [MORE.json ...] -o memory.html [--title TEXT]
               [--markdown summary.md --image-url URL_WITH_{panel}]

The page shows one panel when opened as memory.html#panel=N (used to render
per-panel PNGs).  --markdown writes the same tables as GitHub markdown, with
one image per panel (--image-url, "{panel}" replaced by its index), for PR
comments.
"""

import argparse
import html
import os
import json
import statistics
import sys

# fixed per configuration, so a configuration keeps its color in every chart
# (categorical slots 1-5 of the reference palette)
SLOTS = {
    "mesh": 1,
    "jemalloc+activedefrag": 2,
    "jemalloc": 3,
    "glibc": 4,
    "mesh-nomesh": 5,
}
LABELS = {
    "mesh": "Mesh",
    "jemalloc+activedefrag": "jemalloc + activedefrag",
    "jemalloc": "jemalloc",
    "glibc": "glibc",
    "mesh-nomesh": "Mesh, meshing off",
}
GRID_S = 0.1
PAPER_WINDOW_S = 7.5
LAT_BUCKET_S = 0.5  # latency over time: the slowest request in each bucket
CDF_POINTS = 400    # latency CDF: quantiles kept per configuration


def resample(t, v, end):
    """Sample-and-hold v(t) onto a GRID_S grid over [0, end]."""
    out, j = [], 0
    n = int(end / GRID_S) + 1
    for i in range(n):
        x = i * GRID_S
        while j + 1 < len(t) and t[j + 1] <= x:
            j += 1
        out.append(v[j])
    return out


def percentile(sorted_vals, p):
    if not sorted_vals:
        return 0.0
    return sorted_vals[min(len(sorted_vals) - 1, int(round(p / 100 * (len(sorted_vals) - 1))))]


def build_panels(results):
    groups = {}
    for r in results:
        if r.get("state") is None or not r.get("samples") or not r["samples"].get("t_s"):
            continue
        groups.setdefault((r["server"], r["state"]), {}).setdefault(r["config"], []).append(r)

    def state_keys(k):
        return (k[0], float(k[1].split("k")[0]) if k[1][0].isdigit() else 0)

    panels = []
    for (server, state) in sorted(groups, key=state_keys):
        by_config = groups[(server, state)]
        series, rows = [], []
        for config in sorted(by_config, key=lambda c: SLOTS.get(c, 99)):
            rs = by_config[config]
            # common window: from insertion start to the shortest trial's end
            end = min(r["samples"]["t_s"][-1] for r in rs)
            grids = [resample(r["samples"]["t_s"], [k / 1024 for k in r["samples"]["mem_kib"]], end) for r in rs]
            median = [statistics.median(g[i] for g in grids) for i in range(len(grids[0]))]
            pooled = sorted(x for g in grids for x in g)  # each grid point = GRID_S of time
            paper_n = int(PAPER_WINDOW_S / GRID_S) + 1
            in_paper_window = [x for g in grids for x in g[:paper_n]]
            series.append({
                "key": config,
                "name": LABELS.get(config, config),
                "slot": SLOTS.get(config, 6),
                "y": [round(x, 1) for x in median],
                "cdf": [round(x, 1) for x in pooled],
            })
            # latency: per trial, the slowest probe request in each LAT_BUCKET_S
            probes = [r.get("probe") or {"t_s": [], "ms": []} for r in rs]
            lat_end = min((pr["t_s"][-1] for pr in probes if pr["t_s"]), default=0)
            nb = int(lat_end / LAT_BUCKET_S)
            per_trial = []
            for pr in probes:
                worst = [0.0] * nb
                for t, ms in zip(pr["t_s"], pr["ms"]):
                    b = int(t / LAT_BUCKET_S)
                    if b < nb:
                        worst[b] = max(worst[b], ms)
                per_trial.append(worst)
            lat_ms = sorted(ms for pr in probes for ms in pr["ms"])
            series[-1]["latT"] = [round((i + 0.5) * LAT_BUCKET_S, 2) for i in range(nb)]
            series[-1]["latWorst"] = [round(statistics.median(w[i] for w in per_trial), 3) for i in range(nb)] if nb else []
            q = [percentile(lat_ms, 100 * i / (CDF_POINTS - 1)) for i in range(CDF_POINTS)] if lat_ms else []
            series[-1]["latCdf"] = [round(max(x, 0.001), 4) for x in q]
            rows.append({
                "name": LABELS.get(config, config),
                "slot": SLOTS.get(config, 6),
                "trials": len(rs),
                "mean": statistics.mean(pooled),
                "mean_paper": statistics.mean(in_paper_window),
                "p50": percentile(pooled, 50),
                "p90": percentile(pooled, 90),
                "peak": statistics.median(max(g) for g in grids),
                "final": statistics.median(g[-1] for g in grids),
                "insert": statistics.median(r["insert_s"] for r in rs),
                "idle_cpu": statistics.median(r.get("idle_cpu_s") or 0 for r in rs),
                "lat_p50": percentile(lat_ms, 50),
                "lat_p99": percentile(lat_ms, 99),
                "lat_p999": percentile(lat_ms, 99.9),
                "lat_max": lat_ms[-1] if lat_ms else 0.0,
                "pass_max": statistics.median((r.get("mesh_pass_max_ms") or 0) for r in rs),
                "requests": len(lat_ms),
            })
        insert_end = statistics.median(r["insert"] for r in rows) if rows else 0
        panels.append({
            "server": server,
            "state": state,
            "step": GRID_S,
            "insertEnd": round(insert_end, 2),
            "series": series,
            "rows": rows,
        })
    return panels


PAGE = r"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>__TITLE__</title>
<style>
.viz-root {
  color-scheme: light;
  --surface-1: #fcfcfb; --page: #f9f9f7;
  --text-primary: #0b0b0b; --text-secondary: #52514e; --text-muted: #898781;
  --grid: #e1e0d9; --axis: #c3c2b7; --border: rgba(11,11,11,0.10);
  --series-1: #2a78d6; --series-2: #eb6834; --series-3: #1baf7a; --series-4: #eda100; --series-5: #e87ba4;
  --series-6: #008300;
}
@media (prefers-color-scheme: dark) {
  :root:where(:not([data-theme="light"])) .viz-root {
    color-scheme: dark;
    --surface-1: #1a1a19; --page: #0d0d0d;
    --text-primary: #f0efec; --text-secondary: #c3c2b7; --text-muted: #898781;
    --grid: #2c2c2a; --axis: #383835; --border: rgba(255,255,255,0.10);
    --series-1: #3987e5; --series-2: #d95926; --series-3: #199e70; --series-4: #c98500; --series-5: #d55181;
    --series-6: #0ca30c;
  }
}
:root[data-theme="dark"] .viz-root {
  color-scheme: dark;
  --surface-1: #1a1a19; --page: #0d0d0d;
  --text-primary: #f0efec; --text-secondary: #c3c2b7; --text-muted: #898781;
  --grid: #2c2c2a; --axis: #383835; --border: rgba(255,255,255,0.10);
  --series-1: #3987e5; --series-2: #d95926; --series-3: #199e70; --series-4: #c98500; --series-5: #d55181;
  --series-6: #0ca30c;
}
body { margin: 0; background: var(--page); }
.viz-root { font: 14px/1.4 system-ui, -apple-system, "Segoe UI", sans-serif; color: var(--text-primary);
  background: var(--page); padding: 24px; max-width: 1240px; margin: 0 auto; }
h1 { font-size: 20px; margin: 0 0 4px; }
.sub { color: var(--text-secondary); margin: 0 0 24px; }
section { background: var(--surface-1); border: 1px solid var(--border); border-radius: 8px; padding: 16px 20px;
  margin-bottom: 24px; }
h2 { font-size: 16px; margin: 0 0 4px; }
.legend { display: flex; flex-wrap: wrap; gap: 16px; margin: 8px 0 4px; color: var(--text-secondary);
  font-size: 13px; }
.legend span { display: inline-flex; align-items: center; gap: 6px; }
.key { width: 16px; height: 2px; border-radius: 1px; display: inline-block; }
.charts { display: grid; grid-template-columns: 1fr 1fr; gap: 16px; }
@media (max-width: 900px) { .charts { grid-template-columns: 1fr; } }
.chart h3 { font-size: 13px; font-weight: 600; margin: 8px 0 0; color: var(--text-secondary); }
.chart { position: relative; }
svg { display: block; width: 100%; height: auto; overflow: visible; }
svg text { fill: var(--text-muted); font-size: 11px; font-variant-numeric: tabular-nums; }
svg .label { fill: var(--text-secondary); font-size: 11px; }
svg .guide { paint-order: stroke; stroke: var(--surface-1); stroke-width: 3px; stroke-linejoin: round; }
.tip { position: absolute; pointer-events: none; background: var(--surface-1); border: 1px solid var(--border);
  border-radius: 6px; padding: 6px 8px; font-size: 12px; box-shadow: 0 2px 8px rgba(0,0,0,0.12);
  display: none; white-space: nowrap; z-index: 2; }
.tip .hd { color: var(--text-muted); margin-bottom: 2px; }
.tip .row { display: flex; align-items: center; gap: 6px; }
.tip .row b { font-variant-numeric: tabular-nums; color: var(--text-primary); min-width: 64px; text-align: right; }
.tip .row span { color: var(--text-secondary); }
table { border-collapse: collapse; margin-top: 12px; font-size: 13px; width: 100%; }
th, td { padding: 4px 10px; text-align: right; border-bottom: 1px solid var(--grid);
  font-variant-numeric: tabular-nums; }
th { color: var(--text-muted); font-weight: 500; }
th:first-child, td:first-child { text-align: left; }
td .key { margin-right: 6px; vertical-align: middle; }
.note { color: var(--text-muted); font-size: 12px; margin-top: 8px; }
</style>
</head>
<body>
<div class="viz-root" id="root">
<h1>__TITLE__</h1>
<p class="sub">__SUBTITLE__</p>
</div>
<script>
const PANELS = __DATA__;
const PAPER_WINDOW = __PAPER__;
const LAT_BUCKET = __LATB__;
const W = 560, H = 300, M = {l: 48, r: 150, t: 12, b: 36};
const NS = "http://www.w3.org/2000/svg";
const color = s => `var(--series-${s})`;
function el(tag, attrs, parent) {
  const e = document.createElementNS(NS, tag);
  for (const k in attrs) e.setAttribute(k, attrs[k]);
  if (parent) parent.appendChild(e);
  return e;
}
function div(cls, parent, text) {
  const d = document.createElement("div");
  if (cls) d.className = cls;
  if (text !== undefined) d.textContent = text;
  if (parent) parent.appendChild(d);
  return d;
}
function niceTicks(lo, hi, n) {
  const span = hi - lo, raw = span / n, mag = Math.pow(10, Math.floor(Math.log10(raw)));
  const step = [1, 2, 2.5, 5, 10].map(m => m * mag).find(s => span / s <= n) || 10 * mag;
  const out = [];
  for (let v = Math.ceil(lo / step) * step; v <= hi + 1e-9; v += step) out.push(+v.toFixed(6));
  return out;
}
// Generic line chart: series {name, slot, xs, ys}; valueAt(series, x) -> y for the crosshair.
function lineChart(parent, o) {
  const box = div("chart", parent);
  const h3 = document.createElement("h3"); h3.textContent = o.title; box.appendChild(h3);
  const svg = el("svg", {viewBox: `0 0 ${W} ${H}`, role: "img", "aria-label": o.title}, box);
  const pw = W - M.l - M.r, ph = H - M.t - M.b;
  const x = v => M.l + (v - o.x0) / (o.x1 - o.x0) * pw;
  const y = v => M.t + ph - (v - o.y0) / (o.y1 - o.y0) * ph;
  for (const t of niceTicks(o.y0, o.y1, 5)) {
    el("line", {x1: M.l, x2: M.l + pw, y1: y(t), y2: y(t), stroke: "var(--grid)", "stroke-width": 1}, svg);
    el("text", {x: M.l - 6, y: y(t) + 4, "text-anchor": "end"}, svg).textContent = o.yFmt(t);
  }
  for (const t of (o.xTicks || niceTicks(o.x0, o.x1, 6))) {
    el("text", {x: x(t), y: M.t + ph + 16, "text-anchor": "middle"}, svg).textContent = o.xFmt(t);
  }
  el("line", {x1: M.l, x2: M.l + pw, y1: M.t + ph, y2: M.t + ph, stroke: "var(--axis)", "stroke-width": 1}, svg);
  el("text", {x: M.l + pw / 2, y: H - 4, "text-anchor": "middle"}, svg).textContent = o.xLabel;
  el("text", {x: 12, y: M.t + ph / 2, "text-anchor": "middle", transform: `rotate(-90 12 ${M.t + ph / 2})`},
     svg).textContent = o.yLabel;
  (o.guides || []).forEach((g, i) => {
    if (g.x <= o.x0 || g.x >= o.x1) return;
    el("line", {x1: x(g.x), x2: x(g.x), y1: M.t, y2: M.t + ph, stroke: "var(--axis)", "stroke-width": 1}, svg);
    // staggered so neighboring guides' labels never overlap
    // at the bottom, where early in the run every line is still high
    el("text", {x: x(g.x) + 4, y: o.guidesTop ? M.t + 10 + 13 * i : M.t + ph - 6 - 13 * i, "text-anchor": "start",
                class: "guide"}, svg)
        .textContent = g.label;
  });
  // lines
  for (const s of o.series) {
    let d = "";
    for (let i = 0; i < s.xs.length; i++) d += (i ? "L" : "M") + x(s.xs[i]).toFixed(1) + "," + y(s.ys[i]).toFixed(1);
    el("path", {d, fill: "none", stroke: color(s.slot), "stroke-width": 2, "stroke-linejoin": "round",
                "stroke-linecap": "round"}, svg);
  }
  // direct labels at line ends, pushed apart so they don't collide
  const ends = o.labelAt ? o.series.map(s => ({s, yy: y(o.labelAt(s))})).sort((a, b) => a.yy - b.yy) : [];
  for (let i = 1; i < ends.length; i++) if (ends[i].yy - ends[i - 1].yy < 13) ends[i].yy = ends[i - 1].yy + 13;
  for (const e of ends) {
    el("line", {x1: M.l + pw + 4, x2: M.l + pw + 14, y1: e.yy, y2: e.yy, stroke: color(e.s.slot),
                "stroke-width": 2, "stroke-linecap": "round"}, svg);
    el("text", {x: M.l + pw + 18, y: e.yy + 4, class: "label"}, svg).textContent = e.s.name;
  }
  // crosshair + one tooltip listing every series
  const cross = el("line", {y1: M.t, y2: M.t + ph, stroke: "var(--text-muted)", "stroke-width": 1,
                            visibility: "hidden"}, svg);
  const dots = o.series.map(s => el("circle", {r: 4, fill: color(s.slot), stroke: "var(--surface-1)",
                                                 "stroke-width": 2, visibility: "hidden"}, svg));
  const hit = el("rect", {x: M.l, y: M.t, width: pw, height: ph, fill: "transparent", tabindex: 0}, svg);
  const tip = div("tip", box);
  function show(px) {
    const xv = Math.max(o.x0, Math.min(o.x1, o.snap(o.x0 + (px - M.l) / pw * (o.x1 - o.x0))));
    cross.setAttribute("x1", x(xv)); cross.setAttribute("x2", x(xv)); cross.setAttribute("visibility", "visible");
    tip.replaceChildren();
    div("hd", tip, o.xFmtLong(xv));
    const vals = o.series.map(s => ({s, v: o.valueAt(s, xv)})).sort((a, b) => b.v - a.v);
    o.series.forEach((s, i) => {
      const v = o.valueAt(s, xv);
      dots[i].setAttribute("cx", x(xv)); dots[i].setAttribute("cy", y(v)); dots[i].setAttribute("visibility", "visible");
    });
    for (const {s, v} of vals) {
      const row = div("row", tip);
      const k = document.createElement("i"); k.className = "key"; k.style.background = color(s.slot); row.appendChild(k);
      const b = document.createElement("b"); b.textContent = o.yFmtLong(v); row.appendChild(b);
      const n = document.createElement("span"); n.textContent = s.name; row.appendChild(n);
    }
    tip.style.display = "block";
    const r = svg.getBoundingClientRect(), scale = r.width / W;
    let left = x(xv) * scale + 12;
    if (left + tip.offsetWidth > r.width) left = x(xv) * scale - tip.offsetWidth - 12;
    tip.style.left = left + "px"; tip.style.top = (M.t * scale + 24) + "px";
  }
  function hide() { tip.style.display = "none"; cross.setAttribute("visibility", "hidden");
                    dots.forEach(d => d.setAttribute("visibility", "hidden")); }
  hit.addEventListener("pointermove", ev => {
    const r = svg.getBoundingClientRect(); show((ev.clientX - r.left) / r.width * W);
  });
  hit.addEventListener("pointerleave", hide);
  let kx = null;
  hit.addEventListener("focus", () => { kx = M.l + pw / 2; show(kx); });
  hit.addEventListener("blur", hide);
  hit.addEventListener("keydown", ev => {
    if (ev.key !== "ArrowLeft" && ev.key !== "ArrowRight") return;
    kx = Math.max(M.l, Math.min(M.l + pw, (kx ?? M.l) + (ev.key === "ArrowRight" ? 1 : -1) * pw / 50));
    show(kx); ev.preventDefault();
  });
}
const mib = v => v.toFixed(0), mibLong = v => v.toFixed(1) + " MiB";
const root = document.getElementById("root");
const only = /panel=(\d+)/.exec(location.hash);
PANELS.forEach((p, i) => { if (!only || +only[1] === i) renderPanel(p); });
function renderPanel(p) {
  const sec = document.createElement("section"); root.appendChild(sec);
  const h2 = document.createElement("h2"); h2.textContent = `${p.server} — ${p.state} left after eviction`; sec.appendChild(h2);
  const lg = div("legend", sec);
  for (const s of p.series) {
    const sp = document.createElement("span");
    const k = document.createElement("i"); k.className = "key"; k.style.background = color(s.slot); sp.appendChild(k);
    sp.appendChild(document.createTextNode(s.name)); lg.appendChild(sp);
  }
  const charts = div("charts", sec);
  const allY = p.series.flatMap(s => s.y);
  const y0 = Math.max(0, Math.floor(Math.min(...allY) / 50) * 50), y1 = Math.ceil(Math.max(...allY) / 50) * 50;
  const xMax = Math.max(...p.series.map(s => (s.y.length - 1) * p.step));
  // 1. memory over time
  lineChart(charts, {
    title: "Memory over time (median of trials)",
    series: p.series.map(s => ({...s, xs: s.y.map((_, i) => i * p.step), ys: s.y})),
    x0: 0, x1: xMax, y0, y1, xLabel: "seconds since insertion started", yLabel: "MiB",
    xFmt: v => v + "s", xFmtLong: v => v.toFixed(1) + " s", yFmt: mib, yFmtLong: mibLong,
    guides: [{x: p.insertEnd, label: "insertion done"}, {x: PAPER_WINDOW, label: "paper's 7.5 s"}],
    snap: v => Math.round(v / p.step) * p.step,
    valueAt: (s, xv) => s.ys[Math.min(s.ys.length - 1, Math.max(0, Math.round(xv / p.step)))],
    labelAt: s => s.ys[s.ys.length - 1],
  });
  // 2. distribution of memory over time (CDF)
  const cdfSeries = p.series.map(s => {
    const n = s.cdf.length, xs = [], ys = [];
    for (let i = 0; i < n; i++) { xs.push(s.cdf[i]); ys.push((i + 1) / n * 100); }
    return {...s, xs, ys, sorted: s.cdf};
  });
  const frac = (s, xv) => { let lo = 0, hi = s.sorted.length;
    while (lo < hi) { const m = (lo + hi) >> 1; if (s.sorted[m] <= xv) lo = m + 1; else hi = m; }
    return lo / s.sorted.length * 100; };
  lineChart(charts, {
    title: "Share of time at or below each memory level",
    series: cdfSeries, x0: y0, x1: y1, y0: 0, y1: 100,
    xLabel: "MiB", yLabel: "% of time", xFmt: mib, xFmtLong: v => "≤ " + v.toFixed(0) + " MiB",
    yFmt: v => v + "%", yFmtLong: v => v.toFixed(0) + "% of time",
    snap: v => Math.round(v), valueAt: frac,  // no end labels: the legend and the table carry identity
  });
  // 3. table
  const tbl = document.createElement("table"); sec.appendChild(tbl);
  const hr = tbl.insertRow();
  for (const h of ["configuration", "trials", `mean, first ${PAPER_WINDOW} s`, "mean", "median", "p90", "peak",
                   "final"]) {
    const th = document.createElement("th"); th.textContent = h; hr.appendChild(th);
  }
  for (const r of p.rows) {
    const tr = tbl.insertRow();
    const c0 = tr.insertCell();
    const k = document.createElement("i"); k.className = "key"; k.style.background = color(r.slot); c0.appendChild(k);
    c0.appendChild(document.createTextNode(r.name));
    tr.insertCell().textContent = r.trials;
    for (const f of ["mean_paper", "mean", "p50", "p90", "peak", "final"]) tr.insertCell().textContent = r[f].toFixed(1);
  }
  // 4. latency
  const h4 = document.createElement("h3"); h4.textContent = "Latency (GET every 10 ms during the idle window)";
  h4.style.margin = "20px 0 0"; sec.appendChild(h4);
  const lcharts = div("charts", sec);
  const allW = p.series.flatMap(s => s.latWorst);
  const wMax = Math.max(1, Math.ceil(Math.max(...allW) / 5) * 5);
  lineChart(lcharts, {
    title: `Slowest request in each ${LAT_BUCKET} s (median of trials)`,
    series: p.series.map(s => ({...s, xs: s.latT, ys: s.latWorst})),
    x0: 0, x1: xMax, y0: 0, y1: wMax, xLabel: "seconds since insertion started", yLabel: "ms",
    xFmt: v => v + "s", xFmtLong: v => "at " + v.toFixed(1) + " s", yFmt: v => String(v),
    yFmtLong: v => v.toFixed(2) + " ms", guides: [{x: PAPER_WINDOW, label: "paper's 7.5 s"}], guidesTop: true,
    snap: v => (Math.round(v / LAT_BUCKET - 0.5) + 0.5) * LAT_BUCKET,
    valueAt: (s, xv) => s.ys[Math.min(s.ys.length - 1, Math.max(0, Math.round(xv / LAT_BUCKET - 0.5)))] ?? 0,
    // no end labels: every line ends near zero; the legend carries identity
  });
  // CDF on a log axis: x holds log10(ms)
  const lx0 = -1, lx1 = Math.max(1, Math.ceil(Math.log10(Math.max(...p.series.map(s => s.latCdf[s.latCdf.length - 1] || 1)))));
  const latCdf = p.series.map(s => {
    const n = s.latCdf.length;
    return {...s, xs: s.latCdf.map(v => Math.max(lx0, Math.log10(v))), ys: s.latCdf.map((_, i) => i / (n - 1) * 100),
            sortedLog: s.latCdf.map(v => Math.log10(v))};
  });
  const fracLog = (s, xv) => { let lo = 0, hi = s.sortedLog.length;
    while (lo < hi) { const m = (lo + hi) >> 1; if (s.sortedLog[m] <= xv) lo = m + 1; else hi = m; }
    return lo / s.sortedLog.length * 100; };
  const ticks = []; for (let e = lx0; e <= lx1; e++) ticks.push(e);
  lineChart(lcharts, {
    title: "Share of requests at or below each latency",
    series: latCdf, x0: lx0, x1: lx1, y0: 0, y1: 100, xTicks: ticks,
    xLabel: "ms (log scale)", yLabel: "% of requests",
    xFmt: v => String(+Math.pow(10, v).toPrecision(1)), xFmtLong: v => "≤ " + (+Math.pow(10, v).toPrecision(2)) + " ms",
    yFmt: v => v + "%", yFmtLong: v => v.toFixed(1) + "% of requests",
    snap: v => Math.round(v * 20) / 20, valueAt: fracLog,
  });
  const lt = document.createElement("table"); sec.appendChild(lt);
  const lh = lt.insertRow();
  for (const h of ["configuration", "requests", "insert (s)", "idle CPU (s)", "p50 (ms)", "p99 (ms)", "p99.9 (ms)",
                   "max (ms)", "longest mesh pass (ms)"]) {
    const th = document.createElement("th"); th.textContent = h; lh.appendChild(th);
  }
  for (const r of p.rows) {
    const tr = lt.insertRow();
    const c0 = tr.insertCell();
    const k = document.createElement("i"); k.className = "key"; k.style.background = color(r.slot); c0.appendChild(k);
    c0.appendChild(document.createTextNode(r.name));
    tr.insertCell().textContent = r.requests;
    tr.insertCell().textContent = r.insert.toFixed(2);
    tr.insertCell().textContent = r.idle_cpu.toFixed(2);
    for (const f of ["lat_p50", "lat_p99", "lat_p999", "lat_max"]) tr.insertCell().textContent = r[f].toFixed(2);
    tr.insertCell().textContent = r.pass_max ? r.pass_max.toFixed(1) : "—";
  }
  div("note", sec, "MiB of cgroup memory (mstat). Means, median and p90 are over time from the start of " +
      `insertion, pooled across trials ("first ${PAPER_WINDOW} s" is the paper's measurement window); ` +
      "peak and final are medians across trials.");
}
</script>
</body>
</html>
"""


def markdown(panels, notes, image_url):
    """GitHub-markdown version of the tables, one image per panel."""
    out = [f"_{'; '.join(notes)}. Memory: MiB of cgroup memory (mstat); latency: a GET every 10 ms during "
           f"the idle window._", ""]
    for i, p in enumerate(panels):
        out.append(f"#### {p['server']} — {p['state']} left after eviction")
        if image_url:
            out.append(f"![{p['server']} {p['state']}: memory and latency over time]({image_url.format(panel=i)})")
        out.append("")
        out.append(f"| configuration | trials | mean, first {PAPER_WINDOW_S:g} s | mean | median | p90 | peak | final "
                   f"| latency p99 (ms) | p99.9 (ms) | max (ms) | idle CPU (s) | longest mesh pass (ms) |")
        out.append("|---|---|---|---|---|---|---|---|---|---|---|---|---|")
        for r in p["rows"]:
            out.append(f"| {r['name']} | {r['trials']} | {r['mean_paper']:.1f} | {r['mean']:.1f} | {r['p50']:.1f} | "
                       f"{r['p90']:.1f} | {r['peak']:.1f} | {r['final']:.1f} | {r['lat_p99']:.2f} | "
                       f"{r['lat_p999']:.2f} | {r['lat_max']:.1f} | {r['idle_cpu']:.2f} | "
                       f"{r['pass_max']:.1f} |" if r["pass_max"] else
                       f"| {r['name']} | {r['trials']} | {r['mean_paper']:.1f} | {r['mean']:.1f} | {r['p50']:.1f} | "
                       f"{r['p90']:.1f} | {r['peak']:.1f} | {r['final']:.1f} | {r['lat_p99']:.2f} | "
                       f"{r['lat_p999']:.2f} | {r['lat_max']:.1f} | {r['idle_cpu']:.2f} | — |")
        out.append("")
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("json", nargs="+", help="result files from fragmentation.py --json")
    ap.add_argument("-o", "--output", default="memory.html")
    ap.add_argument("--title", default="Redis / Valkey memory over time")
    ap.add_argument("--markdown", help="also write the tables as GitHub markdown here")
    ap.add_argument("--image-url", help="with --markdown: per-panel image URL, '{panel}' = panel index")
    a = ap.parse_args()
    results, notes = [], []
    for path in a.json:
        with open(path) as f:
            d = json.load(f)
        results.extend(d["results"])
        notes.append(f"{d.get('trials', '?')} trials, {d.get('settle_s', '?'):g} s idle window "
                     f"({os.path.basename(path)})")
    if any("state" not in r for r in results):
        sys.exit("results lack eviction states; regenerate them with the current fragmentation.py")
    panels = build_panels(results)
    page = (PAGE.replace("__TITLE__", html.escape(a.title))
            .replace("__SUBTITLE__", html.escape("; ".join(notes)))
            .replace("__PAPER__", str(PAPER_WINDOW_S))
            .replace("__LATB__", str(LAT_BUCKET_S))
            .replace("__DATA__", json.dumps(panels).replace("</", "<\\/")))
    with open(a.output, "w") as f:
        f.write(page)
    if a.markdown:
        with open(a.markdown, "w") as f:
            f.write(markdown(panels, notes, a.image_url))
    print(f"panels: {len(panels)}")
    print(f"wrote {a.output}: {len(panels)} panels")
    for p in panels:
        print(f"  {p['server']} {p['state']}:")
        for r in p["rows"]:
            print(f"    {r['name']:26s} mean@{PAPER_WINDOW_S:g}s {r['mean_paper']:6.1f}  mean {r['mean']:6.1f}  p50 {r['p50']:6.1f}  p90 {r['p90']:6.1f}  "
                  f"peak {r['peak']:6.1f}  final {r['final']:6.1f}  (n={r['trials']})  |  latency p50 "
                  f"{r['lat_p50']:.2f} p99 {r['lat_p99']:.2f} p99.9 {r['lat_p999']:.2f} max {r['lat_max']:.1f} ms  "
                  f"idle CPU {r['idle_cpu']:.2f} s")


if __name__ == "__main__":
    main()
