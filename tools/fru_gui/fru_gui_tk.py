#!/usr/bin/env python3
"""
fru_gui_tk.py -- uFC Toolbox: FRU / I2C / PMBus desktop tool (Tkinter).

Reads/writes the FRU EEPROM through a NAT-MCH (IPMI raw commands 0x32/0x12,
0x13), edits the board/product fields, saves or loads the raw image.
"""

import argparse
import datetime
import os
import queue
import re
import subprocess
import sys
import threading
import time
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fru_core import (FruAccess, parse_fru, build_image, image_for_eeprom,  # noqa: E402
                      EEPROM_SIZE, FRU_MAX_SIZE, FRU_MAX_STRING,
                      mfg_minutes_to_str, mfg_str_to_minutes)

APP_TITLE = "uFC Toolbox"

# ---- house palette --------------------------------------------------------
BG = "#FAF9F5"          # paper ground
PANEL = "#FFFFFF"       # panels and inputs
INK = "#211E19"         # primary text
MUTED = "#726B5F"       # secondary text
LINE = "#E4DED2"        # hairlines
ACCENT = "#0E6B5E"      # the one hue (FRU / board green-teal)
ACCENT_HOVER = "#0A544A"
DANGER = "#A6372B"
OK = "#0E6B5E"

UI_FONT = "Noto Sans"
MONO_FONT = "DejaVu Sans Mono"

FIELD_LABELS = {
    "manufacturer": "Manufacturer",
    "product_name": "Board / product name",
    "serial": "Serial number",
    "part_number": "Part number",
    "version": "Part version",
    "asset_tag": "Asset tag",
    "fru_file_id": "FRU file ID",
}


class Tooltip:
    """Small hover hint in the app's ink-on-paper language."""

    def __init__(self, widget, text):
        self.widget = widget
        self.text = text
        self.tip = None
        widget.bind("<Enter>", self._show, add="+")
        widget.bind("<Leave>", self._hide, add="+")

    def _show(self, _event=None):
        if self.tip or not self.text:
            return
        x = self.widget.winfo_rootx() + 8
        y = self.widget.winfo_rooty() + self.widget.winfo_height() + 6
        self.tip = tk.Toplevel(self.widget)
        self.tip.wm_overrideredirect(True)
        self.tip.wm_geometry("+%d+%d" % (x, y))
        tk.Label(self.tip, text=self.text, bg="#2B2823", fg="#FFFFFF",
                 font=(UI_FONT, 8), padx=8, pady=4, justify="left",
                 relief="flat").pack()

    def _hide(self, _event=None):
        if self.tip:
            self.tip.destroy()
            self.tip = None


class FruEditor(tk.Tk):
    def __init__(self, mch, target, bus, transit, eeprom_size=256, fru_limit=128):
        super().__init__()
        self.title(APP_TITLE)
        self.geometry("1040x980")
        self.minsize(940, 740)
        self.configure(bg=BG)

        self.raw = None
        self.parsed = None
        self.entries = {"board": {}, "product": {}}
        self._style()

        self._build_ui(mch, target, bus, transit, eeprom_size, fru_limit)
        for var in (self.mch_var, self.transit_var, self.tch_var, self.target_var,
                    self.bus_var):
            var.trace_add("write", lambda *_: self._update_path())
        self._update_path()
        self._update_address()
        self._on_bridge_change()
        self.size_var.trace_add("write", lambda *_: self._update_address())
        self.i2c_var.trace_add("write", lambda *_: self._update_address())
        self._status("Ready — firmware must be built with -DEEPROM_ACCESS=ON", "idle")

    # ---------------------------------------------------------------- style
    def _style(self):
        st = ttk.Style(self)
        st.theme_use("clam")
        st.configure(".", background=BG, foreground=INK, font=(UI_FONT, 10))
        st.configure("TFrame", background=BG)
        st.configure("Panel.TFrame", background=PANEL)
        st.configure("TLabel", background=BG, foreground=INK)
        st.configure("Muted.TLabel", background=BG, foreground=MUTED, font=(UI_FONT, 9))
        st.configure("FieldLabel.TLabel", background=BG, foreground=MUTED,
                     font=(UI_FONT, 9), anchor="e")
        st.configure("Hint.TLabel", background=BG, foreground=MUTED, font=(UI_FONT, 8))
        st.configure("Title.TLabel", background=BG, foreground=INK, font=(UI_FONT, 15, "bold"))
        st.configure("Sub.TLabel", background=BG, foreground=MUTED, font=(UI_FONT, 9))
        st.configure("Section.TLabelframe", background=PANEL, bordercolor=LINE,
                     relief="solid", borderwidth=1)
        st.configure("Section.TLabelframe.Label", background=PANEL, foreground=MUTED,
                     font=(UI_FONT, 9, "bold"))
        st.configure("TEntry", fieldbackground=PANEL, foreground=INK, bordercolor=LINE,
                     lightcolor=LINE, darkcolor=LINE, insertcolor=INK, padding=(6, 2))
        st.configure("TButton", background=PANEL, foreground=INK, bordercolor=LINE,
                     lightcolor=LINE, darkcolor=LINE, focuscolor=BG, padding=(12, 6),
                     font=(UI_FONT, 9))
        st.map("TButton", background=[("active", "#F3F0E9"), ("pressed", "#EDE9DF")])
        st.configure("Primary.TButton", background=ACCENT, foreground="#FFFFFF",
                     bordercolor=ACCENT, lightcolor=ACCENT, darkcolor=ACCENT,
                     padding=(14, 6), font=(UI_FONT, 9, "bold"))
        st.map("Primary.TButton",
               background=[("active", ACCENT_HOVER), ("pressed", ACCENT_HOVER)],
               foreground=[("disabled", "#D8E6E2")])
        st.configure("TNotebook", background=BG, borderwidth=0, tabmargins=(0, 4, 0, 0))
        st.configure("TNotebook.Tab", background=BG, foreground=MUTED,
                     padding=(14, 6), borderwidth=0)
        st.map("TNotebook.Tab",
               background=[("selected", PANEL)],
               foreground=[("selected", INK)])
        st.configure("TSeparator", background=LINE)

    # ------------------------------------------------------------------- ui
    def _build_ui(self, mch, target, bus, transit, eeprom_size, fru_limit):
        header = ttk.Frame(self, padding=(16, 12, 16, 10))
        header.pack(fill="x")

        mark = tk.Canvas(header, width=30, height=30, bg=BG, highlightthickness=0)
        mark.pack(side="left")
        mark.create_rectangle(6, 4, 24, 26, fill=ACCENT, outline="")
        for y in (9, 15, 21):
            mark.create_line(9, y, 21, y, fill=BG, width=1)
        for x in (6, 24):
            for y in (8, 12, 16, 20):
                mark.create_line(x - (3 if x == 6 else -3), y,
                                 x, y, fill=ACCENT, width=2)

        title = ttk.Frame(header)
        title.pack(side="left", padx=(10, 0))
        ttk.Label(title, text=APP_TITLE, style="Title.TLabel").pack(anchor="w")
        ttk.Label(title, text="FRU  ·  I\u00b2C  ·  PMBus  over IPMI",
                  style="Sub.TLabel").pack(anchor="w")

        ttk.Separator(self).pack(fill="x", padx=16)

        # connection / size strip
        strip = ttk.Frame(self, padding=(16, 10, 16, 6))
        strip.pack(fill="x")
        self.mch_var, self.mch_w = self._labeled(
            strip, "MCH / BMC", mch, 14,
            tip="IPMI-over-LAN address of the first controller\n(the crate manager or a BMC).")

        bbox = ttk.Frame(strip)
        bbox.pack(side="left", padx=(0, 14))
        blab = ttk.Label(bbox, text="Bridge", style="FieldLabel.TLabel")
        blab.pack(anchor="w")
        self.bridge_var = tk.StringVar(value="Double bridge")
        self.bridge_box = ttk.Combobox(bbox, textvariable=self.bridge_var, width=13,
                                       state="readonly",
                                       values=("Double bridge", "Single bridge", "Direct"))
        self.bridge_box.pack(anchor="w")
        self.bridge_box.bind("<<ComboboxSelected>>", lambda _e: self._on_bridge_change())
        Tooltip(blab, "How many IPMI hops the request needs:\n"
                      "double = MCH -> transit -> target, single = MCH -> target,\n"
                      "direct = talk to the MCH/BMC itself.")
        Tooltip(self.bridge_box, "double: transit 0x82 -> target 0x7E\n"
                                 "single: target on a bus of the MCH\n"
                                 "direct: no bridging (the MCH answers)")
        self.transit_var, self.transit_w = self._labeled(
            strip, "Transit", transit, 6,
            tip="First bridge hop: the intermediate IPMI controller\nthat owns the target's bus.")
        self.tch_var, self.tch_w = self._labeled(
            strip, "Transit ch", "0", 4,
            tip="Channel from the first controller to the transit (usually 0).")
        self.target_var, self.target_w = self._labeled(
            strip, "Target", target, 6,
            tip="IPMB address of the device that answers (the module's MMC).")
        self.bus_var, self.bus_w = self._labeled(
            strip, "Bus", str(bus), 4,
            tip="IPMB channel that reaches the target\n(per-slot on a MicroTCA crate).")
        self.i2c_var, self.i2c_w = self._labeled(
            strip, "EEPROM I\u00b2C", "0x50", 6,
            tip="7-bit I2C address of the EEPROM on the FRU bus.\n"
                "0x50 is the board FRU part; other addresses use the\n"
                "address-parameterised firmware commands (0x14/0x15).")
        self.size_var, self.size_w = self._labeled(
            strip, "EEPROM size (B)", str(eeprom_size), 7,
            tip="Bytes read from the EEPROM. The device sits at I2C 0x50 on\nthe FRU bus (fixed in firmware); the tool addresses it by\nbyte offset (16-bit, so 65536 bytes is the maximum).")
        self.limit_var, self.limit_w = self._labeled(
            strip, "FRU writable (B)", str(fru_limit), 7,
            tip="Bytes of the FRU image that are written.\nThe 24AA025E48 only allows the lower 128 bytes.")

        info = ttk.Frame(self, padding=(16, 2, 16, 4))
        info.pack(fill="x")
        self.path_var = tk.StringVar()
        self.fruinfo_var = tk.StringVar(value="no FRU read yet")
        tk.Label(info, textvariable=self.path_var, bg=BG, fg=MUTED,
                 font=(UI_FONT, 8), anchor="w").pack(fill="x")
        tk.Label(info, textvariable=self.fruinfo_var, bg=BG, fg=MUTED,
                 font=(UI_FONT, 8), anchor="w").pack(fill="x")
        self.addr_var = tk.StringVar()
        tk.Label(info, textvariable=self.addr_var, bg=BG, fg=MUTED,
                 font=(UI_FONT, 8), anchor="w").pack(fill="x")

        # notebook
        self.nb = ttk.Notebook(self)
        self.nb.pack(fill="both", expand=True, padx=16, pady=(8, 8))

        self.fields_tab = ttk.Frame(self.nb)
        self.raw_tab = ttk.Frame(self.nb, padding=12)
        self.nb.add(self.fields_tab, text="FRU fields")

        toolbar = ttk.Frame(self.fields_tab, padding=(12, 8, 12, 0))
        toolbar.pack(fill="x")
        ttk.Button(toolbar, text="Read FRU", style="Primary.TButton",
                   command=self.read_fru).pack(side="left")
        ttk.Button(toolbar, text="Write FRU",
                   command=self.write_fru).pack(side="left", padx=(8, 0))
        ttk.Button(toolbar, text="Save image…",
                   command=self.save_image).pack(side="left", padx=(16, 0))
        ttk.Button(toolbar, text="Load image to EEPROM",
                   command=self.load_image).pack(side="left", padx=(8, 0))
        self.nb.add(self.raw_tab, text="Raw EEPROM")

        canvas = tk.Canvas(self.fields_tab, bg=BG, highlightthickness=0)
        vsb = ttk.Scrollbar(self.fields_tab, orient="vertical", command=canvas.yview)
        canvas.configure(yscrollcommand=vsb.set)
        canvas.pack(side="left", fill="both", expand=True)
        vsb.pack(side="right", fill="y")
        inner = ttk.Frame(canvas, padding=12)
        window = canvas.create_window((0, 0), window=inner, anchor="nw")
        inner.bind("<Configure>", lambda e: canvas.configure(scrollregion=canvas.bbox("all")))
        canvas.bind("<Configure>", lambda e: canvas.itemconfigure(window, width=e.width))

        board_frame = ttk.LabelFrame(inner, text="BOARD INFO AREA",
                                     style="Section.TLabelframe", padding=(12, 6))
        board_frame.pack(fill="x", pady=(0, 10))
        ttk.Label(board_frame, text="Strings up to 62 characters  ·  time in UTC",
                  style="Hint.TLabel").grid(row=0, column=0, columnspan=3, sticky="w",
                                            pady=(0, 4))
        self._build_area(board_frame, "board", with_time=True, start_row=1)

        product_frame = ttk.LabelFrame(inner, text="PRODUCT INFO AREA",
                                       style="Section.TLabelframe", padding=(12, 6))
        product_frame.pack(fill="x")
        ttk.Label(product_frame, text="Strings up to 62 characters",
                  style="Hint.TLabel").grid(row=0, column=0, columnspan=3, sticky="w",
                                            pady=(0, 4))
        self._build_area(product_frame, "product", with_time=False, start_row=1)

        self.i2c_tab = ttk.Frame(self.nb, padding=12)
        self.nb.add(self.i2c_tab, text="I²C transfer")
        self._build_i2c_tab(self.i2c_tab)

        self.ota_tab = ttk.Frame(self.nb, padding=12)
        self.nb.add(self.ota_tab, text="OTA update")
        self._build_ota_tab(self.ota_tab)

        self.hex_text = tk.Text(self.raw_tab, font=(MONO_FONT, 10), state="disabled",
                                height=18, width=64, bg=PANEL, fg=INK,
                                relief="flat", highlightthickness=1,
                                highlightbackground=LINE, insertbackground=INK,
                                padx=10, pady=8)
        self.hex_text.pack(fill="both", expand=True)

        # log + status
        logf = ttk.Frame(self, padding=(16, 2, 16, 4))
        logf.pack(fill="x")
        ttk.Label(logf, text="LOG", style="Hint.TLabel").pack(anchor="w")
        self.log_text = tk.Text(logf, height=2, font=(MONO_FONT, 8), state="disabled",
                                bg=PANEL, fg=MUTED, relief="flat",
                                highlightthickness=1, highlightbackground=LINE,
                                padx=8, pady=4)
        self.log_text.pack(fill="x")

        status = ttk.Frame(self, style="Panel.TFrame", padding=(16, 6))
        status.pack(fill="x", side="bottom")
        self.dot = tk.Label(status, text="●", fg=MUTED, bg=PANEL, font=(UI_FONT, 9))
        self.dot.pack(side="left", padx=(0, 6))
        self.status = tk.StringVar(value="")
        tk.Label(status, textvariable=self.status, bg=PANEL, fg=INK, anchor="w",
                 font=(UI_FONT, 9)).pack(side="left", fill="x", expand=True)

        bottom = ttk.Frame(self, padding=(16, 6))
        bottom.pack(fill="x")
        ttk.Button(bottom, text="Exit", command=self.destroy).pack(side="right")

    def _build_ota_tab(self, parent):
        self.ota_proc = None
        self.ota_queue = queue.Queue()
        self.ota_progress_re = re.compile(r"(\d{1,3})%")

        row = ttk.Frame(parent)
        row.pack(fill="x", pady=(0, 8))
        ttk.Label(row, text="HPM file", style="FieldLabel.TLabel").pack(side="left")
        self.ota_file = tk.StringVar(value="")
        ttk.Entry(row, textvariable=self.ota_file, width=52).pack(side="left", padx=(6, 6))
        ttk.Button(row, text="Browse…", command=self._ota_browse).pack(side="left")

        row2 = ttk.Frame(parent)
        row2.pack(fill="x", pady=(0, 8))
        ttk.Label(row2, text="Component", style="FieldLabel.TLabel").pack(side="left")
        self.ota_comp = tk.StringVar(value="1")
        ttk.Entry(row2, textvariable=self.ota_comp, width=4).pack(side="left", padx=(6, 16))
        ttk.Label(row2, text="(0 bootloader · 1 MMC · 2 payload)",
                  style="Hint.TLabel").pack(side="left")
        ttk.Button(row2, text="Check target", command=self._ota_check).pack(side="left", padx=(16, 0))

        row3 = ttk.Frame(parent)
        row3.pack(fill="x", pady=(0, 8))
        self.ota_buttons = []
        for text, cmd in (("Upload", self._ota_upload),
                          ("Activate", self._ota_activate),
                          ("Upload + Activate", self._ota_upload_activate)):
            b = ttk.Button(row3, text=text, command=cmd)
            b.pack(side="left", padx=(0, 8))
            self.ota_buttons.append(b)

        self.ota_pbar = ttk.Progressbar(parent, orient="horizontal", mode="determinate",
                                        maximum=100)
        self.ota_pbar.pack(fill="x", pady=(0, 8))

        ttk.Label(parent, text="ipmitool output", style="Hint.TLabel").pack(anchor="w")
        self.ota_out = tk.Text(parent, height=14, font=(MONO_FONT, 8), state="disabled",
                               bg=PANEL, fg=INK, relief="flat", highlightthickness=1,
                               highlightbackground=LINE, padx=8, pady=6)
        self.ota_out.pack(fill="both", expand=True)

    def _ota_browse(self):
        path = filedialog.askopenfilename(filetypes=[("HPM image", "*.hpm"), ("All files", "*.*")])
        if path:
            self.ota_file.set(path)

    def _ota_log(self, text):
        self.ota_out.configure(state="normal")
        self.ota_out.insert("end", text)
        self.ota_out.see("end")
        self.ota_out.configure(state="disabled")

    def _ota_busy(self, busy):
        for b in self.ota_buttons:
            b.configure(state="disabled" if busy else "normal")

    def _ota_run(self, args, next_args=None, timeout=None):
        if self.ota_proc is not None:
            return
        self.ota_next = next_args
        self.ota_deadline = (time.time() + timeout) if timeout else None
        self.ota_forced = False
        try:
            acc = self._access()
        except Exception as e:
            messagebox.showerror(APP_TITLE, str(e))
            return
        cmd = acc.hpm_command(args)
        self._ota_log("$ %s\n" % " ".join(cmd))
        self.ota_pbar.configure(value=0)
        self._ota_busy(True)
        try:
            self.ota_proc = subprocess.Popen(
                cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT, text=True, bufsize=1)
            try:
                self.ota_proc.stdin.write("y\n")
                self.ota_proc.stdin.flush()
            except Exception:
                pass
        except Exception as e:
            self.ota_proc = None
            self._ota_busy(False)
            messagebox.showerror(APP_TITLE, "cannot run ipmitool:\n%s" % e)
            return
        threading.Thread(target=self._ota_reader, daemon=True).start()
        self.after(150, self._ota_poll)

    def _ota_reader(self):
        proc = self.ota_proc
        for line in proc.stdout:
            self.ota_queue.put(("line", line))
        code = proc.wait()
        self.ota_queue.put(("done", code))

    def _ota_poll(self):
        done = False
        code = None
        if (self.ota_proc is not None and self.ota_deadline is not None
                and time.time() > self.ota_deadline):
            self._ota_log("\n(timeout waiting for the reply — the MMC is probably resetting)\n")
            try:
                self.ota_proc.terminate()
            except Exception:
                pass
            self.ota_forced = True
            self.ota_deadline = None
        while True:
            try:
                kind, value = self.ota_queue.get_nowait()
            except queue.Empty:
                break
            if kind == "line":
                self._ota_log(value)
                for m in self.ota_progress_re.finditer(value):
                    try:
                        pct = int(m.group(1))
                    except ValueError:
                        continue
                    if 0 <= pct <= 100:
                        self.ota_pbar.configure(value=pct)
            else:
                done = True
                code = value
        if done:
            forced = getattr(self, "ota_forced", False)
            self.ota_proc = None
            self._ota_busy(False)
            self.ota_pbar.configure(value=100 if (code == 0 or forced) else self.ota_pbar.cget("value"))
            if forced:
                self._status("Activate sent — MMC resetting, read again in a few seconds", "ok")
            else:
                self._status("OTA command %s" % ("finished" if code == 0 else "failed"),
                             "ok" if code == 0 else "error")
            nxt = getattr(self, "ota_next", None)
            self.ota_next = None
            if nxt and code == 0:
                self.after(600, lambda: self._ota_run(nxt))
            return
        self.after(150, self._ota_poll)

    def _ota_check(self):
        self._ota_run(["check"])

    def _ota_upload(self):
        path = self.ota_file.get().strip()
        if not path or not os.path.isfile(path):
            messagebox.showwarning(APP_TITLE, "Choose an .hpm file first.")
            return
        self._ota_run(["upgrade", path, "-C", self.ota_comp.get().strip()])

    def _ota_activate(self):
        if not messagebox.askyesno(APP_TITLE,
                                    "Activate the staged image? The MMC will reset."):
            return
        self._ota_run(["activate"], timeout=25)

    def _ota_upload_activate(self):
        path = self.ota_file.get().strip()
        if not path or not os.path.isfile(path):
            messagebox.showwarning(APP_TITLE, "Choose an .hpm file first.")
            return
        if not messagebox.askyesno(APP_TITLE,
                                    "Upload and activate %s?\nThe MMC will reset after the upload."
                                    % os.path.basename(path)):
            return
        self._ota_run(["upgrade", path, "-C", self.ota_comp.get().strip()],
                      next_args=["activate"])

    def _build_i2c_tab(self, parent):
        row = ttk.Frame(parent)
        row.pack(fill="x", pady=(0, 8))
        ttk.Label(row, text="Bus", style="FieldLabel.TLabel").pack(side="left")
        self.i2c_bus = tk.StringVar(value="I2C2")
        ttk.Combobox(row, textvariable=self.i2c_bus, width=6, state="readonly",
                     values=("I2C2", "I2C1")).pack(side="left", padx=(6, 16))

        ttk.Label(row, text="Device", style="FieldLabel.TLabel").pack(side="left")
        self.i2c_dev = tk.StringVar(value="0x50")
        ttk.Entry(row, textvariable=self.i2c_dev, width=7).pack(side="left", padx=(6, 16))

        ttk.Label(row, text="Write len", style="FieldLabel.TLabel").pack(side="left")
        self.i2c_wlen = tk.StringVar(value="1")
        ttk.Entry(row, textvariable=self.i2c_wlen, width=4).pack(side="left", padx=(6, 16))

        ttk.Label(row, text="Read len", style="FieldLabel.TLabel").pack(side="left")
        self.i2c_rlen = tk.StringVar(value="8")
        ttk.Entry(row, textvariable=self.i2c_rlen, width=4).pack(side="left", padx=(6, 16))

        ttk.Button(row, text="Transfer", style="Primary.TButton",
                   command=self.i2c_transfer).pack(side="left")

        datarow = ttk.Frame(parent)
        datarow.pack(fill="x", pady=(0, 8))
        ttk.Label(datarow, text="Write data (hex)", style="FieldLabel.TLabel").pack(side="left")
        self.i2c_wdata = tk.StringVar(value="00")
        ttk.Entry(datarow, textvariable=self.i2c_wdata, width=40).pack(side="left", padx=(6, 12))
        ttk.Label(datarow, text="(Write len must match the number of bytes)",
                  style="Hint.TLabel").pack(side="left")

        muxrow = ttk.Frame(parent)
        muxrow.pack(fill="x", pady=(0, 8))
        ttk.Label(muxrow, text="Mux stages (address, channel; blank = unused)",
                  style="Hint.TLabel").pack(side="left", padx=(0, 10))
        self.mux_vars = []
        for i in range(3):
            ttk.Label(muxrow, text="addr", style="FieldLabel.TLabel").pack(side="left")
            a = tk.StringVar(value="0x70" if i == 0 else "")
            ttk.Entry(muxrow, textvariable=a, width=6).pack(side="left", padx=(4, 4))
            ttk.Label(muxrow, text="ch", style="FieldLabel.TLabel").pack(side="left")
            c = tk.StringVar(value="0" if i == 0 else "")
            ttk.Entry(muxrow, textvariable=c, width=4).pack(side="left", padx=(4, 14))
            self.mux_vars.append((a, c))

        ttk.Label(parent,
                  text="write len 0 = read only   ·   read len 0 = write only   ·   both >0 = write then read",
                  style="Hint.TLabel").pack(anchor="w")
        self.i2c_mode = tk.StringVar(value="")
        self.i2c_mode_lbl = tk.Label(parent, textvariable=self.i2c_mode, bg=BG, fg=ACCENT,
                                     font=(UI_FONT, 9))
        self.i2c_mode_lbl.pack(anchor="w", pady=(0, 8))
        for var in (self.i2c_wdata, self.i2c_rlen, self.i2c_wlen):
            var.trace_add("write", lambda *_: self._update_i2c_mode())
        self._update_i2c_mode()

        presets = ttk.Frame(parent)
        presets.pack(fill="x", pady=(0, 8))
        ttk.Label(presets, text="Presets", style="Hint.TLabel").pack(side="left", padx=(0, 8))
        for label, fn in (("EEPROM FRU", self._preset_eeprom),
                          ("UCD PMBus", self._preset_pmbus),
                          ("Clock 8V54816", self._preset_clock),
                          ("Si5345", self._preset_si5345)):
            ttk.Button(presets, text=label, command=fn).pack(side="left", padx=(0, 6))

        out = ttk.Frame(parent)
        out.pack(fill="both", expand=True)
        ttk.Label(out, text="Result", style="Hint.TLabel").pack(anchor="w")
        self.i2c_out = tk.Text(out, height=6, font=(MONO_FONT, 10), state="disabled",
                               bg=PANEL, fg=INK, relief="flat", highlightthickness=1,
                               highlightbackground=LINE, padx=10, pady=8)
        self.i2c_out.pack(fill="both", expand=True)

    def _update_i2c_mode(self):
        try:
            data = len(self._hex_bytes(self.i2c_wdata.get()))
            w = int(self.i2c_wlen.get().strip() or 0)
            r = int(self.i2c_rlen.get().strip() or 0)
        except Exception:
            self.i2c_mode_lbl.configure(fg=DANGER)
            self.i2c_mode.set("write ? B · read ? B")
            return
        if w != data:
            self.i2c_mode_lbl.configure(fg=DANGER)
            self.i2c_mode.set("Write len %d does not match %d byte(s) of data" % (w, data))
            return
        self.i2c_mode_lbl.configure(fg=ACCENT)
        self.i2c_mode.set("write %d B  ·  read %d B  →  %s" % (
            w, r,
            "write then read" if w and r else ("read only" if r else ("write only" if w else "nothing to do"))))

    @staticmethod
    def _hex_bytes(text):
        parts = [p for p in text.replace(",", " ").split() if p]
        out = []
        for p in parts:
            v = int(p, 16) if not p.lower().startswith("0x") else int(p, 16)
            if not 0 <= v <= 0xFF:
                raise ValueError("byte out of range: %s" % p)
            out.append(v)
        return bytes(out)

    @staticmethod
    def _hex_val(text, default=0):
        text = (text or "").strip()
        if not text:
            return default
        return int(text, 16) if text.lower().startswith("0x") else int(text, 0)

    def _i2c_params(self):
        bus = 1 if self.i2c_bus.get() == "I2C2" else 0
        muxes = []
        for a, c in self.mux_vars:
            if not a.get().strip():
                continue
            muxes.append((self._hex_val(a.get()), self._hex_val(c.get())))
        dev = self._hex_val(self.i2c_dev.get())
        wdata = self._hex_bytes(self.i2c_wdata.get())
        wlen = int(self.i2c_wlen.get().strip() or 0)
        rlen = int(self.i2c_rlen.get().strip() or 0)
        if wlen != len(wdata):
            raise ValueError("Write len is %d but the write data has %d byte(s)"
                             % (wlen, len(wdata)))
        return bus, muxes, dev, wdata, rlen

    def i2c_transfer(self):
        try:
            bus, muxes, dev, wdata, rlen = self._i2c_params()
            acc = self._access()
            data = acc.xfer(bus, muxes, dev, wdata, rlen)
            self._status("I2C transfer OK — %d byte(s) read" % len(data), "ok")
            self._i2c_show(data)
        except Exception as e:
            self._status("I2C transfer failed", "error")
            self._i2c_show(b"")
            self.log("ERROR: %s" % e)
            messagebox.showerror(APP_TITLE, "I2C transfer failed:\n%s" % e)

    def _i2c_show(self, data):
        self.i2c_out.configure(state="normal")
        self.i2c_out.delete("1.0", "end")
        if data:
            self.i2c_out.insert("end", " ".join("%02x" % b for b in data) + "\n")
            self.i2c_out.insert("end", "as bytes: %s" % " ".join(str(b) for b in data))
        else:
            self.i2c_out.insert("end", "(no data)")
        self.i2c_out.configure(state="disabled")

    def _preset(self, bus, muxes, dev, wdata, rlen):
        self.i2c_bus.set("I2C2" if bus == 1 else "I2C1")
        for i, (a, c) in enumerate(self.mux_vars):
            if i < len(muxes):
                a.set("0x%02x" % muxes[i][0]); c.set(str(muxes[i][1]))
            else:
                a.set(""); c.set("")
        self.i2c_dev.set("0x%02x" % dev)
        self.i2c_wdata.set(" ".join("%02x" % b for b in wdata))
        self.i2c_wlen.set(str(len(wdata)))
        self.i2c_rlen.set(str(rlen))

    def _preset_eeprom(self):
        self._preset(1, [(0x70, 0)], 0x50, b"\x00", 8)

    def _preset_pmbus(self):
        self._preset(1, [(0x70, 0)], 0x11, b"\x8b", 2)

    def _preset_clock(self):
        self._preset(1, [(0x70, 2)], 0x58, b"\x00", 1)

    def _preset_si5345(self):
        self._preset(1, [(0x70, 2)], 0x69, b"\x00", 1)

    def _labeled(self, parent, text, value, width, tip=None):
        box = ttk.Frame(parent)
        box.pack(side="left", padx=(0, 14))
        lab = ttk.Label(box, text=text, style="FieldLabel.TLabel")
        lab.pack(anchor="w")
        var = tk.StringVar(value=value)
        ent = ttk.Entry(box, textvariable=var, width=width)
        ent.pack(anchor="w")
        if tip:
            Tooltip(lab, tip)
            Tooltip(ent, tip)
        return var, ent

    def _build_area(self, parent, key, with_time, start_row=0):
        row = start_row
        if with_time:
            cell = ttk.Frame(parent)
            cell.grid(row=row, column=1, sticky="w", pady=1)
            self.time_var = tk.StringVar()
            ttk.Entry(cell, textvariable=self.time_var, width=20).pack(side="left")
            ttk.Button(cell, text="Now", width=4,
                       command=self._set_time_now).pack(side="left", padx=(6, 0))
            ttk.Label(cell, text="YYYY-MM-DD HH:MM", style="Hint.TLabel").pack(
                side="left", padx=(8, 0))
            row += 1
        for name in ("manufacturer", "product_name", "serial", "part_number",
                     "version", "asset_tag", "fru_file_id"):
            if key == "board" and name in ("version", "asset_tag"):
                continue
            ttk.Label(parent, text=FIELD_LABELS[name], style="FieldLabel.TLabel").grid(
                row=row, column=0, sticky="e", padx=(0, 10), pady=1)
            var = tk.StringVar()
            ent = ttk.Entry(parent, textvariable=var, width=63)
            ent.grid(row=row, column=1, sticky="w", pady=1)
            hint = ttk.Label(parent, text="", style="Hint.TLabel")
            hint.grid(row=row, column=2, sticky="w", padx=(8, 0))
            self.entries[key][name] = (var, ent, hint)
            self._auto_width(ent, var)
            row += 1

    @staticmethod
    def _auto_width(ent, var):
        def update(*_args):
            n = len(var.get())
            ent.configure(width=max(FRU_MAX_STRING + 1, n + 1))
        var.trace_add("write", update)
        update()

    # ------------------------------------------------------------- helpers
    def _i2c_addr(self):
        text = self.i2c_var.get().strip().lower()
        try:
            addr = int(text, 16) if text.startswith("0x") else int(text, 0)
        except ValueError:
            raise ValueError("EEPROM I2C address must be 0x08..0x77")
        if not 0x08 <= addr <= 0x77:
            raise ValueError("EEPROM I2C address must be 0x08..0x77")
        return addr

    def _update_address(self):
        try:
            size = int(self.size_var.get().strip())
        except ValueError:
            size = 0
        try:
            addr = self.i2c_var.get().strip()
        except Exception:
            addr = "0x50"
        self.addr_var.set(
            "EEPROM at I2C %s  ·  byte offsets 0x0000–0x%04X"
            % (addr, max(0, size - 1)))

    def _bridge_mode(self):
        return {"Double bridge": "double", "Single bridge": "single",
                "Direct": "direct"}.get(self.bridge_var.get(), "double")

    def _on_bridge_change(self):
        mode = self._bridge_mode()
        state = "disabled"
        for w, needed in ((self.transit_w, mode == "double"),
                          (self.tch_w, mode == "double"),
                          (self.target_w, mode != "direct"),
                          (self.bus_w, mode != "direct")):
            w.configure(state="normal" if needed else state)
        self._update_path()

    def _update_path(self):
        try:
            mode = self._bridge_mode()
            if mode == "direct":
                text = "LAN → %s  (direct, no bridge)" % self.mch_var.get()
            elif mode == "single":
                text = "LAN → %s → target %s (bus %s)" % (
                    self.mch_var.get(), self.target_var.get(), self.bus_var.get())
            else:
                text = "LAN → %s → transit %s (ch %s) → target %s (bus %s)" % (
                    self.mch_var.get(), self.transit_var.get(), self.tch_var.get(),
                    self.target_var.get(), self.bus_var.get())
            self.path_var.set(text)
        except Exception:
            pass

    def _fru_stats(self, parsed, limit):
        raw = parsed["raw"]
        used = parsed["image_len"]
        multi = len(parsed["multi"])
        ok = (sum(raw[:8]) & 0xFF) == 0
        for key in ("board_area", "product_area"):
            a = parsed[key]
            if a:
                ok = ok and (sum(raw[a["offset"]:a["offset"] + a["length"]]) & 0xFF) == 0
        mfg = parsed["board"].get("mfg_time", 0)
        return "%d B used · %d B free · multirecord %d B · checksums %s · mfg %s" % (
            used, limit - used, multi, "OK" if ok else "BAD",
            mfg_minutes_to_str(mfg))

    def _set_time_now(self):
        now = datetime.datetime.now(datetime.timezone.utc)
        self.time_var.set(now.strftime("%Y-%m-%d %H:%M"))

    def _status(self, text, kind="idle"):
        self.status.set(text)
        self.dot.configure(fg={"ok": OK, "error": DANGER, "idle": MUTED}.get(kind, MUTED))

    def log(self, text):
        self.log_text.configure(state="normal")
        self.log_text.insert("end", text + "\n")
        self.log_text.see("end")
        self.log_text.configure(state="disabled")

    def _sizes(self):
        try:
            size = int(self.size_var.get().strip())
            limit = int(self.limit_var.get().strip())
        except ValueError:
            raise ValueError("EEPROM size and writable limit must be integers")
        if not 1 <= size <= 0x10000:
            raise ValueError("EEPROM size must be 1..65536 bytes")
        if not 1 <= limit <= size:
            raise ValueError("FRU writable limit must be 1..EEPROM size")
        return size, limit

    def _access(self):
        size, _limit = self._sizes()
        return FruAccess(self.mch_var.get().strip(), self.target_var.get().strip(),
                         self.transit_var.get().strip(), self.bus_var.get().strip(),
                         eeprom_size=size, transit_ch=int(self.tch_var.get().strip() or 0),
                         bridge=self._bridge_mode(), i2c_addr=self._i2c_addr())

    def _fill(self, raw, parsed):
        self.raw = raw
        self.parsed = parsed
        minutes = parsed["board"].get("mfg_time", 0)
        self.time_var.set(mfg_minutes_to_str(minutes))
        for key, area_key in (("board", "board_area"), ("product", "product_area")):
            info = parsed[area_key]
            present = {f["name"]: f for f in info["fields"]} if info else {}
            for name, (var, ent, hint) in self.entries[key].items():
                f = present.get(name)
                if f is None:
                    var.set("")
                    ent.configure(state="disabled")
                    hint.configure(text="n/a")
                else:
                    var.set(f["value"])
                    ent.configure(state="normal")
                    hint.configure(text="")
        self.hex_text.configure(state="normal")
        self.hex_text.delete("1.0", "end")
        for i in range(0, len(raw), 16):
            self.hex_text.insert("end", "%04x  %s\n" % (
                i, " ".join("%02x" % b for b in raw[i:i + 16])))
        self.hex_text.configure(state="disabled")

    # -------------------------------------------------------------- actions
    def read_fru(self):
        try:
            _size, limit = self._sizes()
            acc = self._access()
            raw = acc.dump()
            parsed = parse_fru(raw, limit=limit)
            self._fill(raw, parsed)
            self._update_path()
            info = self._fru_stats(parsed, limit)
            try:
                d = acc.device_id()
                if len(d) >= 5:
                    info += "   ·   MMC 0x%02x  fw %d.%02d  IPMI %d.%d" % (
                        d[0], d[2] & 0x7F, d[3], d[4] & 0x0F, (d[4] >> 4) & 0x0F)
            except Exception:
                pass
            self.fruinfo_var.set(info)
            self._status("FRU read — %d bytes, board and product areas parsed" % len(raw), "ok")
            self.log("Read %d bytes from the FRU EEPROM." % len(raw))
        except Exception as e:
            self._status("Read failed", "error")
            self.log("ERROR: %s" % e)
            messagebox.showerror(APP_TITLE, "Read failed:\n%s" % e)

    def write_fru(self):
        if self.raw is None:
            messagebox.showwarning(APP_TITLE, "Read the FRU first.")
            return
        if not messagebox.askyesno(APP_TITLE, "Write the edited fields to the FRU EEPROM?"):
            return
        values = {"board": {}, "product": {}}
        for key, fields in self.entries.items():
            for name, (var, ent, _hint) in fields.items():
                if str(ent.cget("state")) != "disabled":
                    values[key][name] = var.get()
        values["board"]["mfg_time"] = self.time_var.get()
        try:
            _size, limit = self._sizes()
            image = build_image(self.parsed, values, limit=limit)
            payload = image_for_eeprom(image, limit)
            acc = self._access()
            acc.write(0, payload)
            raw = acc.dump()
            parsed = parse_fru(raw, limit=limit)
            self._fill(raw, parsed)
            self.fruinfo_var.set(self._fru_stats(parsed, limit))
            problems = []
            for key, area_key in (("board", "board_area"), ("product", "product_area")):
                info = parsed[area_key]
                if not info:
                    continue
                for f in info["fields"]:
                    if f["name"] in values[key] and f["value"] != values[key][f["name"]]:
                        problems.append("%s.%s" % (key, f["name"]))
            if problems:
                raise RuntimeError("verification failed: " + ", ".join(problems))
            self._status("FRU written and verified — %d bytes" % len(image), "ok")
            self.log("Rebuilt and wrote the FRU image (%d bytes), verification OK." % len(image))
        except Exception as e:
            self._status("Write failed", "error")
            self.log("ERROR: %s" % e)
            messagebox.showerror(APP_TITLE, "Write failed:\n%s" % e)

    def save_image(self):
        if self.raw is None:
            messagebox.showwarning(APP_TITLE, "Read the FRU first.")
            return
        path = filedialog.asksaveasfilename(defaultextension=".bin",
                                            filetypes=[("FRU image", "*.bin"), ("All files", "*.*")])
        if not path:
            return
        with open(path, "wb") as f:
            f.write(self.raw)
        self.log("Saved %d bytes to %s" % (len(self.raw), path))

    def load_image(self):
        path = filedialog.askopenfilename(filetypes=[("FRU image", "*.bin"), ("All files", "*.*")])
        if not path:
            return
        try:
            _size, limit = self._sizes()
            with open(path, "rb") as f:
                data = f.read(limit)
            if not data:
                raise ValueError("empty image")
            parse_fru(data, limit=limit)  # validate before writing
            if not messagebox.askyesno(APP_TITLE, "Write the whole image to the FRU EEPROM?"):
                return
            acc = self._access()
            acc.write(0, image_for_eeprom(data[:limit], limit))
            raw = acc.dump()
            self._fill(raw, parse_fru(raw, limit=limit))
            self._status("Image written", "ok")
            self.log("Wrote %d-byte image from %s" % (len(data), path))
        except Exception as e:
            self._status("Load failed", "error")
            self.log("ERROR: %s" % e)
            messagebox.showerror(APP_TITLE, "Load failed:\n%s" % e)


def main():
    ap = argparse.ArgumentParser(description="uFC FRU desktop editor")
    ap.add_argument("--mch", default="192.168.1.41")
    ap.add_argument("--target", default="0x7e")
    ap.add_argument("--bus", default="7")
    ap.add_argument("--transit", default="0x82")
    ap.add_argument("--eeprom-size", type=int, default=256, help="EEPROM size in bytes (default 256)")
    ap.add_argument("--fru-limit", type=int, default=128, help="writable FRU bytes (default 128)")
    args = ap.parse_args()
    app = FruEditor(args.mch, args.target, args.bus, args.transit,
                    eeprom_size=args.eeprom_size, fru_limit=args.fru_limit)
    app.mainloop()


if __name__ == "__main__":
    main()
