#!/usr/bin/env python3
"""
fru_gui_tk.py -- uFC Toolbox: FRU / I2C / PMBus desktop tool (Tkinter).

Reads/writes the FRU EEPROM through a NAT-MCH (IPMI raw commands 0x32/0x12,
0x13), edits the board/product fields, saves or loads the raw image.
"""

import argparse
import json
import zlib
import datetime
import os
import queue
import re
import subprocess
import sys
import shutil
import tempfile
import threading
import time
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fru_core import (FruAccess, parse_fru, build_image, image_for_eeprom,  # noqa: E402
                      EEPROM_SIZE, FRU_MAX_SIZE, FRU_MAX_STRING,
                      mfg_minutes_to_str, mfg_str_to_minutes,
                      parse_si5345_txt, program_si5345, read_si5345, default_si5345_groups,
                      build_clock_slot, upload_clock_slot)

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
        self.geometry("1120x1040")
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
        self._restore_settings()
        self._on_amc_change()
        self.protocol("WM_DELETE_WINDOW", self._on_close)
        self._status("Ready — firmware must be built with -DEEPROM_ACCESS=ON", "idle")

    def _on_close(self):
        self._save_settings()
        self.destroy()

    # -------------------------------------------------------------- settings
    CONFIG_PATH = os.path.expanduser("~/.config/ufc-toolbox.json")

    def _load_settings(self):
        try:
            with open(self.CONFIG_PATH) as f:
                return json.load(f)
        except Exception:
            return {}

    def _save_settings(self):
        try:
            os.makedirs(os.path.dirname(self.CONFIG_PATH), exist_ok=True)
            data = {
                "board": getattr(self, "template", "uFC"),
                "mch": self.mch_var.get(),
                "bridge": self.bridge_var.get(),
                "amc": self.amc_var.get(),
                "transit": self.transit_var.get(),
                "transit_ch": self.tch_var.get(),
                "target": self.target_var.get(),
                "bus": self.bus_var.get(),
                "eeprom_i2c": self.i2c_var.get(),
                "eeprom_size": self.size_var.get(),
                "fru_limit": self.limit_var.get(),
            }
            with open(self.CONFIG_PATH, "w") as f:
                json.dump(data, f, indent=1)
        except Exception as e:
            if hasattr(self, "clock_out"):
                self._clock_log("settings not saved: %s" % e)

    def _restore_settings(self):
        cfg = self._load_settings()
        if not cfg:
            return
        board = cfg.get("board")
        if board == "u4FCP":
            board = "u4FCP v2"      # saved before the v1/v2 split
        if board in self.CLOCKING_TEMPLATES:
            self.template_var.set(board)
            self.template_load()
        for var, key in ((self.mch_var, "mch"), (self.bridge_var, "bridge"),
                         (self.size_var, "eeprom_size"), (self.limit_var, "fru_limit")):
            if cfg.get(key) is not None:
                var.set(str(cfg[key]))
        amc = cfg.get("amc")
        if not amc and cfg.get("target"):
            try:
                slot = (int(str(cfg["target"]), 0) - 0x70) // 2
                if 1 <= slot <= 12:
                    amc = "AMC%d" % slot
            except (TypeError, ValueError):
                amc = None
        if amc:
            self.amc_var.set(str(amc))
            self._on_amc_change()
        if cfg.get("eeprom_i2c") is not None:
            self.i2c_var.set(str(cfg["eeprom_i2c"]))

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

        # connection strip: top right of the header line (saves a whole row)
        strip = ttk.Frame(header)
        strip.pack(side="right", anchor="ne", padx=(24, 0))

        ttk.Separator(self).pack(fill="x", padx=16)
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

        # AMC slot selector: derives the transit/target/bus automatically
        abox = ttk.Frame(strip)
        abox.pack(side="left", padx=(0, 14))
        alab = ttk.Label(abox, text="AMC", style="FieldLabel.TLabel")
        alab.pack(anchor="w")
        self.amc_var = tk.StringVar(value="AMC12")
        self.amc_box = ttk.Combobox(abox, textvariable=self.amc_var, width=7,
                                    state="readonly",
                                    values=["AMC%d" % i for i in range(1, 13)])
        self.amc_box.pack(anchor="w")
        self.amc_box.bind("<<ComboboxSelected>>", lambda _e: self._on_amc_change())
        Tooltip(alab, "AMC slot of the target module.\n"
                      "Derived: transit 0x82, target 0x70 + 2*slot, bus 0,\n"
                      "FRU id = slot + 4.")
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
        for widget in (self.transit_w, self.tch_w, self.target_w, self.bus_w):
            try:
                widget.configure(state="readonly")
            except Exception:
                pass

        # board template (shared by every tab)
        tpl = ttk.Frame(self, padding=(16, 2, 16, 4))
        tpl.pack(fill="x")
        bcell = ttk.Frame(tpl)
        bcell.pack(side="left", padx=(0, 18))
        ttk.Label(bcell, text="Board template",
                  style="FieldLabel.TLabel").pack(anchor="w")
        brow = ttk.Frame(bcell)
        brow.pack(anchor="w")
        self.template = "uFC"
        self.template_var = tk.StringVar(value=self.template)
        tbox = ttk.Combobox(brow, textvariable=self.template_var,
                            values=("uFC", "u4FCP v1", "u4FCP v2"), width=10, state="readonly")
        tbox.pack(side="left")
        Tooltip(tbox, "Clock/board description used by the tabs\n"
                      "(tools/clocking/<template>-clocking.json)")
        tload = ttk.Button(brow, text="Load", command=self.template_load)
        tload.pack(side="left", padx=(4, 0))
        Tooltip(tload, "Load the selected board template")
        self.template_label = tk.StringVar(value="uFC")

        # FRU storage, same row as the board selector (filled by the template)
        self.i2c_var, self.i2c_w = self._labeled(
            tpl, "EEPROM I\u00b2C", "0x50", 6,
            tip="7-bit I2C address of the FRU EEPROM.\n"
                "uFC: 0x50 (24AA025E48); u4FCP: 0x54 (CAT24C512/AT24CM01).\n"
                "Set automatically by the board template.")
        self.i2c_w.master.pack_configure(padx=(0, 14))
        self.size_var, self.size_w = self._labeled(
            tpl, "EEPROM size (B)", str(eeprom_size), 7,
            tip="Bytes read from the EEPROM for the FRU image.\n"
                "Set automatically by the board template.")
        self.size_w.master.pack_configure(padx=(0, 14))
        self.limit_var, self.limit_w = self._labeled(
            tpl, "FRU writable (B)", str(fru_limit), 7,
            tip="Bytes of the FRU image that are written.\n"
                "uFC: 128 (24AA025E48 half page); u4FCP: 512 (FRU_MAX_SIZE).\n"
                "Set automatically by the board template.")

        # path / FRU / EEPROM info: right side of the template row
        info = ttk.Frame(tpl)
        info.pack(side="right", padx=(14, 0), anchor="n")
        self.path_var = tk.StringVar()
        self.fruinfo_var = tk.StringVar(value="no FRU read yet")
        tk.Label(info, textvariable=self.path_var, bg=BG, fg=MUTED,
                 font=(UI_FONT, 8), anchor="e", justify="right").pack(anchor="e")
        tk.Label(info, textvariable=self.fruinfo_var, bg=BG, fg=MUTED,
                 font=(UI_FONT, 8), anchor="e", justify="right").pack(anchor="e")
        self.addr_var = tk.StringVar()
        tk.Label(info, textvariable=self.addr_var, bg=BG, fg=MUTED,
                 font=(UI_FONT, 8), anchor="e", justify="right").pack(anchor="e")

        # notebook
        self.nb = ttk.Notebook(self)
        self.nb.pack(fill="both", expand=True, padx=16, pady=(8, 8))

        self.fields_tab = ttk.Frame(self.nb)
        self.raw_tab = ttk.Frame(self.nb, padding=12)
        self.nb.add(self.fields_tab, text="FRU fields")

        toolbar = ttk.Frame(self.fields_tab, padding=(12, 8, 12, 0))
        toolbar.pack(fill="x")
        ttk.Button(toolbar, text="Read FRU",
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

        self.clock_tab = ttk.Frame(self.nb, padding=12)
        self.nb.add(self.clock_tab, text="Clock")
        self._build_clock_tab(self.clock_tab)

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

    CLOCK_INPUTS = [
        ("CLK0", "AMC_TCLKD"), ("CLK1", "AMC_TCLKC"), ("CLK2", "AMC_TCLKA"),
        ("CLK3", "AMC_TCLKB"), ("CLK4", "AMC_FCLKA"), ("CLK5", "S7_INOUT"),
        ("CLK6", "FPGA_INOUT"), ("CLK7", "156M"), ("CLK8", "PLL_OUT"),
    ]
    CLOCK_OUTPUTS = [
        ("CLK9", "PLL_IN_P2"), ("CLK10", "PLL_IN_P1"), ("CLK11", "PLL_IN_P0"),
        ("CLK12", "FMC0_GBTCLK"), ("CLK13", "FMC0_CLK"),
        ("CLK14", "FMC1_CLK"), ("CLK15", "FMC1_GBTCLK"),
    ]

    def _hint(self, widget, text):
        """Show text in the clock hint line while the pointer is over widget."""
        widget.bind("<Enter>", lambda _e: self.clock_hint.set(text), add="+")
        widget.bind("<Leave>", lambda _e: self.clock_hint.set(""), add="+")


    def _bind_scroll_wheel(self, widget, canvas):
        """Scroll the clock tab with the mouse wheel over any of its widgets."""
        widget.bind("<Button-4>", lambda e: canvas.yview_scroll(-3, "units"))
        widget.bind("<Button-5>", lambda e: canvas.yview_scroll(3, "units"))
        widget.bind("<MouseWheel>", lambda e: canvas.yview_scroll(int(-e.delta / 120) or -1, "units"))
        for child in widget.winfo_children():
            self._bind_scroll_wheel(child, canvas)

    def _mux_part(self):
        doc = self._load_clocking()
        if not doc:
            return ""
        return str((doc.get("clock") or {}).get("switch", {}).get("part", ""))

    def _mux_rebuild(self):
        """Build the mux table matching the loaded template's switch."""
        if not hasattr(self, "mux_body"):
            return
        for child in self.mux_body.winfo_children():
            child.destroy()
        part = self._mux_part()
        self.mux_frame.configure(text="1  %s MUX" % (part or "8V54816A"))
        if part.startswith("ADN4604"):
            self._build_mux_adn(self.mux_body)
        else:
            self._build_mux_legacy(self.mux_body)

    def _build_mux_legacy(self, parent):
        """8V54816A table: 16 ports, dir + source + termination + polarity."""
        # ---- block 2: MUX ----------------------------------------------
        mrow = ttk.Frame(parent)
        mrow.pack(fill="x", pady=(0, 4))
        readb = ttk.Button(mrow, text="Read from device",
                           command=self.clock_switch_read)
        readb.pack(side="left")
        self._hint(readb, "Read the 16 port bytes from the switch and update the table")
        writeb = ttk.Button(mrow, text="Write to device", command=self.clock_switch_write)
        writeb.pack(side="left", padx=(8, 0))
        self._hint(writeb, "Write the 16 port bytes to the switch (volatile)")
        ttk.Label(mrow, text="slot", style="FieldLabel.TLabel").pack(side="left", padx=(10, 4))
        mslot = ttk.Combobox(mrow, textvariable=self.slot_sel, values=("auto", "A", "B"),
                             width=5, state="readonly")
        mslot.pack(side="left")
        self._hint(mslot, "Flash target: auto = inactive slot (atomic), A or B explicit")
        mfl = ttk.Button(mrow, text="Write to flash", command=self.si_write_flash)
        mfl.pack(side="left", padx=(6, 0))
        self._hint(mfl, "Store matrix + loaded Si5345 table in the clock store - "
                        "applied at the next power-on")

        tblwrap = ttk.Frame(parent)
        tblwrap.pack(fill="both", expand=True, pady=(0, 4))
        tcv = tk.Canvas(tblwrap, height=330, bg=BG, highlightthickness=0)
        vsb = ttk.Scrollbar(tblwrap, orient="vertical", command=tcv.yview)
        tcv.configure(yscrollcommand=vsb.set)
        vsb.pack(side="right", fill="y")
        tcv.pack(side="left", fill="both", expand=True)
        tbl = ttk.Frame(tcv)
        tcv.create_window((0, 0), window=tbl, anchor="nw")
        tbl.bind("<Configure>", lambda e: tcv.configure(scrollregion=tcv.bbox("all")))
        tcv.bind("<Configure>", lambda e: tcv.itemconfigure(tcv.find_all()[0], width=e.width))
        headers = (("port / signal", 0, "8V54816A port and its signal name"),
                   ("dir", 1, "in: input (bit7=0) / out: output (bit7=1)"),
                   ("source", 2, "input port routed to this output (bits3:0)"),
                   ("term", 3, "100 ohm internal termination (bit6)"),
                   ("pol", 4, "ticked = non-inverted (bit5=1), unticked = inverted"),
                   ("byte", 5, "matrix byte written to the switch (0x58)"))
        for text, col, tip in headers:
            lbl = ttk.Label(tbl, text=text, style="Hint.TLabel")
            lbl.grid(row=0, column=col, sticky="w", padx=(4, 8))
            self._hint(lbl, tip)

        self.clock_matrix = bytearray(16)
        self.clock_loading = False
        self.port_vars = []
        self.port_labels = []
        for i in range(16):
            port, name = (self.CLOCK_INPUTS + self.CLOCK_OUTPUTS)[i]
            allowed = self._mux_allowed().get(
                i, ("out",) if i in (9, 10, 11) else ("in",) if i in (7, 8) else ("in", "out"))
            row = i + 1
            lbl = ttk.Label(tbl, text="%s %s" % (port, name), width=22, anchor="w",
                            font=(UI_FONT, 9))
            lbl.grid(row=row, column=0, sticky="w", padx=(4, 8))
            self.port_labels.append(lbl)
            d = tk.StringVar(value="in")
            dirbox = ttk.Combobox(tbl, textvariable=d, values=("in", "out"), width=4,
                                  state="readonly")
            dirbox.grid(row=row, column=1, sticky="w", padx=(4, 8))
            self._hint(dirbox, "Direction: in (bit7=0) or out (bit7=1)")
            src = tk.StringVar(value="-")
            srcbox = ttk.Combobox(tbl, textvariable=src, width=24, state="disabled")
            srcbox.grid(row=row, column=2, sticky="w", padx=(4, 8))
            self._hint(srcbox, "Input port routed to this output (source field)")
            term = tk.BooleanVar(value=False)
            termbox = ttk.Checkbutton(tbl, variable=term)
            termbox.grid(row=row, column=3, sticky="w", padx=(4, 8))
            self._hint(termbox, "100 ohm internal termination (bit6)")
            pol = tk.BooleanVar(value=True)
            polbox = ttk.Checkbutton(tbl, variable=pol)
            polbox.grid(row=row, column=4, sticky="w", padx=(4, 8))
            self._hint(polbox, "Polarity: ticked = non-inverted (bit5=1)")
            byte = tk.StringVar(value="0x00")
            ttk.Label(tbl, textvariable=byte, width=6, font=(MONO_FONT, 9)).grid(
                row=row, column=5, sticky="w", padx=(4, 8))
            entry = {"dir": d, "src": src, "srcbox": srcbox, "term": term, "pol": pol,
                     "byte": byte, "allowed": allowed}
            self.port_vars.append(entry)
            d.trace_add("write", lambda *_a, idx=i: self._port_dir_changed(idx))
            src.trace_add("write", lambda *_a, idx=i: self._port_apply(idx))
            term.trace_add("write", lambda *_a, idx=i: self._port_apply(idx))
            pol.trace_add("write", lambda *_a, idx=i: self._port_apply(idx))

        ttk.Label(mrow, text="matrix", style="FieldLabel.TLabel").pack(side="left", padx=(16, 4))
        self.switch_var = tk.StringVar(value="")
        ttk.Entry(mrow, textvariable=self.switch_var, width=52,
                  state="readonly").pack(side="left")
        self._port_load_all()

    # ---- ADN4604 (u4FCP) mux table --------------------------------------
    ADN_DEV = 0x48
    ADN_MUX = [(0x70, 2)]
    ADN_TX_MODES = ("disabled", "standby", "squelched", "enabled")
    ADN_TERMS = (
        ("RXW", 0, "Inputs 7:0 (West) 50 ohm on-chip termination - "
                   "0xF0[0], ticked = termination DISABLED (1)"),
        ("RXE", 1, "Inputs 15:8 (East) 50 ohm on-chip termination - "
                   "0xF0[1], ticked = termination DISABLED (1)"),
        ("TXS", 2, "Outputs 7:0 (South) 50 ohm on-chip termination - "
                   "0xF0[2], ticked = termination DISABLED (1)"),
        ("TXN", 3, "Outputs 15:8 (North) 50 ohm on-chip termination - "
                   "0xF0[3], ticked = termination DISABLED (1)"),
    )

    def _adn_rw(self, wdata=None, rlen=0):
        acc = self._access()
        if wdata is None:
            return acc.xfer(1, self.ADN_MUX, self.ADN_DEV, bytes([0x90]), rlen)
        return acc.xfer(1, self.ADN_MUX, self.ADN_DEV, bytes(wdata), 0)

    def _adn_read_block(self, reg, n):
        acc = self._access()
        return acc.xfer(1, self.ADN_MUX, self.ADN_DEV, bytes([reg]), n)

    def _adn_write_reg(self, reg, val):
        acc = self._access()
        acc.xfer(1, self.ADN_MUX, self.ADN_DEV, bytes([reg, val]), 0)

    def _build_mux_adn(self, parent):
        row = ttk.Frame(parent)
        row.pack(fill="x", pady=(0, 4))
        rb = ttk.Button(row, text="Read ADN4604", command=self.adn_read)
        rb.pack(side="left")
        self._hint(rb, "Read map 0/1, TX enables, input inversion and termination")
        wb = ttk.Button(row, text="Write ADN4604", command=self.adn_write)
        wb.pack(side="left", padx=(8, 0))
        self._hint(wb, "Write map 0 + TX enables + termination, then UPDATE")
        mfl = ttk.Button(row, text="Write to flash", command=self.si_write_flash)
        mfl.pack(side="left", padx=(8, 0))
        self._hint(mfl, "Store the matrix (bit7 = output enable, [3:0] = input) in the clock store")
        ttk.Label(row, text="slot", style="FieldLabel.TLabel").pack(side="left", padx=(10, 4))
        ttk.Combobox(row, textvariable=self.slot_sel, values=("auto", "A", "B"),
                     width=5, state="readonly").pack(side="left")

        body = ttk.Frame(parent)
        body.pack(fill="both", expand=True, pady=(0, 4))
        in_lf = ttk.LabelFrame(body, text="inputs  (INV = P/N inversion)",
                               style="Section.TLabelframe", padding=(6, 2))
        in_lf.pack(side="left", fill="both", expand=True, padx=(0, 14))
        out_lf = ttk.LabelFrame(body, text="outputs  (source select + TX enable)",
                                style="Section.TLabelframe", padding=(6, 2))
        out_lf.pack(side="left", fill="both", expand=True)

        labels = self._mux_port_labels()
        src_names = self._mux_sources()
        values = ["IN%d %s" % (i, src_names[i].split(" ", 1)[1]) if i in src_names else "IN%d" % i
                  for i in range(16)]

        self.adn_out = []
        self.adn_inv = []

        ttk.Label(in_lf, text="input", style="Hint.TLabel").grid(row=0, column=0, sticky="w", padx=(4, 8))
        ttk.Label(in_lf, text="inv", style="Hint.TLabel").grid(row=0, column=1, sticky="w", padx=(4, 8))
        for i in range(16):
            name = labels.get(i, "CLK%d" % i)
            ttk.Label(in_lf, text=name, width=22, anchor="w",
                      font=(UI_FONT, 9)).grid(row=i + 1, column=0, sticky="w", padx=(4, 8))
            inv = tk.BooleanVar(value=False)
            cb = ttk.Checkbutton(in_lf, text="INV", variable=inv)
            cb.grid(row=i + 1, column=1, sticky="w", padx=(4, 8))
            self._hint(cb, "SIGN[%d]: 1 = invert the P/N pair of input IN%d" % (i, i))
            self.adn_inv.append(inv)

        ttk.Label(out_lf, text="output", style="Hint.TLabel").grid(row=0, column=0, sticky="w", padx=(4, 8))
        ttk.Label(out_lf, text="source (input)", style="Hint.TLabel").grid(row=0, column=1, sticky="w", padx=(4, 8))
        ttk.Label(out_lf, text="TX enable", style="Hint.TLabel").grid(row=0, column=2, sticky="w", padx=(4, 8))
        for i in range(16):
            name = labels.get(i, "CLK%d" % i)
            ttk.Label(out_lf, text=name, width=22, anchor="w",
                      font=(UI_FONT, 9)).grid(row=i + 1, column=0, sticky="w", padx=(4, 8))
            src = tk.StringVar(value=values[0] if values else "IN0")
            sb = ttk.Combobox(out_lf, textvariable=src, values=values, width=22, state="readonly")
            sb.grid(row=i + 1, column=1, sticky="w", padx=(4, 8))
            self._hint(sb, "input routed to this output (map byte [3:0])")
            en = tk.StringVar(value="disabled")
            eb = ttk.Combobox(out_lf, textvariable=en, values=self.ADN_TX_MODES, width=10,
                              state="readonly")
            eb.grid(row=i + 1, column=2, sticky="w", padx=(4, 8))
            self._hint(eb, "TX basic control [5:4]: 00 disabled, 11 enabled")
            self.adn_out.append({"src": src, "en": en})

        term = ttk.Frame(parent)
        term.pack(fill="x", pady=(2, 2))
        ttk.Label(term, text="termination (ticked = OFF)",
                  style="FieldLabel.TLabel").pack(side="left", padx=(0, 6))
        self.adn_term = {}
        for name, bit, tip in self.ADN_TERMS:
            v = tk.BooleanVar(value=False)
            cb = ttk.Checkbutton(term, text=name, variable=v)
            cb.pack(side="left", padx=(0, 8))
            self._hint(cb, tip + " (ticked = disabled)")
            self.adn_term[name] = v
        self.adn_info = tk.StringVar(value="")
        tk.Label(term, textvariable=self.adn_info, bg=BG, fg=MUTED,
                 font=(UI_FONT, 8), anchor="w").pack(side="left", padx=(12, 0))

        note = ("0xF0 registers the on-chip 50 ohm terminations in four quadrants: "
                "RXW = inputs 7:0 (West), RXE = inputs 15:8 (East), TXS = outputs 7:0 "
                "(South), TXN = outputs 15:8 (North).  Bit = 1 disables the termination, "
                "bit = 0 enables it (reset default 0x00 = all enabled).  The firmware's "
                "clock_configuration() writes 0x0F, i.e. all four disabled - only "
                "correct if the far end terminates the line (FPGA/fanout 50 ohm); a "
                "ticked box here means that group stays unterminated.")
        tk.Label(parent, text=note, bg=BG, fg=MUTED, font=(UI_FONT, 8),
                 justify="left", anchor="w", wraplength=1080).pack(fill="x", pady=(2, 0))

    @staticmethod
    def _adn_src_index(text, default=0):
        try:
            return int(str(text).split()[0].replace("IN", "")) & 0x0F
        except Exception:
            return default

    def adn_read(self):
        try:
            map0 = self._adn_read_block(0x90, 8)
            tx = self._adn_read_block(0x20, 16)
            sign = self._adn_read_block(0x12, 2)
            term = self._adn_read_block(0xF0, 1)[0]
            status = self._adn_read_block(0xB0, 8)
            for i in range(16):
                nib = (map0[i // 2] >> (4 * (i % 2))) & 0x0F
                self.adn_out[i]["src"].set("IN%d" % nib)
                self.adn_out[i]["en"].set(self.ADN_TX_MODES[(tx[i] >> 4) & 0x3])
            for i in range(16):
                self.adn_inv[i].set(bool((sign[i // 8] >> (i % 8)) & 1))
            for name, bit, _tip in self.ADN_TERMS:
                self.adn_term[name].set(bool((term >> bit) & 1))
            self.adn_info.set("TX=0x%02X.. term=0x%02X sign=0x%02X%02X status=%s"
                              % (tx[0], term, sign[1], sign[0],
                                 " ".join("%02X" % b for b in status)))
            self._adn_sync_matrix()
        except Exception as e:
            messagebox.showerror(APP_TITLE, "ADN4604 read failed:\n%s" % e)

    def _adn_sync_matrix(self):
        for i in range(16):
            by = self._adn_src_index(self.adn_out[i]["src"].get())
            if self.adn_out[i]["en"].get() != "disabled":
                by |= 0x80
            self.clock_matrix[i] = by
        self.clock_loading = True
        try:
            self.switch_var.set(" ".join("%02x" % b for b in self.clock_matrix))
        except Exception:
            pass
        self.clock_loading = False

    def adn_write(self):
        try:
            mapbytes = bytearray(8)
            tx = bytearray(16)
            for i in range(16):
                src = self._adn_src_index(self.adn_out[i]["src"].get())
                mapbytes[i // 2] |= (src & 0x0F) << (4 * (i % 2))
                mode = self.ADN_TX_MODES.index(self.adn_out[i]["en"].get()) if \
                    self.adn_out[i]["en"].get() in self.ADN_TX_MODES else 0
                tx[i] = (mode & 0x3) << 4
            sign = [0, 0]
            for i in range(16):
                if self.adn_inv[i].get():
                    sign[i // 8] |= 1 << (i % 8)
            term = 0
            for name, bit, _tip in self.ADN_TERMS:
                if self.adn_term[name].get():
                    term |= 1 << bit
            acc = self._access()
            for i in range(16):
                acc.xfer(1, self.ADN_MUX, self.ADN_DEV, bytes([0x20 + i, tx[i]]), 0)
            for i, b in enumerate(mapbytes):
                acc.xfer(1, self.ADN_MUX, self.ADN_DEV, bytes([0x90 + i, b]), 0)
            acc.xfer(1, self.ADN_MUX, self.ADN_DEV, bytes([0x12, sign[0]]), 0)
            acc.xfer(1, self.ADN_MUX, self.ADN_DEV, bytes([0x13, sign[1]]), 0)
            acc.xfer(1, self.ADN_MUX, self.ADN_DEV, bytes([0x81, 0x00]), 0)   # map 0
            acc.xfer(1, self.ADN_MUX, self.ADN_DEV, bytes([0x80, 0x01]), 0)   # UPDATE
            acc.xfer(1, self.ADN_MUX, self.ADN_DEV, bytes([0xF0, term]), 0)
            self.adn_read()
            self.adn_info.set("written, read back: " + self.adn_info.get())
        except Exception as e:
            messagebox.showerror(APP_TITLE, "ADN4604 write failed:\n%s" % e)

    def _build_clock_tab(self, parent):
        self.clock_cfg = {}
        self.si_tables = {}
        self.si_pll = tk.StringVar(value="PLL0")
        self.slot_sel = tk.StringVar(value="auto")
        self.clock_matrix = bytearray(16)
        self.clock_loading = False

        # the tab is taller than the window: put everything in a scrolling canvas
        scroll = tk.Canvas(parent, bg=BG, highlightthickness=0)
        vsb = ttk.Scrollbar(parent, orient="vertical", command=scroll.yview)
        scroll.configure(yscrollcommand=vsb.set)
        vsb.pack(side="right", fill="y")
        scroll.pack(side="left", fill="both", expand=True)
        inner = ttk.Frame(scroll)
        window = scroll.create_window((0, 0), window=inner, anchor="nw")
        inner.bind("<Configure>", lambda e: scroll.configure(scrollregion=scroll.bbox("all")))
        scroll.bind("<Configure>", lambda e: scroll.itemconfigure(window, width=e.width))
        self.clock_scroll = scroll
        parent = inner
        # ---- block 2: MUX (template dependent) ------------------------
        mux = ttk.LabelFrame(parent, text="1  MUX", padding=(8, 4))
        self.mux_frame = mux
        mux.pack(fill="both", expand=True, pady=(0, 4))
        self.mux_body = ttk.Frame(mux)
        self.mux_body.pack(fill="both", expand=True)
        self._mux_rebuild()

        # ---- block 3: PLL ----------------------------------------------
        pll = ttk.LabelFrame(parent, text="2  Si5345 PLL", padding=(8, 4))
        pll.pack(fill="x", pady=(0, 4))
        prow = ttk.Frame(pll)
        prow.pack(fill="x")
        rd = ttk.Button(prow, text="Read from device", command=self.si_read_device)
        rd.pack(side="left")
        self._hint(rd, "Read the loaded register list from the Si5345 and save it to a file")
        self.pll_box = ttk.Combobox(prow, textvariable=self.si_pll, width=10, state="readonly",
                                    values=("PLL0",))
        self.pll_box.pack(side="left", padx=(8, 0))
        self._hint(self.pll_box, "PLL whose table is loaded/written (board template)")
        lf = ttk.Button(prow, text="Load Si5345 file…", command=self.si_load)
        lf.pack(side="left", padx=(6, 0))
        self._hint(lf, "Load a ClockBuilder register script (pre/body/post)")
        dev = ttk.Button(prow, text="Write to device",
                         command=self.si_write_device)
        dev.pack(side="left", padx=(8, 0))
        self._hint(dev, "Write the loaded table to the Si5345 RAM over I2C - "
                        "immediate, lost on power-cycle")
        ttk.Label(prow, text="slot", style="FieldLabel.TLabel").pack(side="left", padx=(10, 4))
        pslot = ttk.Combobox(prow, textvariable=self.slot_sel, values=("auto", "A", "B"),
                             width=5, state="readonly")
        pslot.pack(side="left")
        self._hint(pslot, "Flash target: auto = inactive slot (atomic), A or B explicit")
        fl = ttk.Button(prow, text="Write to flash", command=self.si_write_flash)
        fl.pack(side="left", padx=(6, 0))
        self._hint(fl, "Store matrix + Si5345 table in the MMC clock store - "
                       "applied at the next power-on")
        srow2 = ttk.Frame(pll)
        srow2.pack(fill="x", pady=(2, 0))
        infob = ttk.Button(srow2, text="Stored config info", command=self.store_status)
        infob.pack(side="left")
        self._hint(infob, "Show the active slot, length and CRC of the stored configuration")
        self.si_label = tk.StringVar(value="no Si5345 file loaded")
        ttk.Label(srow2, textvariable=self.si_label,
                  style="Hint.TLabel").pack(side="left", padx=(12, 0))
        self.si_groups = None

        lockrow = ttk.Frame(pll)
        lockrow.pack(fill="x", pady=(4, 0))
        lk = ttk.Button(lockrow, text="Update lock status", command=self.pll_lock_status)
        lk.pack(side="left")
        self._hint(lk, "Read the Si534x status registers (0x0C00 live, 0x0C01 sticky) "
                       "and show whether each PLL is locked")
        self.lock_var = tk.StringVar(value="PLL0: ?    PLL1: ?")
        ttk.Label(lockrow, textvariable=self.lock_var,
                  style="Hint.TLabel").pack(side="left", padx=(12, 0))

        # ---- block 4: block diagram ------------------------------------
        dia = ttk.LabelFrame(parent, text="3  Block diagram", padding=(8, 4))
        dia.pack(fill="x", pady=(0, 4))
        drow = ttk.Frame(dia)
        drow.pack(fill="x")
        exp = ttk.Button(drow, text="Export diagram", command=self.clock_export_dot)
        exp.pack(side="left")
        self._hint(exp, "Save the clocking description as a Graphviz DOT file")
        sysd = ttk.Button(drow, text="System diagram", command=self.clock_show_system)
        sysd.pack(side="left", padx=(8, 0))
        self._hint(sysd, "Draw the board clocking (inputs left, outputs right, jumpers)")

        self.clock_out = tk.Text(parent, height=3, font=(MONO_FONT, 9), state="disabled",
                                 bg=PANEL, fg=INK, relief="flat", highlightthickness=1,
                                 highlightbackground=LINE, padx=8, pady=6)
        self.clock_out.pack(fill="x")

        self.clock_hint = tk.StringVar(value="")
        ttk.Label(parent, textvariable=self.clock_hint, style="Hint.TLabel").pack(
            fill="x", pady=(4, 0))
        self._bind_scroll_wheel(inner, scroll)

    def template_load(self):
        self.template = self.template_var.get()
        doc = self._load_clocking()
        path = self._clocking_path()
        if not doc:
            self.template_label.set("%s — no %s-clocking.json found" % (
                self.template, self.CLOCKING_TEMPLATES.get(self.template, "ufc")))
            self._clock_log("template %s: clocking file not found" % self.template)
            return
        self.template_label.set(self.template)
        self.clock_cfg = doc.get("clock", {}) or {}
        fru = doc.get("fru") or {}
        if fru and hasattr(self, "i2c_var"):
            if fru.get("eeprom_addr") is not None:
                self.i2c_var.set(str(fru["eeprom_addr"]))
            if fru.get("eeprom_size") is not None:
                self.size_var.set(str(fru["eeprom_size"]))
            if fru.get("writable") is not None:
                self.limit_var.set(str(fru["writable"]))
            if hasattr(self, "clock_out"):
                self._clock_log("board %s: FRU EEPROM %s, size %s B, writable %s B" % (
                    self.template, fru.get("eeprom_addr", "?"),
                    fru.get("eeprom_size", "?"), fru.get("writable", "?")))
        if hasattr(self, "pll_box"):
            plls = self.clock_cfg.get("plls") or [{"addr": 0x69}]
            self.pll_box["values"] = tuple("PLL%d" % i for i in range(len(plls)))
            self.si_pll.set("PLL0")
            self.si_tables = {}
            self._mux_format = str((self.clock_cfg.get("switch") or {}).get("format", ""))
        if hasattr(self, "mux_body"):
            self._mux_rebuild()
        if hasattr(self, "clock_out"):
            self._clock_log("template %s loaded from %s" % (self.template, path))
            if doc.get("meta", {}).get("status") == "skeleton":
                self._clock_log("note: this template is a skeleton — fill tools/clocking/%s-clocking.json"
                                % self.CLOCKING_TEMPLATES.get(self.template, "u4fcp"))
        if hasattr(self, "tpl_info_var"):
            self.tpl_info_var.set("board: %s" % self.template)
        if hasattr(self, "mch_var"):
            self._save_settings()

    def _mux_port_labels(self):
        """All 16 mux ports from the clocking file: {index: 'CLKn signal'}."""
        doc = self._load_clocking()
        res = {}
        if doc:
            devices = doc.get("devices", {})
            for p in doc.get("points", []):
                parent = p.get("parent", "")
                dev = devices.get(parent, {})
                if parent.upper() != "MUX" and not str(dev.get("part", "")).startswith(("8V54816A", "ADN4604")):
                    continue
                try:
                    idx = int(str(p.get("name", "")).upper().replace("CLK", ""))
                except ValueError:
                    continue
                label = p.get("name") or ("CLK%d" % idx)
                if p.get("signal"):
                    label += " " + p["signal"]
                res[idx] = label
        return res

    def _mux_sources(self):
        """All input-capable mux ports from the clocking file: {port: name}."""
        doc = self._load_clocking()
        res = {}
        if doc:
            devices = doc.get("devices", {})
            for p in doc.get("points", []):
                parent = p.get("parent", "")
                dev = devices.get(parent, {})
                if parent.upper() != "MUX" and not str(dev.get("part", "")).startswith(("8V54816A", "ADN4604")):
                    continue
                cap = p.get("capability") or {}
                if not cap.get("in", p.get("dir") in ("in", "inout")):
                    continue
                try:
                    idx = int(p["name"].upper().replace("CLK", ""))
                except ValueError:
                    continue
                label = p.get("name") or ("CLK%d" % idx)
                if p.get("signal") and p["signal"].upper() != label.upper():
                    label += " " + p["signal"]
                res[idx] = label
        if not res:
            res = {i: ("CLK%d %s" % (i, n)) for i, (_p, n) in enumerate(self.CLOCK_INPUTS)}
        return res

    def _mux_allowed(self):
        doc = self._load_clocking()
        res = {}
        if not doc:
            return res
        devices = doc.get("devices", {})
        for pt in doc.get("points", []):
            parent = pt.get("parent", "")
            dev = devices.get(parent, {})
            if parent.upper() != "MUX" and not str(dev.get("part", "")).startswith(("8V54816A", "ADN4604")):
                continue
            cap = pt.get("capability") or {}
            ways = tuple(k for k in ("in", "out") if cap.get(k))
            if ways:
                try:
                    res[int(pt["name"].upper().replace("CLK", ""))] = ways
                except ValueError:
                    pass
        return res

    def _port_refresh_names(self):
        """Apply the template's port names/directions to the mux table."""
        labels = self._mux_port_labels()
        fallback = self.CLOCK_INPUTS + self.CLOCK_OUTPUTS
        for i in range(16):
            if hasattr(self, "port_labels") and i < len(self.port_labels):
                self.port_labels[i].configure(text=labels.get(i, "%s %s" % fallback[i]))
            if i < len(self.port_vars):
                allowed = self._mux_allowed().get(i)
                if allowed:
                    self.port_vars[i]["allowed"] = allowed

    def _port_load_all(self):
        self._port_refresh_names()
        self.clock_loading = True
        for i in range(16):
            cfg = self.clock_matrix[i]
            d = self.port_vars[i]
            d["dir"].set("out" if (cfg & 0x80) else "in")
            if d["dir"].get() not in d["allowed"]:
                d["dir"].set(d["allowed"][0])
            d["term"].set(bool(cfg & 0x40))
            d["pol"].set(bool(cfg & 0x20))
            d["byte"].set("0x%02x" % cfg)
            if cfg & 0x80:
                src = cfg & 0x0F
                sources = self._mux_sources()
                d["src"].set(sources.get(src, "-"))
                d["srcbox"].configure(state="readonly",
                                      values=["-"] + [sources[i] for i in sorted(sources)])
            else:
                d["src"].set("-")
                d["srcbox"].configure(state="disabled")
        self.clock_loading = False
        self._clock_sync()

    def _port_dir_changed(self, i):
        if self.clock_loading:
            return
        d = self.port_vars[i]
        if d["dir"].get() not in d["allowed"]:
            self.clock_loading = True
            d["dir"].set(d["allowed"][0])
            self.clock_loading = False
            return
        if d["dir"].get() == "out" and not d["pol"].get():
            d["pol"].set(True)
        self._port_apply(i)

    def _port_apply(self, i):
        if self.clock_loading:
            return
        d = self.port_vars[i]
        term = 0x40 if d["term"].get() else 0
        pol = 0x20 if d["pol"].get() else 0
        if d["dir"].get() == "in":
            self.clock_matrix[i] = term | pol
            d["src"].set("-")
            d["srcbox"].configure(state="disabled")
        else:
            sources = self._mux_sources()
            d["srcbox"].configure(state="readonly",
                                  values=["-"] + [sources[i2] for i2 in sorted(sources)])
            src_name = d["src"].get()
            if src_name == "-":
                self.clock_matrix[i] = 0x80 | term | pol
            else:
                try:
                    src = int(src_name.split()[0].replace("CLK", ""))
                except ValueError:
                    src = 0
                self.clock_matrix[i] = 0x80 | term | pol | src
                port, _name = (self.CLOCK_INPUTS + self.CLOCK_OUTPUTS)[i]
                self._clock_log("%s <- %s" % (port, src_name))
        d["byte"].set("0x%02x" % self.clock_matrix[i])
        self._clock_sync()

    def _clock_sync(self):
        self.switch_var.set(" ".join("%02x" % b for b in self.clock_matrix))

    def _clock_system_dot(self):
        """Compact system view with the 8V54816A port configuration."""
        active = sum(1 for j in range(7)
                     if (self.clock_matrix[9 + j] & 0x80)
                     and ((self.clock_matrix[9 + j] & 0x0F) < len(self.CLOCK_INPUTS)))
        lines = ["digraph clocking {", "  rankdir=LR;",
                 ('  graph [fontname="Helvetica", nodesep=0.12, ranksep=0.55, compound=true,'
                  '         label="uFC clocking — 8V54816A @0x70 ch2 · Si5345 @0x69 · %d active route(s)",'
                  '         labelloc="t", fontsize=9];' % active),
                 '  node [shape=box, style="rounded,filled", fillcolor="#FFFFFF",',
                 '        color="#E4DED2", fontname="Helvetica", fontsize=8];',
                 '  edge [fontname="Helvetica", fontsize=7];',
                 '  AMC [label="AMC\nTCLK/FCLK\n(bidir)"]; OSC [label="156.25 MHz"];',
                 '  S7IN [label="S7_INOUT\n(bidir)"]; FPGAIN [label="FPGA_INOUT\n(bidir)"];',
                 '  subgraph cluster_mux { label="8V54816A"; fontsize=8; color="#FAF9F5";',
                 '    {rank=same; n0; n1; n2; n3; n4; n5; n6; n7; n8;}',
                 '    {rank=same; n9; n10; n11; n12; n13; n14; n15;}']
        short = {"AMC_TCLKD": "TCLKD", "AMC_TCLKC": "TCLKC", "AMC_TCLKA": "TCLKA",
                 "AMC_TCLKB": "TCLKB", "AMC_FCLKA": "FCLKA", "S7_INOUT": "S7_INOUT",
                 "FPGA_INOUT": "FPGA_IN", "156M": "156M", "PLL_OUT": "PLL_OUT",
                 "PLL_IN_P2": "PLL_IN_P2", "PLL_IN_P1": "PLL_IN_P1", "PLL_IN_P0": "PLL_IN_P0",
                 "FMC0_GBTCLK": "FMC0_GBT", "FMC0_CLK": "FMC0_CLK",
                 "FMC1_CLK": "FMC1_CLK", "FMC1_GBTCLK": "FMC1_GBT"}
        for i in range(16):
            if i < 9:
                port, name = self.CLOCK_INPUTS[i]
            else:
                port, name = self.CLOCK_OUTPUTS[i - 9]
            cfg = self.clock_matrix[i]
            lab = "%s %s" % (port, short.get(name, name))
            if cfg & 0x80:
                src = cfg & 0x0F
                lab += "\n0x%02x \u2190 CLK%d" % (cfg, src)
                if cfg & 0x40:
                    lab += " term"
            else:
                lab += "\n" + ("0x%02x" % cfg)
            lines.append('    n%d [label="%s"];' % (i, lab))
        lines.append("  }")
        lines.append('  SI [label="Si5345\n0x69\nPLL 4:10"];')
        lines.append('  FMC0 [label="FMC0\n(bidir)"]; FMC1 [label="FMC1\n(bidir)"];')
        lines.append('  K7 [label="Kintex 7 (XC7K325T)\nREFCLK T26 (200M)\nSRCC R28 (156M)\nMRCC0 AD23 (100M)\nMRCC1 AB27 (125M WR)\nMRCC2 AG29 (20M VCXO)\nMRCC3 C25 (250M DDR)\nLEMO U27\nMGTCLK116 L8/N8\nMGTCLK117 G8/J8\nMGTCLK118 C8/E8"];')
        lines.append('  S7 [label="Spartan 7 (XC7S25)\nCLK_INOUT F14 (156M)\nCLK_IN_P0 N15 (100M)\nCLK_IN_P1 D14 (125M WR)\nFMC0_GTX G16 · FMC0_CLK D18\nFMC1_GTX R14 · FMC1_CLK P14\nFPGA_GTX U17"];')
        lines.append('  OTH [label="other clocks\n8 MHz → MCU\n200 MHz → K7 T26\nLEMO → K7 U27\nWR 125 MHz → K7 AB27 / S7 D14\nVCXO 20 MHz → K7 AG29", shape=note];')
        lines.append('  AMC -> {n0 n1 n2 n3 n4} [dir=both, color="#C9CFC9"];')
        lines.append('  OSC -> n7 [color="#C9CFC9"];')
        lines.append('  S7IN -> n5 [dir=both, color="#C9CFC9"];')
        lines.append('  FPGAIN -> n6 [dir=both, color="#C9CFC9"];')
        lines.append('  n9 -> SI [color="#C9CFC9"]; n10 -> SI [color="#C9CFC9"];')
        lines.append('  n11 -> SI [color="#C9CFC9"];')
        lines.append('  n12 -> FMC0 [dir=both, color="#C9CFC9"]; n13 -> FMC0 [dir=both, color="#C9CFC9"];')
        lines.append('  n14 -> FMC1 [dir=both, color="#C9CFC9"]; n15 -> FMC1 [dir=both, color="#C9CFC9"];')
        lines.append('  SI -> K7 [label="GTX_CLK0-3 → MGTCLK116/117/118"];')
        lines.append('  FMC0 -> S7 [label="FMC0_GTX G16 / FMC0_CLK D18"];')
        lines.append('  FMC1 -> S7 [label="FMC1_GTX R14 / FMC1_CLK P14"];')
        lines.append('  OTH -> K7 [style=dashed, color="#C9CFC9"];')
        lines.append('  OTH -> S7 [style=dashed, color="#C9CFC9"];')
        for j, (port, name) in enumerate(self.CLOCK_OUTPUTS):
            cfg = self.clock_matrix[9 + j]
            if cfg & 0x80 and (cfg & 0x0F) < len(self.CLOCK_INPUTS):
                lines.append('  n%d -> n%d [label="0x%02x", color="#0E6B5E", penwidth=2];'
                             % (cfg & 0x0F, 9 + j, cfg))
        lines.append("}")
        return "\n".join(lines) + "\n"

    # clocking block colours (as in uFCv2/readme/clocking.jpg)
    CLK_GREEN = "#C9DBA8"
    CLK_SALMON = "#E9C6C6"
    CLK_BLUE = "#AEC6E4"
    CLK_GREY = "#8A8A80"
    CLK_EDGE = "#5B6152"

    CLOCKING_TEMPLATES = {"uFC": "ufc", "u4FCP v1": "u4fcp-v1", "u4FCP v2": "u4fcp-v2"}

    def _clocking_path(self):
        import os
        name = self.CLOCKING_TEMPLATES.get(getattr(self, "template", "uFC"), "ufc")
        cands = ("%s-clocking.json" % name,
                 "clocking/%s-clocking.json" % name,
                 "../clocking/%s-clocking.json" % name,
                 "../../clocking/%s-clocking.json" % name,
                 "tools/clocking/%s-clocking.json" % name)
        bases = [os.path.dirname(os.path.abspath(__file__)), os.getcwd()]
        for cand in cands:
            for base in bases:
                path = os.path.abspath(os.path.join(base, cand))
                if os.path.exists(path):
                    return path
        return None

    def _load_clocking(self):
        import json
        path = self._clocking_path()
        if not path:
            return None
        try:
            with open(path) as f:
                return json.load(f)
        except Exception:
            return None

    def _clocking_draw(self, c, w, h):
        """Schematic-style drawing: inputs left, outputs right, jumped lines."""
        doc = self._load_clocking()
        if not doc or "devices" not in doc or not all(
                "box" in d for d in doc["devices"].values()):
            return self._clocking_draw_builtin(c, w, h)
        devices = doc["devices"]
        points = {p["id"]: p for p in doc.get("points", [])}
        ed = self.CLK_EDGE
        c.delete("all")

        # board box
        xs = [d["box"][0] for d in devices.values()]
        ys = [d["box"][1] for d in devices.values()]
        x2 = [d["box"][0] + d["box"][2] for d in devices.values()]
        y2 = [d["box"][1] + d["box"][3] for d in devices.values()]
        bx0, by0, bx1, by1 = min(xs) - 10, min(ys) - 30, max(x2) + 10, max(y2) + 10
        board = next(iter(doc.get("boards", {}).values()), {"label": "uFC"})
        c.create_rectangle(bx0, by0, bx1, by1, outline="#C9C2B2", dash=(4, 3))
        c.create_text(bx0 + 8, by0 + 10, text="board %s" % board.get("label", "uFC"),
                      anchor="w", font=(UI_FONT, 9, "bold"), fill=ed)

        fills = {"chip": self.CLK_BLUE, "oscillator": self.CLK_GREEN,
                 "connector": self.CLK_SALMON}

        # 1) size every box to fit its labels, then stack to avoid overlaps
        sized = {}
        for did, dev in devices.items():
            hx, hy, hw, hh = dev["box"]
            pts = [p for p in doc.get("points", []) if p.get("parent") == did]
            left = [p for p in pts if p["dir"] == "in"]
            right = [p for p in pts if p["dir"] in ("out", "inout")]
            lw = max([len(p["name"]) for p in left] or [0]) * 5.2
            rw = max([len(p["name"]) for p in right] or [0]) * 5.2
            bw = max(hw, 24 + lw + rw)
            bh = max(hh, 24 + max(len(left), len(right)) * 15)
            sized[did] = [hx, hy, bw, bh]
        placed = []
        for did in sorted(sized, key=lambda d: (sized[d][0], sized[d][1])):
            x, y, bw, bh = sized[did]
            moved = True
            while moved:
                moved = False
                for px, py, pw, ph in placed:
                    if x < px + pw + 8 and px < x + bw + 8 and \
                       y < py + ph + 8 and py < y + bh + 8:
                        y = py + ph + 10
                        moved = True
            sized[did] = [x, y, bw, bh]
            placed.append((x, y, bw, bh))
        for did, (x, y, bw, bh) in sized.items():
            devices[did]["box"] = [x, y, bw, bh]

        anchors = {}
        port_labels = []
        for did, dev in devices.items():
            x, y, bw, bh = dev["box"]
            c.create_rectangle(x, y, x + bw, y + bh,
                               fill=fills.get(dev.get("class"), "#FFFFFF"),
                               outline=ed, width=1)
            c.create_text(x + 4, y + 8, text=dev.get("label", did), anchor="w",
                          font=(UI_FONT, 8, "bold"), fill=ed)
            pts = [p for p in doc.get("points", []) if p.get("parent") == did]
            left = [p for p in pts if p["dir"] == "in"]
            right = [p for p in pts if p["dir"] in ("out", "inout")]

            rows = max(len(left), len(right))

            def row_y(i):
                return y + 18 + (bh - 26) * (i + 0.5) / max(1, rows)

            for i, p in enumerate(left):
                yy = row_y(i)
                anchors.setdefault(p["id"], {})["l"] = (x, yy)
                port_labels.append((x + 5, yy, p["name"], "w",
                                    fills.get(dev.get("class"), "#FFFFFF")))
            for i, p in enumerate(right):
                yy = row_y(i)
                anchors.setdefault(p["id"], {})["r"] = (x + bw, yy)
                port_labels.append((x + bw - 5, yy, p["name"], "e",
                                    fills.get(dev.get("class"), "#FFFFFF")))

        h_segs, v_segs = [], []
        for link in doc.get("links", []):
            a, b = link["a"], link["b"]
            if a not in anchors or b not in anchors:
                continue
            da = points.get(a, {}).get("dir")
            db = points.get(b, {}).get("dir")
            if da == "in" and db == "out":
                a, b, da, db = b, a, db, da
            sa = anchors[a].get("r") or anchors[a].get("l")
            dbp = anchors[b].get("l") or anchors[b].get("r")
            if not sa or not dbp:
                continue
            both = "inout" in (da, db)
            color = "#0E6B5E" if both else "#9A968C"
            dash = (3, 2) if link.get("verified") is False else None
            if sa[0] <= dbp[0]:
                mx = (sa[0] + dbp[0]) / 2
                pts = [sa, (mx, sa[1]), (mx, dbp[1]), dbp]
            else:  # backwards: route around the right
                mx = max(sa[0], dbp[0]) + 40
                pts = [sa, (mx, sa[1]), (mx, dbp[1]), dbp]
            for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
                if abs(y0 - y1) < 0.5:
                    h_segs.append((x0, y0, x1, y1, color, dash))
                else:
                    v_segs.append((x0, y0, x1, y1, color))
            c.create_line(*pts, fill=color, width=1, dash=dash,
                          arrow=None if both else tk.LAST,
                          arrowshape=(7, 9, 3))

        # jumper marks where a horizontal segment crosses a vertical one
        for hx0, hy, hx1, _, hcolor, _dash in h_segs:
            xa, xb = min(hx0, hx1), max(hx0, hx1)
            for vx, vy0, _vx1, vy1, _vc in v_segs:
                if xa + 3 < vx < xb - 3 and min(vy0, vy1) - 3 < hy < max(vy0, vy1) + 3:
                    c.create_oval(vx - 4, hy - 4, vx + 4, hy + 4,
                                  fill=BG, outline="")
                    c.create_arc(vx - 5, hy - 5, vx + 5, hy + 5, start=0,
                                 extent=180, style=tk.ARC, outline=hcolor)

        for lx, ly, text, anch, bg in port_labels:
            t = c.create_text(lx, ly, text=text, anchor=anch,
                              font=(UI_FONT, 7), fill=ed)
            c.create_rectangle(*c.bbox(t), fill=bg, outline="")
            c.tag_raise(t)

        for pid, route in doc.get("routing", {}).items():
            if route.get("source") and pid in anchors and route["source"] in anchors:
                sa = anchors[route["source"]].get("r") or anchors[route["source"]].get("l")
                db = anchors[pid].get("l") or anchors[pid].get("r")
                if sa and db:
                    mx = (sa[0] + db[0]) / 2
                    c.create_line(*sa, mx, sa[1], mx, db[1], *db,
                                  fill="#B07A3A", width=2, arrow=tk.LAST,
                                  arrowshape=(7, 9, 3))

        content = "8V54816A @0x70 ch2  ·  Si5345 @0x69  ·  %d active route(s)" % \
                  sum(1 for r in doc.get("routing", {}).values() if r.get("source"))
        c.create_text(w - 10, h - 10, text=content, anchor="se",
                      font=(UI_FONT, 8), fill=MUTED)

    def _clocking_draw_builtin(self, c, w, h):
        c.delete("all")
        g, sa, bl = self.CLK_GREEN, self.CLK_SALMON, self.CLK_BLUE
        ed = self.CLK_EDGE

        def block(x0, y0, x1, y1, text, fill, fs=9):
            c.create_rectangle(x0, y0, x1, y1, fill=fill, outline=ed, width=1)
            if text:
                c.create_text((x0 + x1) / 2, (y0 + y1) / 2, text=text,
                              font=(UI_FONT, fs, "bold"))

        def arrow(x0, y0, x1, y1, both=False, color=None, width=2, dash=None):
            c.create_line(x0, y0, x1, y1, fill=color or ed, width=width,
                          arrow=tk.BOTH if both else tk.LAST, arrowshape=(8, 10, 4),
                          dash=dash)

        def label(x, y, text, color=None, anchor="w", fs=8):
            c.create_text(x, y, text=text, anchor=anchor, font=(UI_FONT, fs),
                          fill=color or ed)

        # ---- left column ------------------------------------------------
        block(20, 60, 110, 240, "AMC", g)
        for i, name in enumerate(("TCLKA", "TCLKB", "TCLC", "TCLKD")):
            y = 80 + i * 30
            arrow(115, y, 198, y, both=True)
            label(120, y - 9, name, fs=8)
        arrow(115, 230, 198, 230, both=True)
        label(120, 221, "FCLKA", fs=8)

        block(20, 290, 120, 330, "156.25MHz", g, 8)
        arrow(125, 310, 198, 310)

        block(20, 360, 120, 396, "S7_INOUT", g, 8)
        arrow(125, 378, 198, 378, both=True)
        block(20, 410, 120, 446, "FPGA_INOUT", g, 8)
        arrow(125, 428, 198, 428, both=True)

        # ---- MUX with the 16 ports -------------------------------------
        block(200, 40, 470, 560, "", bl)
        c.create_text(335, 52, text="MUX  8V54816A  16", font=(UI_FONT, 9, "bold"))
        in_y = [80 + i * 52 for i in range(9)]
        out_y = [80 + j * 68 for j in range(7)]
        c.create_rectangle(206, 62, 464, 548, outline="", fill="")

        # crossbar pattern first, labels on top
        for i in range(9):
            for j in range(7):
                c.create_line(210, in_y[i], 460, out_y[j], fill="#C7CDBE", width=1)
        for j in range(7):
            cfg = self.clock_matrix[9 + j]
            if cfg & 0x80 and (cfg & 0x0F) < len(self.CLOCK_INPUTS):
                i = cfg & 0x0F
                c.create_line(210, in_y[i], 460, out_y[j], fill="#0E6B5E", width=3)
        for i, (port, name) in enumerate(self.CLOCK_INPUTS):
            t = c.create_text(212, in_y[i], text="%s %s" % (port, name), anchor="w",
                              font=(UI_FONT, 7), fill=ed)
            c.create_rectangle(*c.bbox(t), fill=bl, outline="")
            c.tag_raise(t)
        for j, (port, name) in enumerate(self.CLOCK_OUTPUTS):
            t = c.create_text(458, out_y[j], text="%s %s" % (port, name), anchor="e",
                              font=(UI_FONT, 7), fill=ed)
            c.create_rectangle(*c.bbox(t), fill=bl, outline="")
            c.tag_raise(t)
        for j in range(7):
            cfg = self.clock_matrix[9 + j]
            if cfg & 0x80 and (cfg & 0x0F) < len(self.CLOCK_INPUTS):
                i = cfg & 0x0F
                c.create_text(335, (in_y[i] + out_y[j]) / 2,
                              text="0x%02x" % cfg, font=(UI_FONT, 7), fill="#0E6B5E")

        # ---- right side blocks ------------------------------------------
        block(510, 60, 620, 130, "FMC0", sa)
        block(510, 150, 620, 220, "FMC1", sa)
        arrow(470, 95, 510, 95, both=True)
        arrow(510, 185, 470, 185, both=True)
        label(630, 100, "GBTCLK / M2C / C2M", fs=7)

        block(510, 270, 650, 400, "PLL  Si5345A\n4:10", bl, 9)
        arrow(470, 335, 510, 335)
        arrow(510, 315, 470, 315)

        block(820, 40, 970, 190, "FPGA\nSpartan 7", bl)
        arrow(650, 300, 820, 120)
        label(700, 205, "S7_CLK_0", fs=7)
        block(820, 230, 970, 460, "FPGA\nKintex 7", bl)
        arrow(650, 350, 820, 330)
        label(700, 385, "GTX_CLK0-3 / FPGA_CLK0", fs=7)

        block(990, 30, 1070, 70, "8MHz", g, 8)
        block(990, 90, 1070, 130, "MCU", sa, 8)
        arrow(1030, 70, 1030, 90)
        arrow(990, 110, 970, 110)

        block(990, 400, 1070, 440, "WR", bl, 8)
        block(990, 460, 1070, 496, "VCXO 20MHz", g, 8)
        arrow(1030, 460, 1030, 440)
        arrow(990, 420, 970, 420)
        arrow(1030, 400, 970, 160, dash=(3, 3))

        block(620, 500, 710, 536, "200MHz", g, 8)
        arrow(710, 518, 820, 430)
        block(620, 550, 710, 586, "LEMO_CLK_IN", g, 8)
        arrow(710, 568, 820, 450)

        # legend
        c.create_line(20, 600, 50, 600, fill="#0E6B5E", width=3)
        label(58, 600, "active route", fs=8)
        content = "8V54816A @0x70 ch2  ·  Si5345 @0x69  ·  %d active route(s)" % \
                  sum(1 for j in range(7) if (self.clock_matrix[9 + j] & 0x80))
        c.create_text(w - 10, h - 10, text=content, anchor="se", font=(UI_FONT, 8), fill=MUTED)

    def clock_show_system(self):
        win = tk.Toplevel(self)
        win.title("uFC clocking — system view")
        bar = ttk.Frame(win, padding=(8, 6))
        bar.pack(fill="x")
        ttk.Button(bar, text="Refresh", command=lambda: draw()).pack(side="left")
        ttk.Label(bar, text="block diagram of the uFC clocking (reference: uFCv2/readme/clocking.jpg)",
                  style="Hint.TLabel").pack(side="left", padx=(12, 0))
        cv = tk.Canvas(win, width=1080, height=620, bg=BG, highlightthickness=0)
        cv.pack(fill="both", expand=True, padx=8, pady=(0, 8))

        def draw():
            self._clocking_draw(cv, 1080, 620)

        draw()

    def _clock_log(self, text):
        self.clock_out.configure(state="normal")
        self.clock_out.insert("end", text + "\n")
        self.clock_out.see("end")
        self.clock_out.configure(state="disabled")

    def clock_pll_addr(self):
        plls = (self.clock_cfg or {}).get("plls") or [{"addr": 0x69}]
        idx = 0
        try:
            idx = int(self.si_pll.get().replace("PLL", ""))
        except Exception:
            idx = 0
        if idx >= len(plls):
            idx = 0
        return int(plls[idx].get("addr", 0x69))

    def clock_switch_addr(self):
        return int(((self.clock_cfg or {}).get("switch") or {}).get("addr", 0x58))

    def si_load(self):
        path = filedialog.askopenfilename(
            title="ClockBuilder register script",
            filetypes=[("ClockBuilder text", "*.txt"), ("All files", "*")])
        if not path:
            return
        try:
            self.si_groups = parse_si5345_txt(path)
            self.si_tables[self.clock_pll_addr()] = self.si_groups
            n = sum(len(v) for v in self.si_groups.values())
            self.si_label.set("%s  (%d pre, %d body, %d post)" % (
                os.path.basename(path), len(self.si_groups["pre"]),
                len(self.si_groups["body"]), len(self.si_groups["post"])))
            self._clock_log("Si5345 file loaded: %s — %d registers" % (path, n))
        except Exception as e:
            self._clock_log("ERROR: %s" % e)

    def si_write_device(self):
        """Write the loaded Si5345 table to the device RAM (immediate, volatile)."""
        if not self.si_groups:
            self._clock_log("ERROR: load a Si5345 register file first")
            return
        try:
            def progress(n):
                if n % 64 == 0:
                    self._clock_log("Si5345: %d registers written" % n)
                    self.update_idletasks()
            n = program_si5345(self._access(), self.si_groups,
                               dev=self.clock_pll_addr(), progress=progress)
            self._clock_log("Si5345 programmed to the device: %d registers "
                            "(preamble, 300 ms, body, postamble) — lost on power-cycle" % n)
            self._status("Si5345 programmed (device)", "ok")
        except Exception as e:
            self._status("Si5345 write failed", "error")
            self._clock_log("ERROR: %s" % e)

    def pll_lock_status(self):
        """Read 0x0C00 (live) / 0x0C01 (sticky) of both Si5345s and show the lock state.

        Bits: [0] SYSINCAL, [1] LOSXAXB, [2] LOL, [3] LOS, [4] OOF, [5] HOLD.
        """
        try:
            acc = self._access()
            mux = [(0x70, 2)]
            out = []
            for dev, name in ((0x68, "PLL0"), (0x69, "PLL1")):
                try:
                    acc.xfer(1, mux, dev, bytes([0x01, 0x0C]), 0)   # page 0x0C
                    live = acc.xfer(1, mux, dev, bytes([0x00]), 1)[0]
                    acc.xfer(1, mux, dev, bytes([0x01, 0x0C]), 0)
                    sticky = acc.xfer(1, mux, dev, bytes([0x01]), 1)[0]
                    ok = (live & (1 << 2)) == 0
                    out.append("%s: %s (live=0x%02X sticky=0x%02X)"
                               % (name, "locked" if ok else "NOT locked", live, sticky))
                except Exception:
                    out.append("%s: read error" % name)
            self.lock_var.set("    ".join(out))
        except Exception as e:
            messagebox.showerror(APP_TITLE, "PLL status read failed:\n%s" % e)

    def si_read_device(self):
        """Read the loaded register list from the Si5345 and save it as a script."""
        groups = self.si_groups
        if not groups:
            groups = default_si5345_groups()
            self._clock_log("no register file loaded — reading the default Si5345 register set")
        path = filedialog.asksaveasfilename(
            title="Save the Si5345 register dump",
            defaultextension=".txt",
            initialfile="Si5345-dump.txt",
            filetypes=[("ClockBuilder text", "*.txt")])
        if not path:
            return
        try:
            def progress(n):
                if n % 64 == 0:
                    self._clock_log("Si5345: %d registers read" % n)
                    self.update_idletasks()
            vals = read_si5345(self._access(), groups, progress=progress)
            lines = ["# Si534x/7x/8x/9x Registers Script",
                     "# Part: Si5345",
                     "# Dumped from the device by uFC Toolbox",
                     "Address,Data"]
            for section, title in (("pre", "configuration preamble"),
                                   ("body", "configuration registers"),
                                   ("post", "configuration postamble")):
                lines.append("# Start %s" % title)
                lines += ["0x%04X,0x%02X" % (a, v) for a, v in vals[section]]
                lines.append("# End %s" % title)
            open(path, "w").write("\n".join(lines) + "\n")
            n = sum(len(v) for v in vals.values())
            self._clock_log("Si5345: %d registers read, saved to %s" % (n, path))
            self._status("Si5345 dump saved", "ok")
        except Exception as e:
            self._status("Si5345 read failed", "error")
            self._clock_log("ERROR: %s" % e)

    def si_write_flash(self):
        """Store the current matrix + loaded Si5345 table in the MMC clock store."""
        try:
            tables = list(self.si_tables.items()) or []
            slot = build_clock_slot(self.clock_matrix, self.si_groups,
                                    switch_addr=self.clock_switch_addr(),
                                    si_tables=tables or None)
            self._clock_log("clock slot: %d bytes, crc %08x" %
                            (len(slot), zlib.crc32(slot) & 0xFFFFFFFF))
            slot_index = {"auto": 0xFF, "A": 0, "B": 1}.get(self.slot_sel.get(), 0xFF)
            r = upload_clock_slot(self._access(), slot, slot_index,
                                  progress=lambda n: self._clock_log("flash: %d bytes" % n))
            self._clock_log("slot uploaded, finish: %s — stored; reboot the MMC to apply it"
                            % r.hex())
            self._status("Clock config written to flash", "ok")
        except Exception as e:
            self._status("Flash write failed", "error")
            self._clock_log("ERROR: %s" % e)

    def store_apply(self):
        try:
            r = self._access().raw("0x32", "0x19")
            self._clock_log("store apply: %s" % r.hex())
        except Exception as e:
            self._clock_log("ERROR: %s" % e)

    def store_status(self):
        try:
            r = self._access().raw("0x32", "0x18")
            if len(r) >= 8 and r[0]:
                crc = r[4] | (r[5] << 8) | (r[6] << 16) | (r[7] << 24)
                self._clock_log("store: valid, slot %s, %d bytes, crc %08x" %
                                ("AB"[r[1] & 1], r[2] | (r[3] << 8), crc))
            else:
                self._clock_log("store: empty")
        except Exception as e:
            self._clock_log("ERROR: %s" % e)

    def clock_export_dot(self):
        try:
            path = filedialog.asksaveasfilename(defaultextension=".dot",
                                                filetypes=[("Graphviz DOT", "*.dot")])
            if not path:
                return
            open(path, "w").write(self._clock_system_dot())
            self._clock_log("DOT written: " + path)
        except Exception as e:
            self._clock_log("ERROR: %s" % e)

    def clock_switch_read(self):
        try:
            data = self._access().clock_switch_read()
            self.clock_matrix = bytearray(data)
            self._port_load_all()
            self._clock_log("switch matrix: " + " ".join("%02x" % b for b in self.clock_matrix))
        except Exception as e:
            self._clock_log("ERROR: %s" % e)

    def clock_switch_write(self):
        try:
            self._access().clock_switch_write(bytes(self.clock_matrix))
            self._clock_log("switch matrix written: " + " ".join("%02x" % b for b in self.clock_matrix))
            self._status("Clock switch written", "ok")
        except Exception as e:
            self._status("Clock switch write failed", "error")
            self._clock_log("ERROR: %s" % e)

    def clock_program(self):
        try:
            self._access().clock_program()
            self._clock_log("program command accepted — waiting for the status")
            self.after(1500, self.clock_status)
        except Exception as e:
            self._status("Clock programming failed", "error")
            self._clock_log("ERROR: %s" % e)

    def clock_status(self):
        try:
            d = self._access().clock_status()
            names = {0: "idle", 1: "running", 2: "OK", 3: "error"}
            state = d[0] if d else 0
            fail = (d[1] << 8 | d[2]) if len(d) >= 3 else 0
            text = "status: %s" % names.get(state, str(state))
            if state == 3:
                text += " (failed at register 0x%04X)" % fail
            self._clock_log(text)
            self._status("Clock: " + text.split(":", 1)[1].strip(),
                         "ok" if state == 2 else ("error" if state == 3 else "idle"))
        except Exception as e:
            self._status("Clock status failed", "error")
            self._clock_log("ERROR: %s" % e)

    def _build_ota_tab(self, parent):
        self.ota_proc = None
        self.ota_queue = queue.Queue()
        self.ota_progress_re = re.compile(r"(\d{1,3})%")

        row = ttk.Frame(parent)
        row.pack(fill="x", pady=(0, 8))
        ttk.Label(row, text="Firmware file (HPM / BIN / HEX)", style="FieldLabel.TLabel").pack(side="left")
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
        row3.pack(fill="x", pady=(0, 4))
        self.ota_buttons = []
        tips = {
            "Upload": "ipmitool 'hpm upgrade' (HPM.1 image).\n"
                      "Large blocks - use it when the target is reached directly\n"
                      "(KCS, native IPMB); .bin/.hex need 'bin2hpm' in PATH.",
            "Upload (bridge)": "In-tool HPM upload with 20-byte blocks.\n"
                               "This is the path that works through the MCH\n"
                               "IPMB bridge; takes a raw .bin/.hex file.",
            "Activate": "Activate the staged image: the MMC writes fw_info\n"
                        "and resets; the bootloader copies the image.",
            "Upload + Activate": "ipmitool upload (.hpm) followed by activate.",
        }
        for text, cmd in (("Upload", self._ota_upload),
                          ("Upload (bridge)", self._ota_upload_raw),
                          ("Activate", self._ota_activate),
                          ("Upload + Activate", self._ota_upload_activate)):
            b = ttk.Button(row3, text=text, command=cmd)
            b.pack(side="left", padx=(0, 8))
            Tooltip(b, tips.get(text, ""))
            self.ota_buttons.append(b)

        ttk.Label(parent,
                  text="Upload = ipmitool HPM.1 (.hpm) · Upload (bridge) = in-tool HPM,"
                       " 20-byte IPMB blocks, .bin/.hex",
                  style="Hint.TLabel").pack(anchor="w", pady=(0, 8))

        self.ota_pbar = ttk.Progressbar(parent, orient="horizontal", mode="determinate",
                                        maximum=100)
        self.ota_pbar.pack(fill="x", pady=(0, 8))

        ttk.Label(parent, text="ipmitool output", style="Hint.TLabel").pack(anchor="w")
        self.ota_out = tk.Text(parent, height=14, font=(MONO_FONT, 8), state="disabled",
                               bg=PANEL, fg=INK, relief="flat", highlightthickness=1,
                               highlightbackground=LINE, padx=8, pady=6)
        self.ota_out.pack(fill="both", expand=True)

    @staticmethod
    def _parse_intel_hex(text):
        """Return the raw image of an Intel HEX file (lowest address first)."""
        mem = {}
        base = 0
        for line in text.splitlines():
            line = line.strip()
            if not line.startswith(":"):
                continue
            try:
                rec = bytes.fromhex(line[1:])
            except ValueError:
                continue
            if len(rec) < 5:
                continue
            count, addr, rtype = rec[0], (rec[1] << 8) | rec[2], rec[3]
            data = rec[4:4 + count]
            if rtype == 0x00:
                for i, b in enumerate(data):
                    mem[base + addr + i] = b
            elif rtype == 0x01:
                break
            elif rtype == 0x02:
                base = ((data[0] << 8) | data[1]) << 4
            elif rtype == 0x04:
                base = ((data[0] << 8) | data[1]) << 16
        if not mem:
            raise ValueError("no data records in the HEX file")
        lo, hi = min(mem), max(mem)
        out = bytearray(b"\xff") * (hi - lo + 1)
        for a, b in mem.items():
            out[a - lo] = b
        return bytes(out)

    def _ota_resolve_image(self):
        """Return an .hpm path for the selected file (.hpm/.bin/.hex)."""
        path = self.ota_file.get().strip()
        if not path or not os.path.isfile(path):
            raise ValueError("choose an .hpm, .bin or .hex file first")
        ext = os.path.splitext(path)[1].lower()
        if ext == ".hpm":
            return path

        tool = shutil.which("bin2hpm")
        if not tool:
            raise ValueError("bin2hpm not found in PATH (needed for .bin/.hex input)")

        workdir = tempfile.mkdtemp(prefix="ufc-ota-")
        if ext == ".hex":
            with open(path, "r", errors="replace") as f:
                raw = self._parse_intel_hex(f.read())
            src = os.path.join(workdir, "image.bin")
            with open(src, "wb") as f:
                f.write(raw)
            self._ota_log("HEX parsed: %d bytes -> %s\n" % (len(raw), src))
        else:
            src = path

        out = os.path.join(workdir, "image.hpm")
        comp = self.ota_comp.get().strip() or "1"
        cmd = [tool, "-c", comp, "-n", "-m", "0x315A", "-p", "0x0", "-d", "0",
               src, "-o", out]
        self._ota_log("$ %s\n" % " ".join(cmd))
        res = subprocess.run(cmd, capture_output=True, text=True)
        if res.returncode != 0 or not os.path.isfile(out):
            raise ValueError("bin2hpm failed:\n%s" % (res.stderr or res.stdout))
        self._ota_log("HPM built: %s (%d bytes)\n" % (out, os.path.getsize(out)))
        return out

    def _ota_browse(self):
        path = filedialog.askopenfilename(
            filetypes=[("Firmware (HPM/BIN/HEX)", "*.hpm *.bin *.hex"),
                       ("HPM image", "*.hpm"), ("Raw binary", "*.bin"),
                       ("Intel HEX", "*.hex"), ("All files", "*.*")])
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

    def _ota_upload_raw(self):
        """Upload a raw .bin/.hex image with the in-tool HPM sequence.

        ipmitool's hpm upgrade cannot be used through the MCH bridge (it sizes
        the blocks from the LAN interface); this path sends 20-byte blocks."""
        path = self.ota_file.get().strip()
        if not path or not os.path.isfile(path):
            messagebox.showerror(APP_TITLE, "choose a .bin or .hex file first")
            return
        ext = os.path.splitext(path)[1].lower()
        try:
            if ext == ".hex":
                with open(path, "r", errors="replace") as f:
                    data = self._parse_intel_hex(f.read())
            elif ext == ".bin":
                with open(path, "rb") as f:
                    data = f.read()
            else:
                messagebox.showerror(APP_TITLE, "use a .bin/.hex file (or the ipmitool 'Upload' for .hpm)")
                return
            acc = self._access()
        except Exception as e:
            messagebox.showerror(APP_TITLE, str(e))
            return
        comp = int(self.ota_comp.get().strip() or "1")
        self._ota_log("raw HPM upload: %s (%d bytes), component %d\n" % (path, len(data), comp))
        self.ota_pbar.configure(value=0)
        self._ota_busy(True)

        def worker():
            try:
                def progress(done, total):
                    self.ota_queue.put(("progress", int(done * 100 / total)))
                done, fin = acc.hpm_upload(data, comp, progress)
                self.ota_queue.put(("log", "uploaded %d bytes, finish response %s\n"
                                    % (done, fin.hex())))
                self.ota_queue.put(("done_raw", 0))
            except Exception as e:
                self.ota_queue.put(("log", "ERROR: %s\n" % e))
                self.ota_queue.put(("done_raw", 1))
        threading.Thread(target=worker, daemon=True).start()
        self.after(150, self._ota_poll_raw)

    def _ota_poll_raw(self):
        busy = True
        while True:
            try:
                kind, value = self.ota_queue.get_nowait()
            except queue.Empty:
                break
            if kind == "progress":
                self.ota_pbar.configure(value=value)
            elif kind == "log":
                self._ota_log(value)
            elif kind == "done_raw":
                busy = False
                self._ota_busy(False)
                self.ota_pbar.configure(value=100 if value == 0 else 0)
                self._status("Firmware uploaded" if value == 0 else "Firmware upload failed",
                             "ok" if value == 0 else "error")
        if busy:
            self.after(150, self._ota_poll_raw)

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
        try:
            image = self._ota_resolve_image()
        except Exception as e:
            messagebox.showerror(APP_TITLE, str(e))
            return
        self._ota_run(["upgrade", image, "-C", self.ota_comp.get().strip()])

    def _ota_activate(self):
        if not messagebox.askyesno(APP_TITLE,
                                    "Activate the staged image? The MMC will reset."):
            return
        self._ota_run(["activate"], timeout=25)

    def _ota_upload_activate(self):
        try:
            image = self._ota_resolve_image()
        except Exception as e:
            messagebox.showerror(APP_TITLE, str(e))
            return
        if not messagebox.askyesno(APP_TITLE,
                                    "Upload and activate %s?\nThe MMC will reset after the upload."
                                    % os.path.basename(image)):
            return
        self._ota_run(["upgrade", image, "-C", self.ota_comp.get().strip()],
                      next_args=["activate"])

    def _build_i2c_tab(self, parent):
        self.tpl_info_var = tk.StringVar(
            value="board: %s (change it in the header)" % getattr(self, "template", "uFC"))
        ttk.Label(parent, textvariable=self.tpl_info_var,
                  style="Hint.TLabel").pack(anchor="w", pady=(0, 4))
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
        self.i2c_wlen = tk.StringVar(value="0")
        ttk.Entry(row, textvariable=self.i2c_wlen, width=4).pack(side="left", padx=(6, 16))

        ttk.Label(row, text="Read len", style="FieldLabel.TLabel").pack(side="left")
        self.i2c_rlen = tk.StringVar(value="8")
        ttk.Entry(row, textvariable=self.i2c_rlen, width=4).pack(side="left", padx=(6, 16))

        ttk.Button(row, text="Transfer",
                   command=self.i2c_transfer).pack(side="left")

        datarow = ttk.Frame(parent)
        datarow.pack(fill="x", pady=(0, 8))
        ttk.Label(datarow, text="Write data (hex)", style="FieldLabel.TLabel").pack(side="left")
        self.i2c_wdata = tk.StringVar(value="")
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
        for label, fn, tip in (
                ("EEPROM FRU", self._preset_eeprom,
                 "FRU EEPROM at the board template's address;\nread 8 bytes"),
                ("UCD PMBus", self._preset_pmbus,
                 "UCD90xxx @0x11; needs the PMBus command (0x8B) in Write data"),
                ("Clock switch", self._preset_clock,
                 "crosspoint at the template's address; needs its register byte"),
                ("Si5345", self._preset_si5345,
                 "PLL at the template's address; needs its register byte")):
            b = ttk.Button(presets, text=label, command=fn)
            b.pack(side="left", padx=(0, 6))
            Tooltip(b, tip)
        self.i2c_preset_hint = tk.StringVar(value="")
        tk.Label(parent, textvariable=self.i2c_preset_hint, bg=BG, fg=MUTED,
                 font=(UI_FONT, 8), anchor="w").pack(anchor="w")

        # FMC VADJ: TPL0102-100PWR digital pot (0x56, power bus mux ch7)
        vadj = ttk.Frame(parent)
        vadj.pack(fill="x", pady=(6, 4))
        ttk.Label(vadj, text="FMC VADJ", style="FieldLabel.TLabel").pack(side="left", padx=(0, 8))
        ttk.Label(vadj, text="FMC0 (V)", style="Hint.TLabel").pack(side="left")
        self.vadj0_var = tk.StringVar(value="1.8")
        e0 = ttk.Entry(vadj, textvariable=self.vadj0_var, width=6)
        e0.pack(side="left", padx=(4, 12))
        ttk.Label(vadj, text="FMC1 (V)", style="Hint.TLabel").pack(side="left")
        self.vadj1_var = tk.StringVar(value="1.8")
        e1 = ttk.Entry(vadj, textvariable=self.vadj1_var, width=6)
        e1.pack(side="left", padx=(4, 12))
        br = ttk.Button(vadj, text="Read", command=self._vadj_read)
        br.pack(side="left", padx=(0, 8))
        bs = ttk.Button(vadj, text="Set (persist)", command=self._vadj_set)
        bs.pack(side="left")
        tip = ("FMC0/FMC1 VADJ, trimmed by the TPL0102-100PWR (I2C 0x56 on the\n"
               "power bus, mux 0x70 channel 7). Range 0.9-1.8 V; the value is\n"
               "stored in the TPL0102 non-volatile register and survives power cycles.")
        for w in (br, bs, e0, e1):
            Tooltip(w, tip)
        self.i2c_vadj_hint = tk.StringVar(value="")
        tk.Label(parent, textvariable=self.i2c_vadj_hint, bg=BG, fg=MUTED,
                 font=(UI_FONT, 8), anchor="w").pack(anchor="w")

        # JTAG selection: TCA9554 @0x39 on the FPGA bus (mux 0x70 ch4)
        jtag = ttk.Frame(parent)
        jtag.pack(fill="x", pady=(6, 4))
        ttk.Label(jtag, text="JTAG", style="FieldLabel.TLabel").pack(side="left", padx=(0, 8))
        self.jtag_vars = {}
        for name in ("AMC", "RTM", "FMC0", "FMC1"):
            var = tk.BooleanVar(value=False)
            cb = ttk.Checkbutton(jtag, text=name, variable=var)
            cb.pack(side="left", padx=(0, 10))
            self._hint(cb, "TCA9554 output bit driving the JTAG chain select (%s)" % name)
            self.jtag_vars[name] = var
        jr = ttk.Button(jtag, text="Read JTAG", command=self.jtag_read)
        jr.pack(side="left", padx=(6, 0))
        self._hint(jr, "Read the TCA9554 output/config registers (0x39 on mux ch4) "
                       "and tick the enabled JTAG targets")
        ja = ttk.Button(jtag, text="Apply JTAG", command=self.jtag_apply)
        ja.pack(side="left", padx=(6, 0))
        self._hint(ja, "Program the TCA9554 with the ticked JTAG selection "
                       "(config 0x0F, output register 0x01)")
        self.i2c_jtag_hint = tk.StringVar(value="")
        tk.Label(parent, textvariable=self.i2c_jtag_hint, bg=BG, fg=MUTED,
                 font=(UI_FONT, 8), anchor="w").pack(anchor="w")

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

    def _preset(self, bus, muxes, dev, need_byte, rlen, note=""):
        """Fill the transfer fields; the write stays empty (read only).

        need_byte is the register/command byte the chip expects before a
        read - shown as a hint, so it can be typed into 'Write data' (with
        write length 1) when needed."""
        self.i2c_bus.set("I2C2" if bus == 1 else "I2C1")
        for i, (a, c) in enumerate(self.mux_vars):
            if i < len(muxes):
                a.set("0x%02x" % muxes[i][0]); c.set(str(muxes[i][1]))
            else:
                a.set(""); c.set("")
        self.i2c_dev.set("0x%02x" % dev)
        self.i2c_wdata.set("")
        self.i2c_wlen.set("0")
        self.i2c_rlen.set(str(rlen))
        if hasattr(self, "i2c_preset_hint"):
            if need_byte is None:
                self.i2c_preset_hint.set("")
            else:
                self.i2c_preset_hint.set(
                    "preset needs write %02x with write len 1%s"
                    % (need_byte, (": " + note) if note else ""))

    def _preset_eeprom(self):
        self._preset(1, [(0x70, 0)], self._i2c_addr(), 0x00, 8, "EEPROM word address")

    def _preset_pmbus(self):
        self._preset(1, [(0x70, 0)], 0x11, 0x8B, 2, "PMBus command")

    def _preset_clock(self):
        self._preset(1, [(0x70, 2)], self.clock_switch_addr(), 0x00, 1,
                     "crosspoint register")

    def _preset_si5345(self):
        self._preset(1, [(0x70, 2)], self.clock_pll_addr(), 0x00, 1,
                     "Si5345 register")

    # ---- FMC VADJ (TPL0102) -------------------------------------------------
    @staticmethod
    def _vadj_dac(volts):
        volts = max(0.9, min(1.8, float(volts)))
        return max(0, min(255, int((60.4 / (volts / 0.6 - 1.0) - 30.2) * 256 / 100)))

    @staticmethod
    def _vadj_volts(dac):
        return 0.6 * (60.4 / (30.2 + dac * 100.0 / 256.0) + 1.0)

    def _vadj_reg(self, access, reg, val=None):
        mux = [(0x70, 7)]
        if val is None:
            return access.xfer(1, mux, 0x56, bytes([reg]), 1)[0]
        access.xfer(1, mux, 0x56, bytes([reg, val]), 0)
        return None

    # ---- JTAG select (TCA9554 @0x39, mux 0x70 ch4) -------------------------
    JTAG_DEV = 0x39
    JTAG_MUX = [(0x70, 4)]
    JTAG_BITS = {"AMC": 0x10, "FMC0": 0x20, "FMC1": 0x40, "RTM": 0x80}
    # AMC is active high; RTM/FMC0/FMC1 are *_EN_B (active low)
    JTAG_ACTIVE_LOW = ("RTM", "FMC0", "FMC1")

    def _jtag_read_reg(self, reg):
        acc = self._access()
        return acc.xfer(1, self.JTAG_MUX, self.JTAG_DEV, bytes([reg]), 1)[0]

    def _jtag_write_reg(self, reg, val):
        acc = self._access()
        acc.xfer(1, self.JTAG_MUX, self.JTAG_DEV, bytes([reg, val]), 0)

    def _jtag_decode(self, out):
        res = {}
        for name, bit in self.JTAG_BITS.items():
            on = bool(out & bit)
            if name in self.JTAG_ACTIVE_LOW:
                on = not on
            res[name] = on
        return res

    @staticmethod
    def _jtag_encode(vars_):
        out = 0
        for name, bit in FruEditor.JTAG_BITS.items():
            on = vars_[name].get()
            if name in FruEditor.JTAG_ACTIVE_LOW:
                on = not on
            if on:
                out |= bit
        return out

    def jtag_read(self):
        try:
            out = self._jtag_read_reg(0x01)
            cfg = self._jtag_read_reg(0x03)
            st = self._jtag_decode(out)
            for name, val in st.items():
                self.jtag_vars[name].set(val)
            self.i2c_jtag_hint.set(
                "output=0x%02X config=0x%02X -> AMC=%d RTM=%d FMC0=%d FMC1=%d"
                % (out, cfg, st["AMC"], st["RTM"], st["FMC0"], st["FMC1"]))
        except Exception as e:
            messagebox.showerror(APP_TITLE, "JTAG read failed:\n%s" % e)

    def jtag_apply(self):
        try:
            out = self._jtag_encode(self.jtag_vars)
            self._jtag_write_reg(0x03, 0x0F)   # P7..P4 outputs, P3..P0 inputs
            self._jtag_write_reg(0x01, out)
            self.jtag_read()
            self.i2c_jtag_hint.set("written, read back: " + self.i2c_jtag_hint.get())
        except Exception as e:
            messagebox.showerror(APP_TITLE, "JTAG write failed:\n%s" % e)

    def _vadj_read(self):
        try:
            access = self._access()
            out = []
            for chn, var, name in ((0, self.vadj0_var, "FMC0"),
                                   (1, self.vadj1_var, "FMC1")):
                self._vadj_reg(access, 0x10, 0xC0)       # volatile (WR) access
                dac = self._vadj_reg(access, chn)
                var.set("%.3f" % self._vadj_volts(dac))
                out.append("%s %d = %.3f V" % (name, dac, self._vadj_volts(dac)))
            self.i2c_vadj_hint.set("read: " + "   ".join(out))
        except Exception as e:
            messagebox.showerror(APP_TITLE, "VADJ read failed:\n%s" % e)

    def _vadj_set(self):
        try:
            access = self._access()
            for chn, var in ((0, self.vadj0_var), (1, self.vadj1_var)):
                dac = self._vadj_dac(float(var.get()))
                self._vadj_reg(access, 0x10, 0x40)       # non-volatile (IVR) access
                self._vadj_reg(access, chn, dac)
                self._vadj_reg(access, 0x10, 0xC0)       # back to the WR access
            self._vadj_read()
            self.i2c_vadj_hint.set("written to the TPL0102 non-volatile register: "
                                   + self.i2c_vadj_hint.get())
        except Exception as e:
            messagebox.showerror(APP_TITLE, "VADJ write failed:\n%s" % e)

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
            ttk.Label(parent, text="Module current (A)",
                      style="FieldLabel.TLabel").grid(
                row=row, column=0, sticky="e", padx=(0, 10), pady=1)
            cell = ttk.Frame(parent)
            cell.grid(row=row, column=1, sticky="w", pady=1)
            self.current_var = tk.StringVar()
            self.current_ent = ttk.Entry(cell, textvariable=self.current_var, width=8)
            self.current_ent.pack(side="left")
            self.current_hint = ttk.Label(cell, text="", style="Hint.TLabel")
            self.current_hint.pack(side="left", padx=(8, 0))
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

    def _on_amc_change(self):
        """Derive the bridge addressing from the AMC slot (crate convention)."""
        try:
            slot = int(str(self.amc_var.get()).upper().replace("AMC", "").strip())
        except ValueError:
            return
        if not 1 <= slot <= 12:
            return
        self.transit_var.set("0x82")
        self.tch_var.set("0")
        self.target_var.set("0x%02x" % (0x70 + 2 * slot))
        self.bus_var.set("0")
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
                slot = str(self.amc_var.get()).upper().replace("AMC", "").strip()
                fru = ""
                try:
                    fru = " · FRU %d" % (int(slot) + 4)
                except ValueError:
                    pass
                text = "AMC%s%s · LAN → %s → transit %s (ch %s) → target %s (bus %s)" % (
                    slot, fru, self.mch_var.get(), self.transit_var.get(),
                    self.tch_var.get(), self.target_var.get(), self.bus_var.get())
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
        free = limit - used
        free_txt = ("%d B free" % free) if free >= 0 else (
            "image %d B > writable %d B" % (used, limit))
        return "%d B used · %s · multirecord %d B · checksums %s · mfg %s" % (
            used, free_txt, multi, "OK" if ok else "BAD",
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
        mc = parsed.get("module_current")
        if getattr(self, "current_var", None) is not None:
            if mc:
                self.current_var.set("%.1f" % (mc["current"] / 10.0))
                self.current_ent.configure(state="normal")
                self.current_hint.configure(text="PICMG record 0x16 (0.1 A units)")
            else:
                self.current_var.set("")
                self.current_ent.configure(state="disabled")
                self.current_hint.configure(text="no module-current record")
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
        mc = (self.parsed or {}).get("module_current")
        if mc and str(self.current_ent.cget("state")) != "disabled":
            try:
                amps = float(self.current_var.get())
            except ValueError:
                messagebox.showerror(APP_TITLE, "Module current must be a number in A (e.g. 8.0).")
                return
            values["module_current"] = max(0, min(255, int(round(amps * 10))))
        try:
            _size, limit = self._sizes()
            image = build_image(self.parsed, values, limit=_size)
            if len(image) > limit:
                needed = len(image)
                if not messagebox.askyesno(
                        APP_TITLE,
                        "The FRU image is %d B but the writable limit is %d B.\n"
                        "Raise the limit to %d B and write?" % (needed, limit, needed)):
                    return
                limit = needed
                self.limit_var.set(str(limit))
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
