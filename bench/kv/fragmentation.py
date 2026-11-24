#!/usr/bin/env python3
"""Redis/Valkey fragmentation benchmark from the Mesh paper (PLDI'19, sec. 6.2.2).

Adapted from Redis's own active-defrag test (tests/unit/memefficiency.tcl), as
in the paper: configure the server as an LRU cache with maxmemory 100mb, insert
700,000 keys with 240-byte values, then 170,000 keys with 492-byte values
(evicting most of the first batch, leaving fragmented pages), then let the
server idle for --settle seconds.

Configurations, per server:
  jemalloc               default build, bundled jemalloc
  jemalloc+activedefrag  same, with activedefrag enabled after insertion
                         (parameters from memefficiency.tcl)
  glibc                  MALLOC=libc build
  mesh-nomesh            MALLOC=libc build + libmesh, MESH_PERIOD_MS=0
                         (same allocator and randomization, meshing disabled)
  mesh                   MALLOC=libc build + libmesh, MESH_THP=0
  mesh-thp               MALLOC=libc build + libmesh, MESH_THP=1: arena on
                         transparent huge pages (needs shmem_enabled=advise);
                         not in the default set

Each of --trials trials runs every (server, config) once, in a fresh random
order; tables report medians across trials.

Memory.  With --mstat, each server runs under the paper's mstat tool
(github.com/bpowers/mstat): its own memory cgroup, recording cgroup memory
usage (each physical page counted once, plus kernel memory such as page
tables) and HugeTLB usage.  Without --mstat, an equivalent built-in sampler
reads the same cgroup v2 counters.  Both need root; otherwise PSS is used.
RSS is reported too but is misleading for Mesh: it counts a meshed physical
page once per virtual mapping.

Latency, as in the paper (insertion time; time spent compacting):
  insert       wall time of the two DEBUG POPULATE commands
  idle CPU     server CPU time (user+sys) during the idle window: the cost of
               meshing or active defragmentation (or jemalloc purging)
  probe        a client sends GET every 10 ms during the idle window; meshing
               and active defrag both run on the server's main thread, so
               their pauses show up here (p50 / p99 / max)
  mesh passes  Mesh's own accounting: passes, total time, longest pass

With --check, exits nonzero unless, for every server, Mesh meshed in every
trial and its median final memory is at least --min-reduction below
mesh-nomesh's.

Servers bind to 127.0.0.1 only.
"""

import argparse
import json
import os
import random
import re
import signal
import socket
import statistics
import subprocess
import sys
import tempfile
import threading
import time

CONFIGS = ["jemalloc", "jemalloc+activedefrag", "glibc", "mesh-nomesh", "mesh"]

# from redis tests/unit/memefficiency.tcl ("Active defrag" test)
ACTIVEDEFRAG = [
    ("activedefrag", "yes"),
    ("active-defrag-ignore-bytes", "2mb"),
    ("active-defrag-threshold-lower", "5"),
    ("active-defrag-cycle-min", "65"),
    ("active-defrag-cycle-max", "75"),
]

FIRST = (700000, "asdf1", 240)
SECOND = (170000, "asdf2", 492)
PROBE_INTERVAL_S = 0.010
CLK_TCK = os.sysconf("SC_CLK_TCK")


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def rss_kib(pid):
    try:
        with open(f"/proc/{pid}/status") as f:
            for line in f:
                if line.startswith("VmRSS:"):
                    return int(line.split()[1])
    except OSError:
        pass
    return None


def pss_kib(pid):
    try:
        with open(f"/proc/{pid}/smaps_rollup") as f:
            for line in f:
                if line.startswith("Pss:"):
                    return int(line.split()[1])
    except OSError:
        pass
    return None


def cpu_s(pid):
    """user+system CPU seconds of all threads of pid."""
    with open(f"/proc/{pid}/stat") as f:
        fields = f.read().rsplit(")", 1)[1].split()
    return (int(fields[11]) + int(fields[12])) / CLK_TCK  # utime, stime


def pmd_mapped_mib(pid):
    """Shmem/file memory of pid mapped with huge (PMD) pages, from smaps_rollup."""
    total = 0
    try:
        with open(f"/proc/{pid}/smaps_rollup") as f:
            for line in f:
                if line.startswith(("ShmemPmdMapped:", "FilePmdMapped:", "AnonHugePages:")):
                    total += int(line.split()[1])
    except OSError:
        return None
    return round(total / 1024, 1)


CGROUP_ROOT = "/sys/fs/cgroup"


def cgroup_usable():
    try:
        # HugeTLB pages are charged to the hugetlb controller, not memory.current
        # (as mstat does, enable it if the kernel has it)
        with open(os.path.join(CGROUP_ROOT, "cgroup.controllers")) as f:
            if "hugetlb" in f.read().split():
                try:
                    with open(os.path.join(CGROUP_ROOT, "cgroup.subtree_control"), "w") as sc:
                        sc.write("+hugetlb")
                except OSError:
                    pass
        with open(os.path.join(CGROUP_ROOT, "cgroup.subtree_control")) as f:
            if "memory" not in f.read().split():
                return False
        probe = os.path.join(CGROUP_ROOT, f"mesh-kv-probe-{os.getpid()}")
        os.mkdir(probe)
        os.rmdir(probe)
        return True
    except OSError:
        return False


class Cgroup:
    def __init__(self, name):
        self.path = os.path.join(CGROUP_ROOT, f"mesh-kv-{os.getpid()}-{name}")
        os.mkdir(self.path)

    def add(self, pid):
        with open(os.path.join(self.path, "cgroup.procs"), "w") as f:
            f.write(str(pid))

    def current_kib(self):
        try:
            with open(os.path.join(self.path, "memory.current")) as f:
                total = int(f.read())
            for name in os.listdir(self.path):  # hugetlb.2MB.current etc.
                if name.startswith("hugetlb.") and name.endswith(".current") and ".rsvd." not in name:
                    with open(os.path.join(self.path, name)) as f:
                        total += int(f.read())
            return total // 1024
        except OSError:
            return None

    def remove(self):
        for _ in range(50):
            try:
                os.rmdir(self.path)
                return
            except OSError:
                time.sleep(0.1)


class Server:
    def __init__(self, binary, cli, env, log_path, cgroup=None, mstat=None, mstat_tsv=None):
        self.port = free_port()
        self.cli_bin = cli
        self.log = open(log_path, "w")
        self.log_path = log_path
        args = [binary, "--port", str(self.port), "--bind", "127.0.0.1", "--save", "",
                "--appendonly", "no", "--enable-debug-command", "yes", "--daemonize", "no"]
        preexec = None
        if mstat:
            # mstat creates the cgroup, adds its child, then execs the server
            args = [mstat, "-o", mstat_tsv, "-freq", "10", "--"] + args
        elif cgroup:
            # join the cgroup before exec so every page is charged to it
            preexec = lambda: cgroup.add(os.getpid())
        self.launched = time.time()
        self.proc = subprocess.Popen(args, env=env, stdout=self.log, stderr=subprocess.STDOUT,
                                     preexec_fn=preexec)
        self.pid = self.proc.pid
        deadline = time.time() + 20
        while time.time() < deadline:
            if self.proc.poll() is not None:
                raise RuntimeError(f"server exited early; see {log_path}")
            try:
                if self.cli("PING") == "PONG":
                    if mstat:
                        self.pid = self._find_descendant(os.path.basename(binary))
                    return
            except subprocess.CalledProcessError:
                pass
            time.sleep(0.1)
        raise RuntimeError(f"server did not start; see {log_path}")

    def _find_descendant(self, exe_name):
        """PID of the server under mstat (mstat re-execs itself, then runs it)."""
        todo = [self.proc.pid]
        while todo:
            pid = todo.pop()
            try:
                if os.path.basename(os.readlink(f"/proc/{pid}/exe")) == exe_name:
                    return pid
                for tid in os.listdir(f"/proc/{pid}/task"):
                    with open(f"/proc/{pid}/task/{tid}/children") as f:
                        todo.extend(int(c) for c in f.read().split())
            except OSError:
                pass
        raise RuntimeError(f"no {exe_name} process under mstat (pid {self.proc.pid})")

    def cli(self, *args, timeout=300):
        out = subprocess.run([self.cli_bin, "-p", str(self.port), *args], capture_output=True, text=True,
                             timeout=timeout, check=True).stdout.strip()
        if out.startswith("ERR") or out.startswith("(error)"):
            raise RuntimeError(f"{' '.join(args)}: {out}")
        return out

    def info(self, section):
        d = {}
        for line in self.cli("INFO", section).splitlines():
            if ":" in line:
                k, v = line.split(":", 1)
                d[k] = v.strip()
        return d

    def shutdown(self):
        try:
            subprocess.run([self.cli_bin, "-p", str(self.port), "SHUTDOWN", "NOSAVE"], capture_output=True,
                           timeout=30)
        except subprocess.TimeoutExpired:
            pass
        try:
            self.proc.wait(timeout=30)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            self.proc.wait()
        self.log.close()


class Probe:
    """GETs a random second-batch key every PROBE_INTERVAL_S; records latencies."""

    def __init__(self, port, t0):
        self.sock = socket.create_connection(("127.0.0.1", port))
        self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.buf = b""
        self.latencies = []  # seconds
        self.times = []  # when each request was sent, seconds since t0
        self.t0 = t0
        self.stop = threading.Event()
        self.thread = threading.Thread(target=self._run, daemon=True)

    def _read_reply(self):
        def line():
            while b"\r\n" not in self.buf:
                chunk = self.sock.recv(65536)
                if not chunk:
                    raise ConnectionError("server closed")
                self.buf += chunk
            l, self.buf = self.buf.split(b"\r\n", 1)
            return l

        head = line()
        if head[:1] == b"$" and head != b"$-1":
            n = int(head[1:])
            while len(self.buf) < n + 2:
                chunk = self.sock.recv(65536)
                if not chunk:
                    raise ConnectionError("server closed")
                self.buf += chunk
            self.buf = self.buf[n + 2:]

    def _run(self):
        rng = random.Random(1)
        nxt = time.perf_counter()
        while not self.stop.is_set():
            key = f"{SECOND[1]}:{rng.randrange(SECOND[0])}".encode()
            cmd = b"*2\r\n$3\r\nGET\r\n$%d\r\n%s\r\n" % (len(key), key)
            t = time.perf_counter()
            self.times.append(time.time() - self.t0)
            self.sock.sendall(cmd)
            self._read_reply()
            self.latencies.append(time.perf_counter() - t)
            nxt += PROBE_INTERVAL_S
            delay = nxt - time.perf_counter()
            if delay > 0:
                self.stop.wait(delay)
            else:
                nxt = time.perf_counter()

    def start(self):
        self.thread.start()

    def finish(self):
        self.stop.set()
        self.thread.join()
        self.sock.close()


def percentile(vals, p):
    if not vals:
        return 0.0
    vals = sorted(vals)
    return vals[min(len(vals) - 1, int(round(p / 100 * (len(vals) - 1))))]


def read_mstat_tsv(path):
    """[(seconds since mstat's first sample, KiB)] from mstat's usage + hugetlb columns."""
    out = []
    with open(path) as f:
        next(f)  # time rss kernel hugetlb
        for line in f:
            t_ns, usage, kernel, hugetlb = (int(x) for x in line.split())
            out.append((t_ns / 1e9, (usage + kernel + hugetlb) // 1024))  # kernel is 0 on cgroup v2
    return out


# Generic events, valid on any CPU with a PMU.  On Intel, add
# dtlb_load_misses.walk_active,dtlb_store_misses.walk_active for cycles spent walking page tables.
PERF_EVENTS = "dTLB-load-misses,dTLB-store-misses,cycles,instructions"


def read_perf_csv(path):
    """{event: count} from perf stat -x , output (unsupported/uncounted events omitted)."""
    out = {}
    with open(path) as f:
        for line in f:
            parts = line.strip().split(",")
            if len(parts) >= 3 and parts[0].replace(".", "").isdigit():
                out[parts[2]] = int(float(parts[0]))
    return out


def run_one(server_name, config, a, logdir, trial, use_cgroup, mstat):
    jemalloc = config.startswith("jemalloc")
    binary = os.path.join(a.bin, f"{server_name}-server-{'jemalloc' if jemalloc else 'libc'}")
    cli = os.path.join(a.bin, f"{server_name}-cli")
    env = {k: v for k, v in os.environ.items()
           if k not in ("LD_PRELOAD", "MESH_PERIOD_MS", "MALLOCSTATS", "MESH_THP")}
    if config.startswith("mesh"):
        env["LD_PRELOAD"] = a.libmesh
        env["MALLOCSTATS"] = "2"  # level 2 adds the arena accounting (ACCT lines)
        if config == "mesh-nomesh":
            env["MESH_PERIOD_MS"] = "0"
        # THP mode is on by default where the kernel allows it; pin it either way
        env["MESH_THP"] = "1" if config == "mesh-thp" else "0"

    tag = f"{server_name}-{config.replace('+', '-')}-{trial}"
    log_path = os.path.join(logdir, f"{tag}.log")
    tsv = os.path.join(logdir, f"{tag}.mstat.tsv")
    cg = Cgroup(tag) if use_cgroup and not mstat else None
    srv = Server(binary, cli, env, log_path, cg, mstat, tsv)
    pid = srv.pid

    def memory_kib():  # the primary metric, when we sample it ourselves
        if mstat:
            return None
        return cg.current_kib() if cg else pss_kib(pid)

    primary, rss = [], []  # (seconds since start of insertion, KiB)
    stop = threading.Event()
    t0 = time.time()

    def sampler():
        while not stop.is_set():
            now = time.time() - t0
            m, r = memory_kib(), rss_kib(pid)
            if m is not None:
                primary.append((now, m))
            if r is not None:
                rss.append((now, r))
            stop.wait(0.1)

    th = threading.Thread(target=sampler, daemon=True)
    probe = None
    perf, counters = None, {}
    try:
        srv.cli("CONFIG", "SET", "maxmemory", "100mb")
        srv.cli("CONFIG", "SET", "maxmemory-policy", a.policy)
        if a.perf:  # hardware counters for the server, insertion through the end of the window
            perf_out = os.path.join(logdir, f"{tag}.perf.csv")
            perf = subprocess.Popen(["perf", "stat", "-x", ",", "-e", a.perf, "-p", str(pid), "-o", perf_out])
            time.sleep(0.5)  # let perf attach
        t0 = time.time()
        th.start()
        srv.cli("DEBUG", "POPULATE", *map(str, FIRST))
        if a.populate_gap > 0:
            # LRU timestamps have 1 s resolution: a gap of more than a second
            # makes the first batch reliably older than the second
            time.sleep(a.populate_gap)
        srv.cli("DEBUG", "POPULATE", *map(str, SECOND))
        srv.cli("DBSIZE")  # eviction runs before each command
        insert_s = time.time() - t0
        mem_after_insert = memory_kib()
        if config == "jemalloc+activedefrag":
            for k, v in ACTIVEDEFRAG:
                srv.cli("CONFIG", "SET", k, v)
        cpu0 = cpu_s(pid)
        probe = Probe(srv.port, t0)
        probe.start()
        time.sleep(a.settle)
        probe.finish()
        idle_cpu_s = cpu_s(pid) - cpu0
        mem = srv.info("memory")
        stats = srv.info("stats")
        keys = int(srv.cli("DBSIZE"))
        pmd_mib = pmd_mapped_mib(pid)
        if perf:
            perf.send_signal(signal.SIGINT)
            perf.wait(timeout=30)
            counters = read_perf_csv(perf_out)
    finally:
        if probe and not probe.stop.is_set():
            probe.stop.set()
        stop.set()
        if th.is_alive():
            th.join()
        srv.shutdown()
        if cg:
            cg.remove()

    with open(log_path) as f:
        log = f.read()
    m = re.search(r"MESH COUNT:\s+(\d+)", log)
    mesh_count = int(m.group(1)) if m else None
    m = re.search(r"Arena VMAs:\s+(\d+)", log)
    arena_vmas = int(m.group(1)) if m else None
    m = re.search(r"Write barrier:\s+(\S+)", log)
    barrier = m.group(1) if m else None
    m = re.search(r"Meshed MB HWM:\s+([\d.]+)", log)
    meshed_mb_hwm = float(m.group(1)) if m else None
    m = re.search(r"Mesh passes:\s+(\d+) \(total ([\d.]+) ms, max ([\d.]+) ms", log)
    passes, pass_total_ms, pass_max_ms = (int(m.group(1)), float(m.group(2)), float(m.group(3))) if m else (
        None, None, None)

    end = rss[-1][0] if rss else 0
    if mstat:
        # mstat's clock starts at its first sample, ~at launch; align to t0
        offset = t0 - srv.launched
        primary = [(t - offset, kib) for t, kib in read_mstat_tsv(tsv)]
        primary = [p for p in primary if 0 <= p[0] <= end]
        mem_after_insert = min(primary, key=lambda p: abs(p[0] - insert_s))[1] if primary else 0

    def stat(series, f, lo, hi):
        vals = [v for t, v in series if lo <= t <= hi]
        return round(f(vals) / 1024, 1) if vals else 0

    lat_ms = [x * 1000 for x in probe.latencies] if probe else []
    return {
        "server": server_name,
        "config": config,
        "trial": trial,
        "metric": "mstat" if mstat else "cgroup" if cg else "PSS",
        "keys": keys,
        "mem_after_insert_mib": round((mem_after_insert or 0) / 1024, 1),
        "mem_final_mib": stat(primary, statistics.median, end - 1.0, end),
        "mem_mean_mib": stat(primary, statistics.mean, insert_s, end),
        "mem_peak_mib": stat(primary, max, 0, end),
        "rss_final_mib": stat(rss, statistics.median, end - 1.0, end),
        "used_memory_mib": round(int(mem.get("used_memory", 0)) / 2**20, 1),
        # the allocator's own view (jemalloc builds report these; libc builds don't)
        "allocator_mib": {k: round(int(mem[k]) / 2**20, 1) for k in
                          ("allocator_allocated", "allocator_active", "allocator_resident", "allocator_frag_bytes",
                           "allocator_rss_bytes", "rss_overhead_bytes", "used_memory_rss")
                          if mem.get(k, "").lstrip("-").isdigit()},
        "accounting": re.findall(r"ACCT .*", log),
        "probe": {"t_s": [round(t, 3) for t in probe.times] if probe else [],
                  "ms": [round(x, 3) for x in lat_ms]},
        "insert_s": round(insert_s, 3),
        "idle_cpu_s": round(idle_cpu_s, 3),
        "probe_n": len(lat_ms),
        "probe_p50_ms": round(percentile(lat_ms, 50), 3),
        "probe_p99_ms": round(percentile(lat_ms, 99), 3),
        "probe_max_ms": round(max(lat_ms), 3) if lat_ms else 0,
        "active_defrag_hits": int(stats.get("active_defrag_hits", 0)),
        "mesh_count": mesh_count,
        "barrier": barrier,
        # memory the kernel maps with 2 MiB pages (THP), at the end of the window
        "pmd_mapped_mib": pmd_mib,
        "perf": counters,
        "arena_vmas": arena_vmas,
        "meshed_mb_hwm": meshed_mb_hwm,
        "mesh_passes": passes,
        "mesh_pass_total_ms": pass_total_ms,
        "mesh_pass_max_ms": pass_max_ms,
        "samples": {"t_s": [t for t, _ in primary], "mem_kib": [v for _, v in primary],
                    "rss_t_s": [t for t, _ in rss], "rss_kib": [v for _, v in rss]},
    }


def label_eviction_states(results):
    """Tag each result with its server's eviction outcome ("191k keys", ...).

    How many keys survive eviction is bimodal: LRU timestamps have 1 s
    resolution, so whether the two inserted batches get distinguishable
    timestamps decides whether eviction removes mostly the older 240-byte keys
    (fewer keys left) or picks among both batches (more, smaller keys left,
    a more fragmented heap).  Key counts are clustered per server: a new
    state starts where the count jumps by more than 10%.
    """
    for server in sorted({r["server"] for r in results}):
        rs = sorted((r for r in results if r["server"] == server), key=lambda r: r["keys"])
        clusters = [[rs[0]]] if rs else []
        for r in rs[1:]:
            if r["keys"] > clusters[-1][-1]["keys"] * 1.10:
                clusters.append([])
            clusters[-1].append(r)
        for c in clusters:
            label = f"{round(statistics.median(x['keys'] for x in c) / 1000)}k keys"
            for x in c:
                x["state"] = label


def aggregate(results, by_state=False):
    """{(server, config[, state]): {metric: median over trials}} plus *_min/*_max for final memory."""
    groups = {}
    for r in results:
        key = (r["server"], r["config"], r["state"]) if by_state else (r["server"], r["config"])
        groups.setdefault(key, []).append(r)
    agg = {}
    for key, rs in groups.items():
        d: dict = {"trials": len(rs)}
        for k, v in rs[0].items():
            if isinstance(v, (int, float)) and not isinstance(v, bool) and k != "trial":
                d[k] = statistics.median(r[k] for r in rs)
            elif v is None:
                d[k] = None
        d["mem_final_min"] = min(r["mem_final_mib"] for r in rs)
        d["mem_final_max"] = max(r["mem_final_mib"] for r in rs)
        d["mesh_count_min"] = min((r["mesh_count"] or 0) for r in rs)
        agg[key] = d
    return agg


def fmt(v, spec=".1f"):
    return "" if v is None else format(v, spec)


def tables(agg, order, metric, trials, settle):
    """Rows per (server, config, eviction state); medians within each."""
    mem = [f"Memory ({metric}, MiB): median over the trials in each eviction state (n), final range in "
           f"brackets; {trials} trials per config.  *RSS counts a meshed page once per virtual mapping.", "",
           "| server | config | eviction state | n | after insert | final | mean | RSS final* | used_memory |",
           "|---|---|---|---|---|---|---|---|---|"]
    lat = [f"Latency: median over the trials in each eviction state.  Idle window = {settle:g} s after "
           f"insertion; probe = GET every {PROBE_INTERVAL_S * 1000:g} ms during it.", "",
           "| server | config | eviction state | insert (s) | idle CPU (s) | probe p50 (ms) | probe p99 (ms) | "
           "probe max (ms) | mesh passes | meshing total (ms) | longest pass (ms) | meshes | defrag hits |",
           "|---|---|---|---|---|---|---|---|---|---|---|---|---|"]
    rows = []
    for s, c in order:
        states = sorted((k for k in agg if k[0] == s and k[1] == c), key=lambda k: agg[k]["keys"])
        rows.extend(states)
    for key in rows:
        d = agg[key]
        s, c, st = key
        mem.append(f"| {s} | {c} | {st} | {d['trials']} | {fmt(d['mem_after_insert_mib'])} | "
                   f"{fmt(d['mem_final_mib'])} [{fmt(d['mem_final_min'])}–{fmt(d['mem_final_max'])}] | "
                   f"{fmt(d['mem_mean_mib'])} | {fmt(d['rss_final_mib'])} | {fmt(d['used_memory_mib'])} |")
        lat.append(f"| {s} | {c} | {st} | {fmt(d['insert_s'], '.2f')} | {fmt(d['idle_cpu_s'], '.2f')} | "
                   f"{fmt(d['probe_p50_ms'], '.2f')} | {fmt(d['probe_p99_ms'], '.2f')} | "
                   f"{fmt(d['probe_max_ms'], '.1f')} | {fmt(d['mesh_passes'], '.0f')} | "
                   f"{fmt(d['mesh_pass_total_ms'], '.0f')} | {fmt(d['mesh_pass_max_ms'], '.1f')} | "
                   f"{fmt(d['mesh_count'], '.0f')} | {fmt(d['active_defrag_hits'], '.0f')} |")
    return "\n".join(mem) + "\n\n" + "\n".join(lat)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--bin", default=os.path.join(os.path.dirname(__file__), "build", "bin"))
    ap.add_argument("--libmesh", required=True, help="path to libmesh.so")
    ap.add_argument("--servers", default="redis,valkey")
    ap.add_argument("--configs", default=",".join(CONFIGS))
    ap.add_argument("--trials", type=int, default=5)
    ap.add_argument("--settle", type=float, default=15.0, help="idle seconds after insertion")
    ap.add_argument("--seed", type=int, default=None, help="seed for the per-trial run order")
    ap.add_argument("--policy", default="allkeys-lru", help="maxmemory-policy (the paper: allkeys-lru)")
    ap.add_argument("--populate-gap", type=float, default=0.0,
                    help="seconds to wait between the two batches (> 1 makes eviction deterministic)")
    ap.add_argument("--no-cgroup", action="store_true", help="measure PSS instead of cgroup memory")
    ap.add_argument("--mstat", help="path to mstat (github.com/bpowers/mstat); run each server under it")
    ap.add_argument("--perf", metavar="EVENTS", nargs="?", const=PERF_EVENTS,
                    help="count perf events (comma-separated; default: TLB misses, cycles, instructions) for "
                         "each server, from insertion to the end of the window; needs perf and hardware counters")
    ap.add_argument("--json", help="write per-trial results (with memory time series) here")
    ap.add_argument("--check", action="store_true", help="fail unless meshing happened and helped")
    ap.add_argument("--min-reduction", type=float, default=0.15,
                    help="with --check: required median final-memory reduction of mesh vs mesh-nomesh")
    ap.add_argument("--max-pass-ms", type=float, default=None,
                    help="with --check: also require Mesh's median longest mesh pass to be at most this")
    a = ap.parse_args()
    a.libmesh = os.path.realpath(a.libmesh)
    logdir = tempfile.mkdtemp(prefix="kv-frag-")
    use_cgroup = not a.no_cgroup and cgroup_usable()
    mstat = os.path.realpath(a.mstat) if a.mstat else None
    metric = ("mstat: cgroup memory + hugetlb" if mstat else
              "cgroup memory.current + hugetlb" if use_cgroup else "PSS (no usable cgroup v2)")
    try:
        max_map_count = open("/proc/sys/vm/max_map_count").read().strip()
    except OSError:
        max_map_count = "?"
    print(f"measuring {metric}; {a.trials} trials, {a.settle:g} s idle window; "
          f"vm.max_map_count={max_map_count}", flush=True)

    servers, configs = a.servers.split(","), a.configs.split(",")
    order = [(s, c) for s in servers for c in configs]
    rng = random.Random(a.seed)
    results = []
    for trial in range(a.trials):
        runs = order[:]
        rng.shuffle(runs)
        for server, config in runs:
            r = run_one(server, config, a, logdir, trial, use_cgroup, mstat)
            print(f"[trial {trial + 1}/{a.trials}] {server:7s} {config:22s} final {r['mem_final_mib']:7.1f} MiB  "
                  f"insert {r['insert_s']:.2f}s  idle CPU {r['idle_cpu_s']:.2f}s  "
                  f"probe p99 {r['probe_p99_ms']:.2f} max {r['probe_max_ms']:.1f} ms  "
                  f"meshes {r['mesh_count']}  keys {r['keys']}  arena VMAs {r['arena_vmas']}" +
                  (f"  barrier {r['barrier']}" if r["barrier"] else "") +
                  (f"  dTLB load misses {r['perf']['dTLB-load-misses'] / 1e6:.2f}M"
                   if "dTLB-load-misses" in r["perf"] else ""),
                  flush=True)
            results.append(r)

    label_eviction_states(results)
    agg = aggregate(results)  # the check uses medians over all trials
    out = tables(aggregate(results, by_state=True), order, metric, a.trials, a.settle)
    if a.populate_gap > 0 or a.policy != "allkeys-lru":
        out = f"Workload: maxmemory-policy {a.policy}, {a.populate_gap:g} s between batches.\n\n" + out
    versions = os.path.join(a.bin, "VERSIONS")
    if os.path.exists(versions):
        out = ("Versions: " + ", ".join(open(versions).read().split("\n")).strip(", ") +
               f"; vm.max_map_count={max_map_count}\n\n" + out)
    print("\n" + out)
    if os.environ.get("GITHUB_STEP_SUMMARY"):
        with open(os.environ["GITHUB_STEP_SUMMARY"], "a") as f:
            f.write("## Redis/Valkey fragmentation (Mesh paper, sec. 6.2.2)\n\n" + out + "\n")
    if a.json:
        with open(a.json, "w") as f:
            json.dump({"metric": metric, "trials": a.trials, "settle_s": a.settle, "results": results}, f,
                      indent=1)
    print(f"\nserver logs: {logdir}")

    if not a.check:
        return 0
    ok = True
    for server in servers:
        mesh, nomesh = agg.get((server, "mesh")), agg.get((server, "mesh-nomesh"))
        if not mesh or not nomesh:
            print(f"CHECK {server}: need both mesh and mesh-nomesh configs")
            ok = False
            continue
        reduction = 1 - mesh["mem_final_mib"] / nomesh["mem_final_mib"]
        passed = mesh["mesh_count_min"] > 0 and reduction >= a.min_reduction and mesh["keys"] > 0
        if a.max_pass_ms is not None:
            pass_ok = (mesh["mesh_pass_max_ms"] or 0) <= a.max_pass_ms
            print(f"CHECK {server}: {'PASS' if pass_ok else 'FAIL'}: median longest mesh pass "
                  f"{mesh['mesh_pass_max_ms']} ms (need <= {a.max_pass_ms:g})")
            passed &= pass_ok
        ok &= passed
        print(f"CHECK {server}: {'PASS' if passed else 'FAIL'}: meshes in every trial "
              f"(min {mesh['mesh_count_min']}), median final {mesh['mem_final_mib']} vs "
              f"{nomesh['mem_final_mib']} MiB without meshing ({reduction:.0%} reduction, "
              f"need >= {a.min_reduction:.0%})")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
