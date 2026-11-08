#!/usr/bin/env python3
"""
fru_gui_tk.py -- desktop FRU editor for the uFC MMC (Tkinter).

Reads/writes the FRU EEPROM through a NAT-MCH (IPMI raw commands 0x32/0x12,
0x13), edits the board/product fields, saves or loads the raw image.
"""

import argparse
import datetime
import os
import sys
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fru_core import (FruAccess, parse_fru, build_image, image_for_eeprom,  # noqa: E402
                      EEPROM_SIZE, FRU_MAX_SIZE, FRU_MAX_STRING,
                      mfg_minutes_to_str, mfg_str_to_minutes)

APP_TITLE = "uFC FRU editor"

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
        self.geometry("1020x930")
        self.minsize(940, 700)
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
        self.size_var.trace_add("write", lambda *_: self._update_address())
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
        ttk.Label(title, text="24AA025E48  ·  AMC7  ·  IPMI raw access",
                  style="Sub.TLabel").pack(anchor="w")

        actions = ttk.Frame(header)
        actions.pack(side="right")
        ttk.Button(actions, text="Write FRU", style="TButton",
                   command=self.write_fru).pack(side="right", padx=(8, 0))
        ttk.Button(actions, text="Read FRU", style="Primary.TButton",
                   command=self.read_fru).pack(side="right")

        ttk.Separator(self).pack(fill="x", padx=16)

        # connection / size strip
        strip = ttk.Frame(self, padding=(16, 10, 16, 6))
        strip.pack(fill="x")
        self.mch_var = self._labeled(
            strip, "MCH", mch, 14,
            tip="Address of the NAT-MCH crate manager\n(IPMI over LAN, this host's entry point).")
        self.transit_var = self._labeled(
            strip, "Transit", transit, 6,
            tip="First bridge hop: the intermediate IPMI controller\n(NAT-MCH MCMC) that owns the AMC buses.")
        self.tch_var = self._labeled(
            strip, "Transit ch", "0", 4,
            tip="Channel from the MCH to the transit bridge (usually 0).")
        self.target_var = self._labeled(
            strip, "Target", target, 6,
            tip="Final device that answers: the AMC7 MMC's IPMB address (0x7E).")
        self.bus_var = self._labeled(
            strip, "Bus", str(bus), 4,
            tip="IPMB channel on the transit that reaches the target\n(bus 7 = AMC7 slot).")
        self.size_var = self._labeled(
            strip, "EEPROM size (B)", str(eeprom_size), 7,
            tip="Bytes read from the EEPROM. The device sits at I2C 0x50 on\nthe FRU bus (fixed in firmware); the tool addresses it by\nbyte offset (16-bit, so 65536 bytes is the maximum).")
        self.limit_var = self._labeled(
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
        ttk.Button(bottom, text="Save image…", command=self.save_image).pack(side="left")
        ttk.Button(bottom, text="Load image to EEPROM",
                   command=self.load_image).pack(side="left", padx=(8, 0))
        ttk.Button(bottom, text="Exit", command=self.destroy).pack(side="right")

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
        return var

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
    def _update_address(self):
        try:
            size = int(self.size_var.get().strip())
        except ValueError:
            size = 0
        self.addr_var.set(
            "EEPROM at I2C 0x50 on the FRU bus (mux ch 0) — addressed by byte offset, "
            "0x0000–0x%04X" % (max(0, size - 1)))

    def _update_path(self):
        try:
            self.path_var.set(
                "LAN → MCH %s → transit %s (ch %s) → target %s (bus %s)"
                % (self.mch_var.get(), self.transit_var.get(), self.tch_var.get(),
                   self.target_var.get(), self.bus_var.get()))
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
                         eeprom_size=size, transit_ch=int(self.tch_var.get().strip() or 0))

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
