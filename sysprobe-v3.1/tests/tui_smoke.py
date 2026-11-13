#!/usr/bin/env python3
"""Smoke test the ANSI terminal dashboard using a Linux pseudo-terminal."""
import fcntl
import os
import pty
import select
import struct
import subprocess
import sys
import termios
import time

master, slave = pty.openpty()
fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', 32, 110, 0, 0))
proc = subprocess.Popen([sys.argv[1], '--tui'], stdin=slave, stdout=slave, stderr=slave, start_new_session=True)
os.close(slave)
try:
    time.sleep(1.4)
    data = b''
    while select.select([master], [], [], 0)[0]:
        try:
            chunk = os.read(master, 65536)
            if not chunk:
                break
            data += chunk
        except OSError:
            break
    for expected in (b'SYSPROBE', b'CPU', b'MEMORY', b'PROCESSES', b'NETWORK'):
        assert expected in data, f'Missing dashboard element: {expected!r}'
    os.write(master, b'q')
    assert proc.wait(timeout=5) == 0
    print('TUI smoke test passed')
finally:
    if proc.poll() is None:
        proc.kill()
        proc.wait()
    os.close(master)
