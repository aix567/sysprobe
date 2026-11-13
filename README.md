# SysProbe

**A lightweight Linux system information utility written in C.**

SysProbe is an open-source project focused on exploring Linux system programming, operating system interfaces, and low-level diagnostics.

The project is designed to grow from a simple command-line system information tool into a modular Linux diagnostics utility.

## Overview

SysProbe retrieves system information using native Linux and POSIX interfaces. It prioritizes simplicity, readable code, error handling, and minimal dependencies.

The project also serves as a practical learning environment for C programming, debugging, and Linux internals.

## Features

### Implemented — v0.1

- Kernel version detection
- Operating system identification
- System hostname retrieval
- CPU architecture identification
- System information through the `uname()` interface
- Basic error handling
- Lightweight command-line execution

### Planned

- CPU information and utilization monitoring
- Memory statistics through `/proc/meminfo`
- Process inspection through `/proc`
- File descriptor inspection
- JSON output
- Command-line arguments such as `--help` and `--kernel-only`
- Automated tests and continuous integration

## Requirements

- Linux operating system
- GCC or Clang compiler
- Standard C library
- Git (optional)

Developed primarily for Fedora Linux, with Ubuntu compatibility as a development goal.

## Installation

Clone the repository:

```bash
git clone https://github.com/YOUR_USERNAME/sysprobe.git
cd sysprobe
```

Compile using GCC:

```bash
gcc -std=c11 -Wall -Wextra -Wpedantic -Werror -g -o sysprobe main.c
```

Run:

```bash
./sysprobe
```

Replace `YOUR_USERNAME` with your GitHub username after publishing the repository.

## Example output

Illustrative output (system details will vary):

```text
=== SysProbe v0.1 ===
System:       Linux
Node:         fedora
Kernel:       6.x.x
Architecture: x86_64
```

## Project Structure

```text
sysprobe/
├── main.c
├── README.md
└── .gitignore
```

Additional source files, test suites, and build scripts will be introduced as the project expands.

## Technical Details

SysProbe currently uses the `uname()` function provided through `<sys/utsname.h>` to retrieve system information.

The application checks return values and reports errors using `perror()`.

**Current design principles:**

- Prefer well-documented system interfaces
- Check and handle errors
- Avoid unnecessary runtime dependencies
- Keep modules small and maintainable
- Use compiler warnings to improve code quality
- Validate behavior with reproducible tests

## Development Roadmap

| Version | Planned milestone |
|---|---|
| v0.1 | Basic system information |
| v0.2 | CPU and memory diagnostics |
| v0.3 | Process inspection |
| v0.4 | File descriptor analysis |
| v0.5 | JSON output and CLI arguments |
| v0.6 | Automated testing and CI |
| v1.0 | Stable Linux diagnostic utility |

These milestones are development targets and may change.

## Build Quality

Recommended development compilation:

```bash
gcc -std=c11 -Wall -Wextra -Wpedantic -Werror \
    -g -fsanitize=address,undefined \
    -o sysprobe main.c
```

This enables common compiler warnings and AddressSanitizer/UndefinedBehaviorSanitizer checks on supported toolchains.

## Contributing

Contributions, bug reports, and suggestions are welcome.

For changes:

1. Open an issue describing the proposed improvement.
2. Create a separate Git branch.
3. Implement and test the change.
4. Submit a pull request describing your changes.

Please keep contributions focused, readable, and documented.

## Disclaimer

SysProbe is an independent educational open-source project. It is not affiliated with, endorsed by, or maintained by Canonical, Red Hat, Fedora, or the Linux Foundation.

## Author

Developed by an aspiring Linux systems engineer, with a focus on C programming, Linux internals, and open-source development.

## License

A license has not yet been selected. Add an appropriate `LICENSE` file before distributing the project under an open-source license.
