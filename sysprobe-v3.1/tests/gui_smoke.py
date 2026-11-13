#!/usr/bin/env python3
"""Smoke test under xvfb-run: verify the GUI and its primary screens."""
import sys
from pathlib import Path
import time
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'gui'))
import sysprobe_gui as gui
root = gui.tk.Tk()
app = gui.SysProbeApp(root)
start = time.monotonic()
try:
    while app.last_data is None and time.monotonic() - start < 15:
        root.update()
        time.sleep(.05)
    assert app.last_data is not None, app.status.get()
    assert app.last_data['system']['os'] == 'Linux'
    for page in ('Performance', 'Services', 'Storage', 'Network', 'Users', 'Logs', 'Processes'):
        app.switch_tab(page)
        root.update()
    assert app.process_tree.get_children(), 'No processes displayed'
    print('GUI smoke test passed (seven pages and live C collector)')
finally:
    app.close()
