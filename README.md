# SysProbe v2.0 

A keyboard-driven **Linux terminal Task Manager**, built in C11 for Fedora and Ubuntu. Combines a compact btop-inspired performance dashboard with Windows Task Manager–style tabs for **Processes**, **Services**, and **Performance**.

> This is a native terminal application, not a Windows GUI clone. It runs under your ordinary user permissions and never runs `sudo` automatically.

## Features

- Real-time total CPU utilization, history, RAM, swap, root filesystem usage, and network rates
- Processes from `/proc`: PID, name, user, sampled per-process CPU %, resident RAM and state
- Sort by CPU, RAM or PID and search process names
- **End task** with `SIGTERM` or **force kill** with `SIGKILL` (both require explicit confirmation)
- Checks PID plus process start time before sending a signal to avoid acting on a reused PID in ordinary cases
- Browse `systemd` user or system services, filter names, and attempt to start/stop selected services with confirmation
- Process details, pause, auto-refresh, keyboard help, terminal restoration on exit
- Previous command-line diagnostic and JSON reporting options remain available
- No external C libraries required; needs `systemctl` for the Services view

<img width="846" height="729" alt="image" src="https://github.com/user-attachments/assets/fe9c43a0-08bd-4c4f-ba4c-ad335bddf609" />
<img width="846" height="729" alt="image" src="https://github.com/user-attachments/assets/d3507490-ce07-40e1-af23-a90a01d527f4" />
<img width="841" height="736" alt="image" src="https://github.com/user-attachments/assets/9b68519a-7352-499f-b74f-e966068328af" />




## Install Fedora

```bash
sudo dnf install -y gcc make python3 git unzip
make
make test
./build/sysprobe --tui
```

For Ubuntu/Debian: `sudo apt install build-essential python3` then run the same `make` steps.

**Terminal size:** at least 78 x 24 (recommend 110 x 32 or larger). ANSI-capable terminal such as GNOME Console, kitty, Alacritty, or Konsole.

## Keyboard shortcuts

| Key | Tab / action |
|---|---|
| `1` / `2` / `3` | Processes / Services / Performance |
| `j` / `k`, up/down | Navigate list |
| `/` | Filter by process or service name (`Enter` accepts) |
| `s` | Cycle process sort: live CPU%, RAM, PID |
| `d` | Process details popup |
| `t` | End selected process: SIGTERM (graceful) |
| `K` (capital) | Force kill selected process: SIGKILL |
| `u` | Switch between user and system services |
| `e` | Stop selected service |
| `b` | Start selected service |
| `y` / `n` | Confirm / cancel pending action |
| `Space` | Pause/resume refresh |
| `r` | Force refresh |
| `?` | Help |
| `q` | Quit |

**Safety:** SIGTERM lets applications exit cleanly; SIGKILL does not. Unsaved application data can be lost. Terminating system processes may destabilize the session. Service actions invoke `systemctl --user` or `systemctl --system` with no password prompt, so operations may fail when the current account lacks authorization. Stopping network, display or other critical services can interrupt your session. There is **no enable/disable at boot** feature; start/stop affects current service operation only. The service list covers `systemd` units, not all possible init systems.

## Command-line reporting

```bash
./build/sysprobe --help
./build/sysprobe --json
./build/sysprobe --processes
./build/sysprobe --top 20
./build/sysprobe --pid 1234
./build/sysprobe --interval 500
./build/sysprobe --version
```

`--top` accepts 1–2048. CLI process lists default to resident-memory sorting and `cpu_ticks` in JSON is cumulative, **not** live CPU%. The interactive dashboard calculates sampled per-process CPU% across refreshes. First sample shows 0.0% until a second snapshot. CPU% can exceed 100 for multi-threaded processes (100% represents approximately one logical CPU).

## Source layout

```text
src/main.c          CLI parsing
src/collect.c       /proc, statvfs, process collection
src/output.c        Text and JSON reporting
src/tui.c           ANSI dashboard, process control, systemd services
include/sysprobe.h  Shared types and interfaces
tests/unit.c        Parsing unit tests
tests/integration.py CLI checks
tests/tui_smoke.py Pseudo-terminal smoke test
.github/workflows/ci.yml
Makefile
LICENSE
```

## Development and testing

```bash
make clean && make
make test
make sanitize  # creates instrumented build for manual checks
```

## Known limitations

- The TUI shows the largest **2,048 processes by resident memory**; smaller processes may be omitted on systems with more processes. Linux process listings can change between samples.
- CPU usage and network throughput are estimates based on time-separated samples; network totals include loopback and potentially multiple interfaces.
- The process name is `/proc/PID/stat` `comm`, not the full command line.
- The process identity guard reduces PID-reuse mistakes but is not atomic; `pidfd_send_signal` would be a stronger future improvement.
- Service enumeration uses a fixed `systemctl` command, but depends on a running systemd user or system manager. Some services may require privileges or be unavailable.
- No CPU-per-core panel, GPU statistics, process tree, mouse support, or full Windows Task Manager GUI in this version.
- UTF-8 names are rendered conservatively in the terminal; very long lines can be clipped by the current layout.

## License

MIT — see `LICENSE`. Independent project, not affiliated with Canonical, Fedora, or Microsoft.
