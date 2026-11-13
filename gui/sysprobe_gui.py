#!/usr/bin/env python3
"""SysProbe 3.1 desktop task manager. Live data comes from SysProbe's C backend."""
import json
import os
from pathlib import Path
import queue
import shutil
import signal
import subprocess
import sys
import threading
import time
import tkinter as tk
from tkinter import ttk, messagebox
from collections import deque

VERSION = '3.1.0'
BG = '#081321'
SIDE = '#111e2b'
PANEL = '#142332'
PANEL2 = '#1b2b3b'
BORDER = '#33475a'
TEXT = '#f1f5f9'
MUTED = '#99acbe'
MINT = '#62e9c0'
RED = '#ff6274'
BLUE = '#61b9fa'
ORANGE = '#f3a66b'


def backend_path():
    env = os.environ.get('SYSPROBE_BACKEND')
    if env:
        return env
    local = Path(__file__).resolve().parent.parent / 'build' / 'sysprobe'
    return str(local) if local.is_file() else shutil.which('sysprobe')


def snapshot():
    binary = backend_path()
    if not binary:
        raise RuntimeError("C backend missing: run 'make' in the project directory")
    result = subprocess.run([binary, '--json', '--top', '2048', '--interval', '250'],
                            capture_output=True, text=True, check=True, timeout=15)
    return json.loads(result.stdout)


def pid_start_time(pid):
    data = Path(f'/proc/{pid}/stat').read_text()
    return data.rsplit(') ', 1)[1].split()[19]


def memstr(kib):
    return f'{kib / 1048576:.2f} GiB' if kib >= 1048576 else f'{kib / 1024:.1f} MiB'


def bytestr(value):
    for suffix in ('B', 'KiB', 'MiB', 'GiB', 'TiB'):
        if abs(value) < 1024 or suffix == 'TiB':
            return f'{value:.1f} {suffix}'
        value /= 1024


def systemd_units(user=False):
    cmd = ['systemctl'] + (['--user'] if user else []) + [
        'list-units', '--type=service', '--all', '--plain', '--no-legend', '--no-pager']
    result = subprocess.run(cmd, capture_output=True, text=True, timeout=15, check=True)
    units = []
    for line in result.stdout.splitlines():
        cols = line.strip().lstrip('● ').split(None, 4)
        if len(cols) >= 4 and cols[0].endswith('.service'):
            units.append((cols[0], cols[2], cols[3], cols[4] if len(cols) > 4 else ''))
    return units


def read_mounts():
    result = subprocess.run(['df', '-hP'], capture_output=True, text=True, timeout=8, check=True)
    rows = []
    for line in result.stdout.splitlines()[1:]:
        c = line.split(None, 5)
        if len(c) == 6 and c[0].startswith('/dev/'):
            rows.append((c[0], c[5], c[1], c[2], c[3], c[4]))
    return rows


def read_users():
    result = subprocess.run(['who'], capture_output=True, text=True, timeout=8, check=True)
    return [line.split(None, 4) for line in result.stdout.splitlines() if line.strip()]


def read_logs():
    cmd = ['journalctl', '-n', '100', '--no-pager', '--output=short-iso']
    result = subprocess.run(cmd, capture_output=True, text=True, timeout=12)
    return result.stdout if result.stdout else (result.stderr or 'No journal entries available.')


class SysProbeApp:
    def __init__(self, root):
        self.root = root
        root.title('SysProbe v3.1 — Linux Task Manager')
        root.geometry('1360x840')
        root.minsize(950, 600)
        root.configure(bg=BG)
        root.protocol('WM_DELETE_WINDOW', self.close)
        self.closed = False
        self.busy = False
        self.queue = queue.Queue()
        self.data = None
        self.last_data = None  # compatibility with GUI smoke tests
        self.last_t = None
        self.prior = {}
        self.history = deque([0] * 50, maxlen=50)
        self.net_history = deque([0] * 50, maxlen=50)
        self.previous_net = None
        self.processes = {}
        self.services = []
        self.service_user = False
        self.page = 'Processes'
        self.search = tk.StringVar()
        self.sort = tk.StringVar(value='CPU')
        self.service_filter = tk.StringVar()
        self.scope = tk.StringVar(value='System')
        self.status = tk.StringVar(value='Starting SysProbe collector…')
        self.search.trace_add('write', lambda *_: self.render_processes())
        self.sort.trace_add('write', lambda *_: self.render_processes())
        self.service_filter.trace_add('write', lambda *_: self.render_services())
        self.configure_style()
        self.make_ui()
        self.root.after(100, self.poll)
        self.root.after(150, self.refresh)
        self.root.after(2000, self.tick)

    def configure_style(self):
        style = ttk.Style()
        style.theme_use('clam')
        style.configure('SP.Treeview', background=PANEL, fieldbackground=PANEL,
                        foreground=TEXT, rowheight=35, borderwidth=0,
                        font=('DejaVu Sans', 10))
        style.configure('SP.Treeview.Heading', background=PANEL2, foreground='#d9e7f4',
                        borderwidth=0, relief='flat', padding=8,
                        font=('DejaVu Sans', 10, 'bold'))
        style.map('SP.Treeview', background=[('selected', '#244b5a')],
                  foreground=[('selected', '#ffffff')])
        style.configure('SP.Vertical.TScrollbar', background=PANEL2, troughcolor=PANEL,
                        arrowcolor=MUTED, borderwidth=0)

    def text(self, parent, value, size=11, color=TEXT, bold=False, **kwargs):
        return tk.Label(parent, text=value, font=('DejaVu Sans', size, 'bold' if bold else 'normal'),
                        bg=parent.cget('bg'), fg=color, **kwargs)

    def btn(self, parent, value, handler, danger=False, secondary=False):
        return tk.Button(parent, text=value, command=handler, cursor='hand2', relief='flat',
                         bd=0, padx=16, pady=10, fg=RED if danger else (TEXT if secondary else MINT),
                         bg='#34212e' if danger else PANEL2,
                         activebackground='#3a4b5d', activeforeground='white',
                         font=('DejaVu Sans', 10, 'bold'))

    def make_ui(self):
        head = tk.Frame(self.root, bg=BG, padx=26, pady=18)
        head.pack(fill='x')
        brand = tk.Frame(head, bg=BG)
        brand.pack(side='left')
        self.text(brand, 'S', 33, RED, True).pack(side='left', padx=(2, 14))
        titles = tk.Frame(brand, bg=BG)
        titles.pack(side='left')
        self.text(titles, 'SysProbe', 24, TEXT, True).pack(anchor='w')
        self.text(titles, 'LINUX TASK MANAGER  /  v3.1', 9, MUTED).pack(anchor='w')
        self.btn(head, '⟳  Refresh', self.refresh, secondary=True).pack(side='right', pady=4)

        layout = tk.Frame(self.root, bg=BG, padx=18)
        layout.pack(fill='both', expand=True)
        side = tk.Frame(layout, bg=SIDE, width=236)
        side.pack(side='left', fill='y', padx=(0, 20), pady=(0, 12))
        side.pack_propagate(False)
        tk.Frame(side, bg=SIDE, height=18).pack()
        names = [('▤', 'Processes'), ('◴', 'Performance'), ('⚙', 'Services'),
                 ('▣', 'Storage'), ('◎', 'Network'), ('♙', 'Users'), ('▤', 'Logs')]
        self.nav = {}
        for symbol, name in names:
            button = tk.Button(side, text=f'  {symbol}    {name}', anchor='w', relief='flat',
                               borderwidth=0, bg=SIDE, fg=TEXT, font=('DejaVu Sans', 12),
                               padx=13, pady=16, cursor='hand2',
                               activebackground=PANEL2, activeforeground=MINT,
                               command=lambda n=name: self.switch_tab(n))
            button.pack(fill='x', padx=10, pady=3)
            self.nav[name] = button
        self.text(side, 'Fedora / Ubuntu  •  Linux', 9, MUTED).pack(side='bottom', pady=22)
        self.content = tk.Frame(layout, bg=BG)
        self.content.pack(side='left', fill='both', expand=True, pady=(0, 12))
        self.pages = {}
        for name in ['Processes', 'Performance', 'Services', 'Storage', 'Network', 'Users', 'Logs']:
            self.pages[name] = tk.Frame(self.content, bg=BG)
        self.build_process(self.pages['Processes'])
        self.build_performance(self.pages['Performance'])
        self.build_services(self.pages['Services'])
        self.build_generic(self.pages['Storage'], 'Storage', 'Mounted disks and partitions',
                           ('Device', 'Mountpoint', 'Size', 'Used', 'Available', 'Use %'),
                           (210, 210, 105, 105, 105, 105), 'storage')
        self.build_generic(self.pages['Users'], 'Users', 'Current login sessions',
                           ('User', 'Terminal', 'Date', 'Time / Host'),
                           (170, 180, 145, 290), 'users')
        self.build_network(self.pages['Network'])
        self.build_logs(self.pages['Logs'])
        bar = tk.Frame(self.root, bg=SIDE, padx=24, pady=10)
        bar.pack(fill='x')
        tk.Label(bar, textvariable=self.status, bg=SIDE, fg=MUTED,
                 font=('DejaVu Sans', 9), anchor='w').pack(fill='x')
        self.switch_tab('Processes')

    def heading(self, page, title, subtitle):
        self.text(page, title, 23, TEXT, True).pack(anchor='w', pady=(4, 5))
        self.text(page, subtitle, 10, MUTED).pack(anchor='w', pady=(0, 10))

    def entry(self, parent, variable, width=28):
        widget = tk.Entry(parent, textvariable=variable, width=width, bg=PANEL2, fg=TEXT,
                          insertbackground=TEXT, relief='flat', bd=0,
                          highlightbackground=BORDER, highlightcolor=MINT,
                          highlightthickness=1, font=('DejaVu Sans', 11))
        widget.pack(side='left', padx=(0, 12), ipady=11)
        return widget

    def table(self, page, cols, widths):
        area = tk.Frame(page, bg=PANEL, highlightbackground=BORDER, highlightthickness=1)
        area.pack(fill='both', expand=True, pady=(13, 0))
        tree = ttk.Treeview(area, columns=cols, show='headings', style='SP.Treeview', selectmode='browse')
        for column, width in zip(cols, widths):
            tree.heading(column, text=column)
            tree.column(column, width=width, minwidth=55, stretch=column in ('Process Name', 'Description', 'Mountpoint'))
        scroller = ttk.Scrollbar(area, orient='vertical', style='SP.Vertical.TScrollbar', command=tree.yview)
        tree.configure(yscrollcommand=scroller.set)
        scroller.pack(side='right', fill='y')
        tree.pack(fill='both', expand=True)
        tree.tag_configure('odd', background='#192939')
        tree.tag_configure('even', background=PANEL)
        return tree

    def build_process(self, page):
        self.heading(page, 'Processes', 'Applications and system processes • live CPU / RAM telemetry')
        toolbar = tk.Frame(page, bg=BG)
        toolbar.pack(fill='x', pady=6)
        self.entry(toolbar, self.search)
        tk.OptionMenu(toolbar, self.sort, 'CPU', 'Memory', 'Name', 'PID').pack(side='left')
        self.btn(toolbar, 'End task', lambda: self.end_task(False), danger=True).pack(side='right', padx=(8, 0))
        self.btn(toolbar, 'Force kill', lambda: self.end_task(True), secondary=True).pack(side='right')
        self.btn(toolbar, 'Details', self.process_details, secondary=True).pack(side='right', padx=(0, 9))
        self.process_tree = self.table(page,
            ('PID', 'Process Name', 'CPU %', 'Memory', 'State', 'FDs'),
            (82, 315, 110, 142, 96, 100))
        self.process_tree.bind('<Double-1>', lambda *_: self.process_details())

    def build_services(self, page):
        self.heading(page, 'Services', 'Manage systemd units • operations use your existing permissions')
        row = tk.Frame(page, bg=BG)
        row.pack(fill='x', pady=6)
        tk.OptionMenu(row, self.scope, 'System', 'User', command=lambda *_: self.change_scope()).pack(side='left', padx=(0, 10))
        self.entry(row, self.service_filter, 22)
        self.btn(row, 'Reload', self.refresh_services).pack(side='left')
        self.btn(row, 'Stop', lambda: self.service_action('stop'), danger=True).pack(side='right')
        self.btn(row, 'Start', lambda: self.service_action('start')).pack(side='right', padx=8)
        self.service_tree = self.table(page, ('Service', 'Active', 'Substate', 'Description'),
                                       (265, 100, 125, 480))

    def chart_card(self, page, label, color):
        card = tk.Frame(page, bg=PANEL, padx=18, pady=15, highlightbackground=BORDER, highlightthickness=1)
        card.pack(fill='x', pady=7)
        heading = tk.Frame(card, bg=PANEL)
        heading.pack(fill='x')
        self.text(heading, label, 12, TEXT, True).pack(side='left')
        info = self.text(heading, '—', 11, color, True)
        info.pack(side='right')
        canvas = tk.Canvas(card, height=62, bg=PANEL, highlightthickness=0)
        canvas.pack(fill='x', pady=(9, 0))
        return info, canvas, color

    def build_performance(self, page):
        self.heading(page, 'Performance', 'Live resource utilization and history')
        self.perf = tk.Frame(page, bg=BG)
        self.perf.pack(fill='both', expand=True)
        self.cards = {}
        for key, color in [('CPU', MINT), ('Memory', BLUE), ('Swap', ORANGE), ('Disk', RED)]:
            self.cards[key] = self.chart_card(self.perf, key, color)
        self.perf_history = {'Memory': deque([0] * 50, maxlen=50),
                             'Swap': deque([0] * 50, maxlen=50),
                             'Disk': deque([0] * 50, maxlen=50)}

    def build_generic(self, page, title, subtitle, cols, widths, field):
        self.heading(page, title, subtitle)
        self.btn(page, 'Refresh '+title, lambda name=title: self.refresh_extra(name)).pack(anchor='w', pady=4)
        setattr(self, field + '_tree', self.table(page, cols, widths))

    def build_network(self, page):
        self.heading(page, 'Network', 'System-wide network transfer rates and totals')
        self.net_info, self.net_canvas, _ = self.chart_card(page, 'Network traffic', BLUE)
        self.network_extra = self.text(page, 'Waiting for samples…', 11, MUTED)
        self.network_extra.pack(anchor='w', pady=13)
        self.text(page, 'Counters include all interfaces; traffic is not attributed to individual processes.',
                  10, MUTED).pack(anchor='w')

    def build_logs(self, page):
        self.heading(page, 'Logs', 'Recent system journal entries • read-only view')
        self.btn(page, 'Reload journal', lambda: self.refresh_extra('Logs')).pack(anchor='w', pady=4)
        area = tk.Frame(page, bg=PANEL, highlightbackground=BORDER, highlightthickness=1)
        area.pack(fill='both', expand=True, pady=12)
        self.logs_text = tk.Text(area, bg=PANEL, fg=TEXT, insertbackground=TEXT,
                                 relief='flat', wrap='none', font=('DejaVu Sans Mono', 9),
                                 padx=15, pady=12)
        scroll = ttk.Scrollbar(area, command=self.logs_text.yview)
        self.logs_text.configure(yscrollcommand=scroll.set)
        scroll.pack(side='right', fill='y')
        self.logs_text.pack(fill='both', expand=True)
        self.logs_text.configure(state='disabled')

    def switch_tab(self, name):
        for page in self.pages.values():
            page.pack_forget()
        self.pages[name].pack(fill='both', expand=True)
        for key, button in self.nav.items():
            button.configure(bg='#20443f' if key == name else SIDE,
                             fg=MINT if key == name else TEXT)
        self.page = name
        if name == 'Services':
            self.refresh_services()
        if name in ('Storage', 'Users', 'Logs'):
            self.refresh_extra(name)

    def tick(self):
        if not self.closed:
            self.refresh()
            self.root.after(2000, self.tick)

    def refresh(self):
        if self.closed or self.busy:
            return
        self.busy = True
        def worker():
            try:
                self.queue.put(('snapshot', snapshot()))
            except Exception as exc:
                self.queue.put(('snapshot_error', str(exc)))
        threading.Thread(target=worker, daemon=True).start()

    def poll(self):
        if self.closed:
            return
        while True:
            try:
                kind, payload = self.queue.get_nowait()
            except queue.Empty:
                break
            if kind == 'snapshot':
                self.busy = False
                self.update_snapshot(payload)
            elif kind == 'snapshot_error':
                self.busy = False
                self.status.set('Collector error: ' + payload[:180])
            elif kind == 'services':
                scope, rows = payload
                if scope == self.service_user:
                    self.services = rows
                    self.render_services()
                    self.status.set(f'{len(rows)} systemd units loaded')
            elif kind == 'extra':
                name, rows = payload
                self.show_extra(name, rows)
            elif kind == 'status':
                self.status.set(payload)
                if self.page == 'Services':
                    self.refresh_services()
        self.root.after(130, self.poll)

    def update_snapshot(self, d):
        now = time.monotonic()
        dt = now - self.last_t if self.last_t else None
        ticks = os.sysconf('SC_CLK_TCK')
        previous = self.prior
        self.prior = {}
        for p in d.get('processes', []):
            pid = p['pid']
            count = p.get('cpu_ticks', 0)
            self.prior[pid] = count
            p['live_cpu'] = max(0.0, 100 * (count - previous[pid]) / (ticks * dt)) if dt and dt > 0 and pid in previous and count >= previous[pid] else 0.0
        self.last_t = now
        self.data = d
        self.last_data = d
        self.history.append(d.get('cpu_percent', 0.0))
        self.render_processes()
        self.render_performance()
        kernel = d.get('system', {}).get('kernel', '?')
        self.status.set(f'● Live   •   {len(d.get("processes", []))} processes   •   Linux {kernel}   •   refresh 2s')

    def render_processes(self):
        if self.data is None:
            return
        tree = self.process_tree
        old = tree.selection()
        old_pid = old[0] if old else None
        q = self.search.get().strip().lower()
        arr = [p for p in self.data.get('processes', []) if q in p['name'].lower() or q in str(p['pid'])]
        keys = {'CPU': lambda x: -x['live_cpu'], 'Memory': lambda x: -x['rss_kib'],
                'PID': lambda x: x['pid'], 'Name': lambda x: x['name'].lower()}
        arr.sort(key=keys.get(self.sort.get(), keys['CPU']))
        self.processes = {str(p['pid']): p for p in arr}
        tree.delete(*tree.get_children())
        for i, p in enumerate(arr):
            pid = str(p['pid'])
            tree.insert('', 'end', iid=pid, tags=('odd' if i % 2 else 'even',),
                        values=(pid, p['name'], f'{p["live_cpu"]:.1f}%',
                                memstr(p['rss_kib']), p['state'],
                                p['fd_count'] if p['fd_count'] is not None else '—'))
        if old_pid in self.processes:
            tree.selection_set(old_pid)

    def selected_process(self):
        selection = self.process_tree.selection()
        if not selection:
            messagebox.showinfo('Select a process', 'Select a process in the table first.')
            return None
        pid = int(selection[0])
        p = self.processes.get(str(pid))
        return (pid, p) if p else None

    def process_details(self):
        selected = self.selected_process()
        if not selected:
            return
        pid, p = selected
        messagebox.showinfo('Process details',
                            f'Name: {p["name"]}\nPID: {pid}\nCPU: {p["live_cpu"]:.1f}%\n'
                            f'RSS: {memstr(p["rss_kib"])}\nState: {p["state"]}\n'
                            f'File descriptors: {p["fd_count"] if p["fd_count"] is not None else "Unavailable"}')

    def end_task(self, force):
        selected = self.selected_process()
        if not selected:
            return
        pid, p = selected
        if pid in (1, os.getpid()):
            messagebox.showwarning('Protected process', 'SysProbe blocks terminating PID 1 or its own process.')
            return
        try:
            start_time = pid_start_time(pid)
        except (OSError, IndexError, ValueError):
            messagebox.showerror('Process unavailable', 'This process exited or cannot be inspected.')
            return
        verb = 'FORCE KILL' if force else 'End task'
        if not messagebox.askyesno('Confirm process operation',
                                   f'{verb} {p["name"]} (PID {pid})?\n\n'
                                   'Unsaved data may be lost and your desktop could become unstable.'):
            return
        try:
            if pid_start_time(pid) != start_time:
                raise RuntimeError('PID was reused. Operation cancelled.')
            os.kill(pid, signal.SIGKILL if force else signal.SIGTERM)
            self.status.set(f'{"SIGKILL" if force else "SIGTERM"} sent to PID {pid}')
        except (OSError, RuntimeError) as exc:
            messagebox.showerror('Operation failed', str(exc))
        self.root.after(300, self.refresh)

    def graph(self, canvas, values, color):
        canvas.delete('all')
        width = max(canvas.winfo_width(), 100)
        height = max(canvas.winfo_height(), 50)
        canvas.create_line(0, height-2, width, height-2, fill=BORDER)
        if len(values) > 1:
            points = []
            maxval = max(100.0, max(values) * 1.1)
            for i, v in enumerate(values):
                points.extend((i * width / (len(values)-1), height-5 - (height-13) * max(0, v) / maxval))
            canvas.create_line(*points, fill=color, width=2, smooth=False)

    def render_performance(self):
        if self.data is None:
            return
        d = self.data
        m = d['memory']
        t = m['total_kib']
        used = max(0, t - m['available_kib'])
        st = m['swap_total_kib']
        su = max(0, st - m['swap_free_kib'])
        disk = d['root_disk']
        total = disk['total_bytes']
        disk_used = max(0, total - disk['available_bytes'])
        vals = {'CPU': (d['cpu_percent'], f'{d["cpu_percent"]:.1f}%'),
                'Memory': (100*used/t if t else 0, f'{memstr(used)} / {memstr(t)}'),
                'Swap': (100*su/st if st else 0, f'{memstr(su)} / {memstr(st)}'),
                'Disk': (100*disk_used/total if total else 0, f'{bytestr(disk_used)} / {bytestr(total)}')}
        for key, (percent, value) in vals.items():
            if key in self.perf_history:
                self.perf_history[key].append(percent)
                series = self.perf_history[key]
            else:
                series = self.history
            label, canvas, color = self.cards[key]
            label.configure(text=value)
            if self.page == 'Performance':
                self.graph(canvas, series, color)
        net = d['network_since_boot']
        sent = net['tx_bytes']
        recv = net['rx_bytes']
        if self.previous_net:
            old_t, rx, tx = self.previous_net
            interval = max(time.monotonic() - old_t, .01)
            down = max(0, (recv-rx)/interval)
            up = max(0, (sent-tx)/interval)
        else:
            down = up = 0
        self.previous_net = (time.monotonic(), recv, sent)
        self.net_history.append(min(100, (down + up)/1048576 * 15))
        self.net_info.configure(text=f'↓ {bytestr(down)}/s    ↑ {bytestr(up)}/s')
        self.network_extra.configure(text=f'Downloaded since boot: {bytestr(recv)}    Uploaded: {bytestr(sent)}')
        if self.page == 'Network':
            self.graph(self.net_canvas, self.net_history, BLUE)

    def change_scope(self):
        self.service_user = self.scope.get() == 'User'
        self.refresh_services()

    def refresh_services(self):
        scope = self.service_user
        self.status.set('Loading systemd units…')
        def worker():
            try:
                rows = systemd_units(scope)
                self.queue.put(('services', (scope, rows)))
            except Exception as exc:
                self.queue.put(('status', f'Could not load systemd services: {exc}'))
        threading.Thread(target=worker, daemon=True).start()

    def render_services(self):
        tree = self.service_tree
        selected = tree.selection()
        previous = selected[0] if selected else None
        query = self.service_filter.get().lower().strip()
        rows = [p for p in self.services if query in p[0].lower() or query in p[3].lower()]
        tree.delete(*tree.get_children())
        for i, row in enumerate(rows):
            tree.insert('', 'end', iid=row[0], values=row,
                        tags=('odd' if i % 2 else 'even',))
        if previous and tree.exists(previous):
            tree.selection_set(previous)

    def service_action(self, action):
        chosen = self.service_tree.selection()
        if not chosen:
            messagebox.showinfo('Select a service', 'Select a service before using its controls.')
            return
        unit = chosen[0]
        if action == 'stop' and unit in ('display-manager.service', 'dbus.service', 'systemd-logind.service', 'NetworkManager.service'):
            messagebox.showwarning('Protected service', 'SysProbe blocks stopping this critical service.')
            return
        if not messagebox.askyesno('Confirm service operation',
                                   f'{action.title()} {unit}?\n\nThis can interrupt your session or other applications.'):
            return
        scope = self.service_user
        def worker():
            cmd = ['systemctl'] + (['--user'] if scope else []) + [action, unit]
            try:
                result = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
                msg = f'{unit}: {action} completed' if result.returncode == 0 else (result.stderr or result.stdout).strip()[:220]
            except Exception as exc:
                msg = str(exc)
            self.queue.put(('status', msg))
        threading.Thread(target=worker, daemon=True).start()

    def refresh_extra(self, name):
        def worker():
            try:
                if name == 'Storage':
                    result = read_mounts()
                elif name == 'Users':
                    result = read_users()
                else:
                    result = read_logs()
                self.queue.put(('extra', (name, result)))
            except Exception as exc:
                self.queue.put(('status', f'{name}: {exc}'))
        threading.Thread(target=worker, daemon=True).start()

    def show_extra(self, name, rows):
        if name == 'Logs':
            self.logs_text.configure(state='normal')
            self.logs_text.delete('1.0', 'end')
            self.logs_text.insert('1.0', rows)
            self.logs_text.configure(state='disabled')
            return
        tree = self.storage_tree if name == 'Storage' else self.users_tree
        tree.delete(*tree.get_children())
        for i, row in enumerate(rows):
            if name == 'Users':
                row = (row + ['—', '—', '—', '—'])[:4]
            tree.insert('', 'end', values=row, tags=('odd' if i%2 else 'even',))
        self.status.set(f'{name}: {len(rows)} entries')

    def close(self):
        self.closed = True
        self.root.destroy()


def main():
    if sys.platform != 'linux':
        print('SysProbe requires Linux', file=sys.stderr)
        return 1
    try:
        root = tk.Tk()
    except tk.TclError as exc:
        print(f'Cannot open graphical display: {exc}', file=sys.stderr)
        return 1
    SysProbeApp(root)
    root.mainloop()
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
