#!/usr/bin/env python3
"""Redis/Valkey fragmentation benchmark from the Mesh paper (PLDI'19, sec. 6.2.2).

Adapted from Redis's own active-defrag test (tests/unit/memefficiency.tcl), as
in the paper: configure the server as an LRU cache with maxmemory 100mb, insert
700,000 keys with 240-byte values, then 170,000 keys with 492-byte values
(evicting most of the first batch, leaving fragmented pages), then let the
server idle while sampling RSS.

Configurations, per server:
  jemalloc               default build, bundled jemalloc
  jemalloc+activedefrag  same, with activedefrag enabled after insertion
                         (parameters from memefficiency.tcl)
  glibc                  MALLOC=libc build
  mesh-nomesh            MALLOC=libc build + libmesh, MESH_PERIOD_MS=0
                         (same allocator and randomization, meshing disabled)
  mesh                   MALLOC=libc build + libmesh

Memory is measured as in the paper's mstat tool: each server runs in its own
memory cgroup (cgroup v2; needs write access to /sys/fs/cgroup, e.g. root) and
we sample memory.current -- physical pages counted once, plus kernel memory
such as page tables.  Without a cgroup, PSS (/proc/PID/smaps_rollup) is used.
Plain RSS is also reported but is misleading for Mesh: a meshed physical page
is mapped at several virtual addresses and RSS counts it once per mapping.

With --check, exits nonzero unless, for every server, Mesh meshed (MESH COUNT
> 0) and reduced final memory by at least --min-reduction relative to
mesh-nomesh.

Servers bind to 127.0.0.1 only.
"""

import argparse
import json
import os
import re
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


CGROUP_ROOT = "/sys/fs/cgroup"


def cgroup_usable():
    try:
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
                return int(f.read()) // 1024
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
    def __init__(self, binary, cli, env, log_path, cgroup=None):
        self.port = free_port()
        self.cli_bin = cli
        self.log = open(log_path, "w")
        self.log_path = log_path
        args = [binary, "--port", str(self.port), "--bind", "127.0.0.1", "--save", "",
                "--appendonly", "no", "--enable-debug-command", "yes", "--daemonize", "no"]
        # join the cgroup before exec so every page is charged to it
        preexec = (lambda: cgroup.add(os.getpid())) if cgroup else None
        self.proc = subprocess.Popen(args, env=env, stdout=self.log, stderr=subprocess.STDOUT,
                                     preexec_fn=preexec)
        deadline = time.time() + 20
        while time.time() < deadline:
            if self.proc.poll() is not None:
                raise RuntimeError(f"server exited early; see {log_path}")
            try:
                if self.cli("PING") == "PONG":
                    return
            except subprocess.CalledProcessError:
                pass
            time.sleep(0.1)
        raise RuntimeError(f"server did not start; see {log_path}")

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


def run_one(server_name, config, bindir, libmesh, settle, logdir, use_cgroup):
    jemalloc = config.startswith("jemalloc")
    binary = os.path.join(bindir, f"{server_name}-server-{'jemalloc' if jemalloc else 'libc'}")
    cli = os.path.join(bindir, f"{server_name}-cli")
    env = {k: v for k, v in os.environ.items() if k not in ("LD_PRELOAD", "MESH_PERIOD_MS", "MALLOCSTATS")}
    if config.startswith("mesh"):
        env["LD_PRELOAD"] = libmesh
        env["MALLOCSTATS"] = "1"
        if config == "mesh-nomesh":
            env["MESH_PERIOD_MS"] = "0"

    log_path = os.path.join(logdir, f"{server_name}-{config}.log")
    cg = Cgroup(f"{server_name}-{config.replace('+', '-')}") if use_cgroup else None
    srv = Server(binary, cli, env, log_path, cg)
    pid = srv.proc.pid

    def memory_kib():  # the primary metric
        return cg.current_kib() if cg else pss_kib(pid)

    samples = []  # (seconds since start of insertion, primary KiB, RSS KiB)
    stop = threading.Event()
    t0 = time.time()

    def sampler():
        while not stop.is_set():
            m, r = memory_kib(), rss_kib(pid)
            if m is not None and r is not None:
                samples.append((time.time() - t0, m, r))
            stop.wait(0.1)

    th = threading.Thread(target=sampler, daemon=True)
    try:
        srv.cli("CONFIG", "SET", "maxmemory", "100mb")
        srv.cli("CONFIG", "SET", "maxmemory-policy", "allkeys-lru")
        t0 = time.time()
        th.start()
        srv.cli("DEBUG", "POPULATE", "700000", "asdf1", "240")
        srv.cli("DEBUG", "POPULATE", "170000", "asdf2", "492")
        srv.cli("DBSIZE")  # eviction runs before each command
        insert_s = time.time() - t0
        mem_after_insert = memory_kib()
        if config == "jemalloc+activedefrag":
            for k, v in ACTIVEDEFRAG:
                srv.cli("CONFIG", "SET", k, v)
        time.sleep(settle)
        mem = srv.info("memory")
        stats = srv.info("stats")
        keys = int(srv.cli("DBSIZE"))
    finally:
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
    m = re.search(r"Meshed MB HWM:\s+([\d.]+)", log)
    meshed_mb_hwm = float(m.group(1)) if m else None

    end = samples[-1][0] if samples else 0
    tail = [s for s in samples if s[0] >= end - 1.0] or [(0, 0, 0)]
    after = [s for s in samples if s[0] >= insert_s] or [(0, 0, 0)]
    mib = lambda kib: round(kib / 1024, 1)
    return {
        "server": server_name,
        "config": config,
        "metric": "cgroup memory.current" if cg else "PSS",
        "insert_s": round(insert_s, 3),
        "keys": keys,
        "mem_after_insert_mib": mib(mem_after_insert or 0),
        "mem_final_mib": mib(statistics.median(s[1] for s in tail)),
        "mem_mean_mib": mib(statistics.mean(s[1] for s in after)),
        "mem_peak_mib": mib(max(s[1] for s in samples)) if samples else 0,
        "rss_final_mib": mib(statistics.median(s[2] for s in tail)),
        "used_memory_mib": round(int(mem.get("used_memory", 0)) / 2**20, 1),
        "frag_ratio": float(mem.get("mem_fragmentation_ratio", "nan")),
        "active_defrag_hits": int(stats.get("active_defrag_hits", 0)),
        "mesh_count": mesh_count,
        "meshed_mb_hwm": meshed_mb_hwm,
        "samples": samples,
    }


def table(results):
    metric = results[0]["metric"] if results else "memory"
    hdr = (f"| server | config | insert (s) | after insert | final | mean | RSS final* | used_memory | "
           f"defrag hits | meshes |")
    lines = [f"Memory columns: {metric}, MiB.  *RSS counts a meshed page once per virtual mapping.", "",
             hdr, "|" + "---|" * 10]
    for r in results:
        lines.append(f"| {r['server']} | {r['config']} | {r['insert_s']:.2f} | {r['mem_after_insert_mib']} | "
                     f"{r['mem_final_mib']} | {r['mem_mean_mib']} | {r['rss_final_mib']} | "
                     f"{r['used_memory_mib']} | {r['active_defrag_hits']} | "
                     f"{'' if r['mesh_count'] is None else r['mesh_count']} |")
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--bin", default=os.path.join(os.path.dirname(__file__), "build", "bin"))
    ap.add_argument("--libmesh", required=True, help="path to libmesh.so")
    ap.add_argument("--servers", default="redis,valkey")
    ap.add_argument("--configs", default=",".join(CONFIGS))
    ap.add_argument("--settle", type=float, default=10.0, help="idle seconds after insertion")
    ap.add_argument("--no-cgroup", action="store_true", help="measure PSS instead of cgroup memory")
    ap.add_argument("--json", help="write full results (with RSS time series) here")
    ap.add_argument("--check", action="store_true", help="fail unless meshing happened and helped")
    ap.add_argument("--min-reduction", type=float, default=0.15,
                    help="with --check: required final-RSS reduction of mesh vs mesh-nomesh")
    a = ap.parse_args()
    libmesh = os.path.realpath(a.libmesh)
    logdir = tempfile.mkdtemp(prefix="kv-frag-")
    use_cgroup = not a.no_cgroup and cgroup_usable()
    print(f"measuring {'cgroup memory.current' if use_cgroup else 'PSS (no usable cgroup v2)'}", flush=True)

    results = []
    for server in a.servers.split(","):
        for config in a.configs.split(","):
            r = run_one(server, config, a.bin, libmesh, a.settle, logdir, use_cgroup)
            print(f"{server:7s} {config:22s} final {r['mem_final_mib']:7.1f} MiB  "
                  f"(after insert {r['mem_after_insert_mib']:.1f}, RSS {r['rss_final_mib']:.1f}, "
                  f"meshes {r['mesh_count']})", flush=True)
            results.append(r)

    out = table(results)
    versions = os.path.join(a.bin, "VERSIONS")
    if os.path.exists(versions):
        out = "Versions: " + ", ".join(open(versions).read().split("\n")).strip(", ") + "\n\n" + out
    print("\n" + out)
    if os.environ.get("GITHUB_STEP_SUMMARY"):
        with open(os.environ["GITHUB_STEP_SUMMARY"], "a") as f:
            f.write("## Redis/Valkey fragmentation (Mesh paper, sec. 6.2.2)\n\n" + out + "\n")
    if a.json:
        with open(a.json, "w") as f:
            json.dump(results, f, indent=1)
    print(f"\nserver logs: {logdir}")

    if not a.check:
        return 0
    ok = True
    by = {(r["server"], r["config"]): r for r in results}
    for server in a.servers.split(","):
        mesh, nomesh = by.get((server, "mesh")), by.get((server, "mesh-nomesh"))
        if not mesh or not nomesh:
            print(f"CHECK {server}: need both mesh and mesh-nomesh configs")
            ok = False
            continue
        reduction = 1 - mesh["mem_final_mib"] / nomesh["mem_final_mib"]
        meshed = (mesh["mesh_count"] or 0) > 0
        passed = meshed and reduction >= a.min_reduction and mesh["keys"] > 0
        ok &= passed
        print(f"CHECK {server}: {'PASS' if passed else 'FAIL'}: meshes={mesh['mesh_count']}, "
              f"final {mesh['mem_final_mib']} vs {nomesh['mem_final_mib']} MiB without meshing "
              f"({reduction:.0%} reduction, need >= {a.min_reduction:.0%})")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
