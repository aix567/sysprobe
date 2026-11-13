#!/usr/bin/env python3
import json
import os
import subprocess
import sys

binary = sys.argv[1]

def run(*args, ok=True):
    p = subprocess.run([binary, *args], text=True, capture_output=True, timeout=15)
    if ok:
        assert p.returncode == 0, (args, p.stderr)
    return p

assert "3.1.0" in run("--version").stdout
assert "--json" in run("--help").stdout
assert "--tui" in run("--help").stdout
assert "interactive terminal" in run("--tui", ok=False).stderr
assert "SysProbe v3.1" in run().stdout
snap = json.loads(run("--json", "--interval", "100").stdout)
assert snap["system"]["os"] == "Linux"
assert snap["memory"]["total_kib"] >= snap["memory"]["available_kib"]
assert 0 <= snap["cpu_percent"] <= 100
assert isinstance(snap["processes"], list) and not snap["processes"]
procs = json.loads(run("--json", "--top", "3").stdout)["processes"]
assert len(procs) <= 3
assert all(p["pid"] > 0 and p["rss_kib"] >= 0 for p in procs)
self_proc = json.loads(run("--json", "--pid", str(os.getpid())).stdout)["processes"]
assert len(self_proc) == 1, self_proc
assert self_proc[0]["pid"] == os.getpid()
for args in [("--interval", "0"), ("--top", "2049"), ("--pid", "-2"), ("--nope",)]:
    assert run(*args, ok=False).returncode == 2
print("All integration tests passed")
