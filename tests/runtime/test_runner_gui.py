#!/usr/bin/env python3
"""Standalone GUI test runner for KNX IoT runtime conformance tests.

Usage:
    python test_runner_gui.py

Requires: Python 3.9+, pytest, cbor2, cryptography
Optional: zeroconf (for mDNS tests)

Cross-platform: Windows, Linux, macOS (uses tkinter, ships with Python).
"""

import os
import re
import socket
import sys
import queue
import subprocess
import threading
import tkinter as tk
from tkinter import ttk
from pathlib import Path

# ---------------------------------------------------------------------------
# Path detection
# ---------------------------------------------------------------------------
SCRIPT_DIR = Path(__file__).resolve().parent
STACK_ROOT = SCRIPT_DIR.parent.parent

# ---------------------------------------------------------------------------
# Symbols (safe Unicode — supported on Win10+, Linux, macOS)
# ---------------------------------------------------------------------------
CHECK = "\u2611"    # ☑
UNCHECK = "\u2610"  # ☐
SYM_PASS = "\u2713"  # ✓
SYM_FAIL = "\u2717"  # ✗
SYM_SKIP = "\u25CB"  # ○
SYM_ERR = "\u26A0"   # ⚠

# Result line regex:  test_file.py::Class::method PASSED [ 10%]
_RESULT_RE = re.compile(
    r"(tests[\\/]runtime[\\/]\S+?\.py::\S+)"
    r"\s+(PASSED|FAILED|SKIPPED|ERROR|XFAIL|XPASS)"
    r"\s+\[\s*(\d+)%\]"
)

# Bare status line (when server output splits it from the test name)
_BARE_STATUS_RE = re.compile(
    r"^\s*(PASSED|FAILED|SKIPPED|ERROR|XFAIL|XPASS)"
    r"\s+\[\s*(\d+)%\]"
)

# Test name line without status (test started but status not yet on this line)
_TEST_START_RE = re.compile(
    r"(tests[\\/]runtime[\\/]\S+?\.py::\S+)\s*$"
)

# Server debug output prefix
_SERVER_LINE_RE = re.compile(r"^\[server\]")

# Summary line:  === 233 passed, 9 failed, 3 skipped in 690.93s ===
_SUMMARY_RE = re.compile(
    r"=+\s+(.*?)\s+in\s+[\d.]+s"
)


class TestRunnerApp:
    """Main application window."""

    def __init__(self, root: tk.Tk):
        self.root = root
        self.root.title("KNX IoT Runtime Test Runner")
        self.root.geometry("1280x800")
        self.root.minsize(900, 550)

        # State
        self._checked: dict[str, bool] = {}
        self._item_names: dict[str, str] = {}
        self._test_nodes: dict[str, str] = {}       # item_id → pytest node
        self._node_to_item: dict[str, str] = {}      # pytest node → item_id
        self._process = None
        self._output_queue: queue.Queue = queue.Queue()
        self._running = False
        self._counts = {"passed": 0, "failed": 0, "skipped": 0, "error": 0}
        self._total_selected = 0
        self._pending_test_node = None  # test name waiting for status

        self._build_ui()
        self.root.after(200, self._collect_tests)

    # ==================================================================
    # UI Construction
    # ==================================================================

    def _build_ui(self):
        self.root.columnconfigure(0, weight=1)
        self.root.rowconfigure(1, weight=1)

        self._build_settings_frame()
        self._build_main_pane()
        self._build_button_frame()

    def _build_settings_frame(self):
        frame = ttk.LabelFrame(self.root, text="Settings", padding=6)
        frame.grid(row=0, column=0, sticky="ew", padx=6, pady=(6, 2))

        # DUT mode
        ttk.Label(frame, text="DUT Mode:").pack(side=tk.LEFT, padx=(0, 4))
        self._dut_mode = ttk.Combobox(
            frame, values=["External (GUI)", "Internal (built-in server)"],
            state="readonly", width=24)
        self._dut_mode.set("External (GUI)")
        self._dut_mode.pack(side=tk.LEFT, padx=(0, 16))

        # Multicast scope
        ttk.Label(frame, text="Scope:").pack(side=tk.LEFT, padx=(0, 4))
        self._scope = ttk.Combobox(
            frame, values=["2  (link-local)", "5  (site-local)"],
            state="readonly", width=16)
        self._scope.set("2  (link-local)")
        self._scope.pack(side=tk.LEFT, padx=(0, 16))

        # Device host
        ttk.Label(frame, text="Device Host:").pack(side=tk.LEFT, padx=(0, 4))
        self._host_var = tk.StringVar()
        host_entry = ttk.Entry(frame, textvariable=self._host_var, width=28)
        host_entry.pack(side=tk.LEFT, padx=(0, 4))
        ttk.Label(frame, text="(blank = auto-discover)",
                  foreground="gray").pack(side=tk.LEFT, padx=(0, 12))

        # Network interface
        ttk.Label(frame, text="Interface:").pack(side=tk.LEFT, padx=(0, 4))
        self._iface = ttk.Combobox(frame, state="readonly", width=28)
        self._iface.pack(side=tk.LEFT, padx=(0, 4))
        ttk.Button(frame, text="\u21bb", width=2,
                   command=self._populate_interfaces).pack(
            side=tk.LEFT, padx=(0, 16))
        self._populate_interfaces()

        # Show server output toggle
        self._show_server = tk.BooleanVar(value=False)
        ttk.Checkbutton(frame, text="Show server output",
                        variable=self._show_server).pack(side=tk.LEFT)

    def _build_main_pane(self):
        pane = ttk.PanedWindow(self.root, orient=tk.HORIZONTAL)
        pane.grid(row=1, column=0, sticky="nsew", padx=6, pady=2)

        # -- Left: test tree --
        left = ttk.Frame(pane)
        pane.add(left, weight=2)

        self._tree_label = ttk.Label(left, text="Tests (collecting...)")
        self._tree_label.pack(anchor="w", padx=4, pady=(4, 0))

        tree_frame = ttk.Frame(left)
        tree_frame.pack(fill=tk.BOTH, expand=True, padx=4, pady=4)
        tree_frame.columnconfigure(0, weight=1)
        tree_frame.rowconfigure(0, weight=1)

        self.tree = ttk.Treeview(
            tree_frame, columns=("status",), selectmode="browse")
        self.tree.heading("#0", text="Test", anchor="w")
        self.tree.heading("status", text="Result", anchor="w")
        self.tree.column("#0", width=340, stretch=True)
        self.tree.column("status", width=70, stretch=False, anchor="center")

        # Tags for coloring
        self.tree.tag_configure("passed", foreground="#228B22")
        self.tree.tag_configure("failed", foreground="#DC143C")
        self.tree.tag_configure("skipped", foreground="#B8860B")
        self.tree.tag_configure("error", foreground="#FF4500")
        self.tree.tag_configure("running", foreground="#4169E1")

        vsb = ttk.Scrollbar(tree_frame, orient="vertical",
                            command=self.tree.yview)
        self.tree.configure(yscrollcommand=vsb.set)

        self.tree.grid(row=0, column=0, sticky="nsew")
        vsb.grid(row=0, column=1, sticky="ns")

        # Bind click to toggle checkbox
        self.tree.bind("<Button-1>", self._on_tree_click)

        # -- Right: output pane --
        right = ttk.Frame(pane)
        pane.add(right, weight=3)

        ttk.Label(right, text="Output").pack(anchor="w", padx=4, pady=(4, 0))

        out_frame = ttk.Frame(right)
        out_frame.pack(fill=tk.BOTH, expand=True, padx=4, pady=4)
        out_frame.columnconfigure(0, weight=1)
        out_frame.rowconfigure(0, weight=1)

        self.output = tk.Text(out_frame, wrap=tk.WORD, state=tk.DISABLED,
                              bg="#1e1e1e", fg="#d4d4d4",
                              insertbackground="#d4d4d4",
                              selectbackground="#264f78",
                              font=self._mono_font())
        self.output.tag_configure("pass_line", foreground="#6A9955")
        self.output.tag_configure("fail_line", foreground="#F44747")
        self.output.tag_configure("skip_line", foreground="#CCA700")
        self.output.tag_configure("error_line", foreground="#FF6A00")
        self.output.tag_configure("server_line", foreground="#666666")
        self.output.tag_configure("heading", foreground="#569CD6",
                                  font=(self._mono_font_family(), 10, "bold"))

        osb = ttk.Scrollbar(out_frame, orient="vertical",
                            command=self.output.yview)
        self.output.configure(yscrollcommand=osb.set)

        self.output.grid(row=0, column=0, sticky="nsew")
        osb.grid(row=0, column=1, sticky="ns")

    def _build_button_frame(self):
        frame = ttk.Frame(self.root, padding=6)
        frame.grid(row=2, column=0, sticky="ew", padx=6, pady=(2, 6))

        # Selection buttons
        ttk.Button(frame, text="Select All",
                   command=self._select_all).pack(side=tk.LEFT, padx=2)
        ttk.Button(frame, text="Select None",
                   command=self._select_none).pack(side=tk.LEFT, padx=2)
        ttk.Button(frame, text="Select Failed",
                   command=self._select_failed).pack(side=tk.LEFT, padx=2)

        ttk.Separator(frame, orient="vertical").pack(
            side=tk.LEFT, fill="y", padx=8)

        ttk.Button(frame, text="Refresh",
                   command=self._collect_tests).pack(side=tk.LEFT, padx=2)
        ttk.Button(frame, text="Clear Output",
                   command=self._clear_output).pack(side=tk.LEFT, padx=2)

        ttk.Separator(frame, orient="vertical").pack(
            side=tk.LEFT, fill="y", padx=8)

        # Run / Stop
        self._run_btn = ttk.Button(
            frame, text="\u25B6  Run Selected", command=self._run_tests)
        self._run_btn.pack(side=tk.LEFT, padx=2)

        self._stop_btn = ttk.Button(
            frame, text="\u25A0  Stop", command=self._stop_tests,
            state=tk.DISABLED)
        self._stop_btn.pack(side=tk.LEFT, padx=2)

        # Summary label (right side)
        self._summary_var = tk.StringVar(value="")
        ttk.Label(frame, textvariable=self._summary_var,
                  font=("TkDefaultFont", 10, "bold")).pack(
            side=tk.RIGHT, padx=8)

        # Progress bar
        self._progress = ttk.Progressbar(frame, length=160, mode="determinate")
        self._progress.pack(side=tk.RIGHT, padx=8)

    # ==================================================================
    # Font helpers
    # ==================================================================

    def _mono_font_family(self) -> str:
        if sys.platform == "win32":
            return "Consolas"
        elif sys.platform == "darwin":
            return "Menlo"
        return "DejaVu Sans Mono"

    def _mono_font(self):
        return (self._mono_font_family(), 10)

    # ==================================================================
    # Test collection
    # ==================================================================

    def _collect_tests(self):
        """Run pytest --collect-only and populate the tree."""
        self._tree_label.config(text="Tests (collecting...)")

        # Clear existing tree
        for item in self.tree.get_children():
            self.tree.delete(item)
        self._checked.clear()
        self._item_names.clear()
        self._test_nodes.clear()
        self._node_to_item.clear()

        def _do_collect():
            try:
                result = subprocess.run(
                    [sys.executable, "-m", "pytest",
                     "--collect-only", "-q", "tests/runtime/"],
                    capture_output=True, text=True, cwd=str(STACK_ROOT),
                    timeout=30, env={**os.environ, "PYTHONDONTWRITEBYTECODE": "1"}
                )
                lines = result.stdout.strip().splitlines()
            except Exception as e:
                lines = [f"ERROR: {e}"]

            self._output_queue.put(("collect_done", lines))

        threading.Thread(target=_do_collect, daemon=True).start()
        self._poll_collection()

    def _poll_collection(self):
        """Poll for collection results."""
        try:
            msg_type, data = self._output_queue.get_nowait()
            if msg_type == "collect_done":
                self._populate_tree(data)
                return
        except queue.Empty:
            pass
        self.root.after(100, self._poll_collection)

    def _populate_tree(self, lines: list[str]):
        """Parse collected test IDs and build the tree.

        Hierarchy derived from test method names (e.g. test_5_4_1_6_...):
          Section "5.4" → leaf "5.4.1.6 trigger multicast write"
        Every node has a numeric label.  Sorted numerically.
        """
        import re
        from collections import OrderedDict

        # Collect (node_id, method_name) pairs
        tests: list[tuple[str, str]] = []
        for line in lines:
            line = line.strip()
            if not line or line.startswith("=") or "test" not in line:
                continue
            parts = line.split("::")
            if len(parts) < 2:
                continue
            filename = parts[0].replace("\\", "/").split("/")[-1]
            if len(parts) == 3:
                cls, method = parts[1], parts[2]
                node_id = f"tests/runtime/{filename}::{cls}::{method}"
            elif len(parts) == 2:
                method = parts[1]
                node_id = f"tests/runtime/{filename}::{method}"
            else:
                continue
            tests.append((node_id, method))

        def _parse_test_name(method):
            """Parse test_5_4_1_6_description → ((5,4,1,6), '5.4.1.6', 'description')."""
            # Match: test_<major>_<minor>_<sub>_<case><optional letter suffix>_<description>
            m = re.match(r"test_(\d+)_(\d+)_(\d+)_(\d+[a-z]?)(?:_(.*))?$", method)
            if m:
                a, b, c, d = m.group(1), m.group(2), m.group(3), m.group(4)
                # Numeric sort key: strip trailing letter from d
                d_num = int(re.match(r"\d+", d).group())
                num_tuple = (int(a), int(b), int(c), d_num)
                eitt = f"{a}.{b}.{c}.{d}"
                suffix = (m.group(5) or "").replace("_", " ")
                return num_tuple, eitt, suffix
            # Fallback
            nums = re.findall(r"\d+", method)
            num_tuple = tuple(int(n) for n in nums) if nums else (9999,)
            eitt = ".".join(nums) if nums else method
            suffix = re.sub(r"^test_[\d_]+", "", method).strip("_").replace("_", " ")
            return num_tuple, eitt, suffix

        # Parse and sort all tests numerically
        parsed = []
        for node_id, method in tests:
            num_tuple, eitt, suffix = _parse_test_name(method)
            section = (f"{num_tuple[0]}.{num_tuple[1]}"
                       if len(num_tuple) >= 2 else str(num_tuple[0]))
            parsed.append((section, num_tuple, eitt, suffix, node_id))
        parsed.sort(key=lambda x: x[1])

        # Group by section
        sections: OrderedDict[str, list] = OrderedDict()
        for item in parsed:
            sec = item[0]
            if sec not in sections:
                sections[sec] = []
            sections[sec].append(item)

        # Collect section names from filenames
        sec_names: dict[str, set[str]] = {}
        for line in lines:
            line = line.strip()
            if "::" not in line:
                continue
            fname = line.split("::")[0].replace("\\", "/").split("/")[-1]
            m = re.match(r"test_(\d+)_(\d+)_(.*?)\.py", fname)
            if m:
                sec = f"{m.group(1)}.{m.group(2)}"
                name = m.group(3).replace("_", " ")
                if sec not in sec_names:
                    sec_names[sec] = set()
                sec_names[sec].add(name)

        # Build tree: section → leaf tests (flat)
        test_count = 0
        for sec, items in sections.items():
            # Section label: "5.4 group comm" or "5.1 discovery, ..."
            names = sec_names.get(sec, set())
            if len(names) == 1:
                sec_label = f"{sec} {next(iter(names))}"
            elif names:
                sec_label = f"{sec} {', '.join(sorted(names))}"
            else:
                sec_label = sec
            sec_id = self.tree.insert(
                "", "end",
                text=f"{CHECK} {sec_label}",
                open=False)
            self._checked[sec_id] = True
            self._item_names[sec_id] = sec_label

            for _sec, _nums, eitt, suffix, node_id in items:
                label = f"{eitt} {suffix}" if suffix else eitt
                item_id = self.tree.insert(
                    sec_id, "end",
                    text=f"{CHECK} {label}",
                    open=False)
                self._checked[item_id] = True
                self._item_names[item_id] = label
                self._test_nodes[item_id] = node_id
                self._node_to_item[node_id] = item_id
                test_count += 1

        self._tree_label.config(text=f"Tests ({test_count})")
        self._update_summary()

    # ==================================================================
    # Checkbox handling
    # ==================================================================

    def _on_tree_click(self, event):
        """Toggle checkbox on click."""
        item = self.tree.identify_row(event.y)
        if not item:
            return
        region = self.tree.identify_region(event.x, event.y)
        # Toggle on click anywhere on the row text/icon
        if region in ("tree", "cell"):
            self._toggle_check(item)

    def _toggle_check(self, item_id):
        new_state = not self._checked.get(item_id, False)
        self._set_checked(item_id, new_state)
        self._update_parent_check(item_id)
        self._update_summary()

    def _set_checked(self, item_id, state: bool):
        """Set check state and cascade to children."""
        self._checked[item_id] = state
        name = self._item_names.get(item_id, "")
        sym = CHECK if state else UNCHECK
        self.tree.item(item_id, text=f"{sym} {name}")
        # Cascade to children
        for child in self.tree.get_children(item_id):
            self._set_checked(child, state)

    def _update_parent_check(self, item_id):
        """Update parent check state based on children."""
        parent = self.tree.parent(item_id)
        if not parent:
            return
        children = self.tree.get_children(parent)
        all_checked = all(self._checked.get(c, False) for c in children)
        any_checked = any(self._checked.get(c, False) for c in children)
        self._checked[parent] = any_checked
        name = self._item_names.get(parent, "")
        sym = CHECK if all_checked else (UNCHECK if not any_checked else CHECK)
        self.tree.item(parent, text=f"{sym} {name}")
        self._update_parent_check(parent)

    def _select_all(self):
        for item in self.tree.get_children():
            self._set_checked(item, True)
        self._update_summary()

    def _select_none(self):
        for item in self.tree.get_children():
            self._set_checked(item, False)
        self._update_summary()

    def _select_failed(self):
        """Select only tests that failed in the last run."""
        self._select_none()
        for item_id, node_id in self._test_nodes.items():
            status = self.tree.item(item_id, "values")
            if status and status[0] in (f"{SYM_FAIL} FAILED",
                                         f"{SYM_ERR} ERROR"):
                self._set_checked(item_id, True)
                self._update_parent_check(item_id)
        self._update_summary()

    # ==================================================================
    # ==================================================================
    # Network interface detection
    # ==================================================================

    def _populate_interfaces(self):
        """Fill the interface combobox with connected IPv6 interfaces."""
        choices = ["(auto-detect)"]
        try:
            # Use netsh to get connected IPv6 interfaces with proper indices
            result = subprocess.run(
                ["netsh", "interface", "ipv6", "show", "interface"],
                capture_output=True, text=True, timeout=5)
            for line in result.stdout.splitlines():
                parts = line.split()
                # Lines: Idx Met MTU State Name...
                if len(parts) >= 5 and parts[0].isdigit():
                    idx = parts[0]
                    state = parts[3]
                    name = " ".join(parts[4:])
                    if state == "connected":
                        choices.append(f"{name}  (idx {idx})")
        except (OSError, subprocess.TimeoutExpired):
            # Fallback: list all interfaces
            try:
                for idx, name in socket.if_nameindex():
                    choices.append(f"{name}  (idx {idx})")
            except OSError:
                pass
        prev = self._iface.get() if hasattr(self, '_iface') else ""
        self._iface["values"] = choices
        if prev in choices:
            self._iface.set(prev)
        else:
            self._iface.set(choices[0])

    # ==================================================================
    # Test execution
    # ==================================================================

    def _get_env(self) -> dict:
        """Build environment variables for the pytest subprocess."""
        env = dict(os.environ)
        env["PYTHONDONTWRITEBYTECODE"] = "1"
        env["RUNTIME_TEST_QUIET"] = "1"  # suppress server debug output

        # DUT mode
        if "External" in self._dut_mode.get():
            env["EXTERNAL_DUT"] = "1"
        else:
            env.pop("EXTERNAL_DUT", None)

        # Multicast scope
        scope = self._scope.get().split()[0].strip()
        env["KNX_MULTICAST_SCOPE"] = scope

        # Device host
        host = self._host_var.get().strip()
        if host:
            env["DEVICE_HOST"] = host
        else:
            env.pop("DEVICE_HOST", None)

        # Network interface — pass the index, not the display name
        iface = self._iface.get()
        if iface and "auto-detect" not in iface:
            # Extract index from "Name  (idx 17)"
            import re
            m = re.search(r'\(idx (\d+)\)', iface)
            if m:
                env["DEVICE_IFACE"] = m.group(1)
        else:
            env.pop("DEVICE_IFACE", None)

        return env

    def _get_selected_nodes(self) -> list[str]:
        """Return pytest node IDs for all checked leaf tests."""
        selected = []
        for item_id, node_id in self._test_nodes.items():
            if self._checked.get(item_id, False):
                selected.append(node_id)
        return selected

    def _run_tests(self):
        if self._running:
            return

        selected = self._get_selected_nodes()
        if not selected:
            self._append_output("No tests selected.\n", "heading")
            return

        self._running = True
        self._run_btn.config(state=tk.DISABLED)
        self._stop_btn.config(state=tk.NORMAL)
        self._total_selected = len(selected)
        self._counts = {"passed": 0, "failed": 0, "skipped": 0, "error": 0}
        self._progress["value"] = 0
        self._progress["maximum"] = self._total_selected

        # Clear previous results
        for item_id in self._test_nodes:
            self.tree.item(item_id, values=("",), tags=())
        # Clear parent tags too
        for item_id in self._item_names:
            if item_id not in self._test_nodes:
                self.tree.item(item_id, tags=())

        self._clear_output()
        self._append_output(
            f"Running {len(selected)} tests...\n\n", "heading")

        env = self._get_env()

        cmd = [
            sys.executable, "-m", "pytest",
            "-v", "--tb=short", "--no-header",
        ] + selected

        def _run():
            try:
                startupinfo = None
                creationflags = 0
                if sys.platform == "win32":
                    startupinfo = subprocess.STARTUPINFO()
                    startupinfo.dwFlags |= subprocess.STARTF_USESHOWWINDOW
                    creationflags = subprocess.CREATE_NO_WINDOW

                self._process = subprocess.Popen(
                    cmd,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    text=True,
                    cwd=str(STACK_ROOT),
                    env=env,
                    startupinfo=startupinfo,
                    creationflags=creationflags,
                )

                for line in iter(self._process.stdout.readline, ""):
                    self._output_queue.put(("line", line))

                self._process.wait()
                self._output_queue.put(("done", self._process.returncode))
            except Exception as e:
                self._output_queue.put(("error", str(e)))

        threading.Thread(target=_run, daemon=True).start()
        self._poll_output()

    def _stop_tests(self):
        if self._process:
            try:
                self._process.terminate()
            except OSError:
                pass
            self._append_output("\n\nTest run stopped by user.\n", "heading")

    def _poll_output(self):
        """Process output queue and update UI."""
        batch = 0
        while batch < 50:  # process up to 50 items per tick
            try:
                msg_type, data = self._output_queue.get_nowait()
                batch += 1
            except queue.Empty:
                break

            if msg_type == "line":
                self._process_output_line(data)
            elif msg_type == "done":
                self._on_run_complete(data)
                return
            elif msg_type == "error":
                self._append_output(f"\nERROR: {data}\n", "error_line")
                self._on_run_complete(-1)
                return

        if self._running:
            self.root.after(50, self._poll_output)

    def _process_output_line(self, line: str):
        """Parse a pytest output line, update tree and output pane."""
        # --- Filter server debug output ---
        if _SERVER_LINE_RE.match(line):
            if self._show_server.get():
                self._append_output(line, "server_line")
            return

        # --- Full result line: test_path STATUS [ N%] ---
        match = _RESULT_RE.search(line)
        if match:
            self._record_result(
                match.group(1).replace("\\", "/"),
                match.group(2),
                int(match.group(3)),
                line)
            self._pending_test_node = None
            return

        # --- Bare status line (server output split it from test name) ---
        bare = _BARE_STATUS_RE.match(line)
        if bare and self._pending_test_node:
            self._record_result(
                self._pending_test_node,
                bare.group(1),
                int(bare.group(2)),
                line)
            self._pending_test_node = None
            return

        # --- Test name line (status will come on a later line) ---
        start = _TEST_START_RE.search(line)
        if start:
            node = start.group(1).replace("\\", "/")
            if node in self._node_to_item:
                self._pending_test_node = node
                # Mark as running in the tree
                item_id = self._node_to_item[node]
                self.tree.item(item_id, values=("▶ running",),
                               tags=("running",))
                self.tree.see(item_id)
                self._append_output(line, None)
                return

        # --- Other output (tracebacks, fixture output, etc.) ---
        tag = None
        stripped = line.strip()
        if stripped.startswith("FAILED") or "Error" in stripped:
            tag = "fail_line"
        elif stripped.startswith("E "):
            tag = "fail_line"
        elif stripped.startswith("SHORT TEST SUMMARY") or stripped.startswith("="):
            tag = "heading"
        self._append_output(line, tag)

    def _record_result(self, node_raw: str, status: str, pct: int,
                       line: str):
        """Record a test result in the tree and output pane."""
        item_id = self._node_to_item.get(node_raw)
        if item_id:
            tag, sym = {
                "PASSED": ("passed", SYM_PASS),
                "FAILED": ("failed", SYM_FAIL),
                "SKIPPED": ("skipped", SYM_SKIP),
                "ERROR": ("error", SYM_ERR),
                "XFAIL": ("skipped", SYM_SKIP),
                "XPASS": ("passed", SYM_PASS),
            }.get(status, ("", ""))

            self.tree.item(item_id, values=(f"{sym} {status}",),
                           tags=(tag,))
            self.tree.see(item_id)
            self._update_parent_status(item_id)

        # Update counts
        key = status.lower()
        if key in ("xfail", "xpass"):
            key = "skipped" if key == "xfail" else "passed"
        if key in self._counts:
            self._counts[key] += 1

        done = sum(self._counts.values())
        self._progress["value"] = done
        self._update_summary()

        # Colorize line
        line_tag = {"PASSED": "pass_line", "FAILED": "fail_line",
                    "SKIPPED": "skip_line", "ERROR": "error_line"
                    }.get(status, None)
        self._append_output(line, line_tag)

    def _update_parent_status(self, item_id):
        """Propagate worst status to parent nodes."""
        parent = self.tree.parent(item_id)
        if not parent:
            return

        children = self.tree.get_children(parent)
        statuses = set()
        for child in children:
            vals = self.tree.item(child, "values")
            if vals and vals[0]:
                statuses.add(vals[0].split()[-1] if vals[0] else "")
            child_tags = self.tree.item(child, "tags")
            if child_tags:
                statuses.update(child_tags)

        # Priority: failed > error > skipped > passed
        if "failed" in statuses or "FAILED" in statuses:
            self.tree.item(parent, tags=("failed",))
        elif "error" in statuses or "ERROR" in statuses:
            self.tree.item(parent, tags=("error",))
        elif "skipped" in statuses or "SKIPPED" in statuses:
            if "passed" in statuses or "PASSED" in statuses:
                self.tree.item(parent, tags=("passed",))
            else:
                self.tree.item(parent, tags=("skipped",))
        elif "passed" in statuses or "PASSED" in statuses:
            self.tree.item(parent, tags=("passed",))

        self._update_parent_status(parent)

    def _on_run_complete(self, returncode: int):
        self._running = False
        self._process = None
        self._run_btn.config(state=tk.NORMAL)
        self._stop_btn.config(state=tk.DISABLED)
        self._update_summary()
        status = "PASSED" if returncode == 0 else "FINISHED"
        self._append_output(f"\n--- {status} (exit code {returncode}) ---\n",
                            "heading")

    # ==================================================================
    # Output pane
    # ==================================================================

    def _append_output(self, text: str, tag: str = None):
        self.output.config(state=tk.NORMAL)
        if tag:
            self.output.insert(tk.END, text, tag)
        else:
            self.output.insert(tk.END, text)
        self.output.see(tk.END)
        self.output.config(state=tk.DISABLED)

    def _clear_output(self):
        self.output.config(state=tk.NORMAL)
        self.output.delete("1.0", tk.END)
        self.output.config(state=tk.DISABLED)

    # ==================================================================
    # Summary
    # ==================================================================

    def _update_summary(self):
        selected = len(self._get_selected_nodes())
        p, f, s, e = (self._counts["passed"], self._counts["failed"],
                       self._counts["skipped"], self._counts["error"])
        total_done = p + f + s + e

        parts = []
        if total_done > 0:
            if p: parts.append(f"{SYM_PASS} {p} passed")
            if f: parts.append(f"{SYM_FAIL} {f} failed")
            if s: parts.append(f"{SYM_SKIP} {s} skipped")
            if e: parts.append(f"{SYM_ERR} {e} errors")
            parts.append(f"({total_done}/{self._total_selected})")
        else:
            parts.append(f"{selected} selected")

        self._summary_var.set("   ".join(parts))


def main():
    root = tk.Tk()

    # Use system theme
    style = ttk.Style()
    if sys.platform == "win32":
        try:
            style.theme_use("vista")
        except tk.TclError:
            style.theme_use("clam")
    elif sys.platform == "darwin":
        style.theme_use("aqua")
    else:
        style.theme_use("clam")

    app = TestRunnerApp(root)
    root.mainloop()


if __name__ == "__main__":
    main()
