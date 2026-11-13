# SysProbe v3.1 — Linux Task Manager

A real graphical Linux Task Manager with a dark dashboard inspired by modern desktop monitors. **C11 collector + Python 3/Tkinter frontend**. Built for Fedora and Ubuntu; no proprietary backend or cloud access.

> The interface is inspired by the provided SysProbe design concept. This is a functioning Tkinter application, not a pixel-identical rendering of the concept image. Metrics are read from the machine where the app runs.

## Features

| Page | What it does |
|---|---|
| Processes | Live CPU time-based utilization, resident memory, process state and file descriptor count; PID/name search; sorting; process details; terminate (`SIGTERM`) and force kill (`SIGKILL`) with confirmation |
| Performance | Live CPU, RAM, swap and root disk utilization with history charts |
| Services | List system or user systemd services and request start/stop, with confirmation and normal OS permission checks |
| Storage | Mounted `/dev` devices from `df -hP` |
| Network | Combined network transfer rate and byte totals since boot; not per-process attribution |
| Users | Logged-in sessions as reported by `who` |
| Logs | Last 100 available `journalctl` entries, read only |
| Terminal | Existing `sysprobe --tui` monitoring interface |

Auto-refresh occurs every ~2 seconds. System probing uses `/proc`, filesystem statistics, and standard Linux tools. The C collector is the telemetry backend of the desktop GUI.

## Fedora installation

Install dependencies:

```bash
sudo dnf install -y gcc make python3 python3-tkinter systemd unzip
```

Extract the release:

```bash
cd ~/Downloads
unzip sysprobe-v3.1-desktop.zip
cd sysprobe-v3.1
make
make test
make desktop-check
./gui/sysprobe_gui.py
```

If the GUI file is not executable, run:

```bash
python3 gui/sysprobe_gui.py
```

To install a **SysProbe launcher in the GNOME application menu** (this writes to `/usr/local`):

```bash
sudo make install-desktop
sysprobe-gui
```

Open GNOME Activities → search **SysProbe**. Uninstall with `sudo make uninstall-desktop` from the source directory.

## Ubuntu / Debian

```bash
sudo apt update
sudo apt install -y build-essential python3 python3-tk systemd unzip
make
make test
python3 gui/sysprobe_gui.py
```

## Process controls and safety

Select a process, then **End task** sends `SIGTERM` or **Force kill** sends `SIGKILL` after a confirmation dialog. SysProbe blocks signals to PID 1 and to itself and checks process start time again to reduce PID-reuse risk. It cannot prevent every possible race, so avoid terminating core desktop components. Unsaved work may be lost. SysProbe does not elevate privileges for process operations.

The Services page uses `systemctl`, and system-level changes may fail unless permitted or authorized by your operating system. It protects several critical units from the Stop button, but this list is not exhaustive. System services should only be stopped when you understand their impact.

## Project layout

```text
sysprobe-v3.1/
├── src/                     # C monitoring engine and terminal UI
├── include/                 # C header
├── gui/sysprobe_gui.py      # Graphical desktop application
├── assets/                  # Desktop launcher and SVG icon
├── packaging/sysprobe-gui   # Launcher script
├── tests/                   # Unit, integration and GUI smoke tests
├── .github/workflows/ci.yml
├── Makefile
└── LICENSE
```

## Testing

```bash
make test              # C unit + CLI integration + terminal smoke tests
make desktop-check     # Verify Python syntax
make gui-test          # Requires xvfb-run; tests GUI in a virtual display
```

On Fedora, install the virtual display test runner with `sudo dnf install -y xorg-x11-server-Xvfb` if needed.

## Limitations

- The UI uses Tkinter widgets, not native GTK4/libadwaita or real glass blur. It is a functional approximation of the visual mockup.
- No per-process disk or network data; the Network view is system-wide.
- CPU% for each process is calculated from changes in cumulative CPU ticks; the first sample shows 0% until a second sample arrives.
- Network and CPU graphs show recent history gathered while the app has been running.
- No app-grouping, per-core graphs, GPU telemetry, process tree, privilege escalation, or service enable/disable controls yet.
- Service management is dependent on systemd, installed tools and user permissions.
- All updates are driven by local measurements. The app does not send telemetry to a server.

## Release notes: v3.1

- Redesigned desktop UI with dark navy theme, teal accent and left navigation.
- Added Storage, Network, Users and Logs pages.
- Preserved C telemetry collector, terminal UI and process/service controls.
- Improved process details and added process-safety confirmation.

## License

MIT — see [`LICENSE`](LICENSE).

Independent open-source project; not affiliated with Fedora, Canonical, Red Hat, Microsoft or the btop project.
