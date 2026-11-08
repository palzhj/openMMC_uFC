#!/usr/bin/env python3
"""
fru_gui_tk.py -- desktop FRU editor for the uFC MMC (Tkinter).

MicroBoot-like desktop application: connect to the MMC through a NAT-MCH,
read the FRU EEPROM, edit the board/product fields, write them back, or
save/load the raw 256-byte image.

Runs on Linux and Windows (Tkinter ships with CPython on Windows; on Linux
install python3-tk).  Requires the firmware to be built with
-DEEPROM_ACCESS=ON.

Usage:
    python3 fru_gui_tk.py [--mch 192.168.1.41] [--target 0x7e] [--bus 7]
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

APP_TITLE = "uFC FRU Editor"
FIELD_LABELS = {
    "manufacturer": "Manufacturer",
    "product_name": "Product / board name",
    "serial": "Serial number",
    "part_number": "Part number",
    "version": "Part version",
    "asset_tag": "Asset tag",
    "fru_file_id": "FRU file ID",
}


class FruEditor(tk.Tk):
    def __init__(self, mch, target, bus, transit):
        super().__init__()
        self.title(APP_TITLE)
        self.geometry("980x680")
        self.minsize(860, 560)

        self.raw = None
        self.parsed = None
        self.entries = {"board": {}, "product": {}}

        self._build_ui(mch, target, bus, transit)
        self.log("Ready. Firmware must be built with -DEEPROM_ACCESS=ON.")

    # ------------------------------------------------------------------ UI
    def _build_ui(self, mch, target, bus, transit):
        top = ttk.Frame(self, padding=(8, 6))
        top.pack(fill="x")

        ttk.Label(top, text="MCH").grid(row=0, column=0, sticky="e")
        self.mch_var = tk.StringVar(value=mch)
        ttk.Entry(top, textvariable=self.mch_var, width=16).grid(row=0, column=1, padx=4)

        ttk.Label(top, text="Target").grid(row=0, column=2, sticky="e")
        self.target_var = tk.StringVar(value=target)
        ttk.Entry(top, textvariable=self.target_var, width=7).grid(row=0, column=3, padx=4)

        ttk.Label(top, text="Bus").grid(row=0, column=4, sticky="e")
        self.bus_var = tk.StringVar(value=str(bus))
        ttk.Entry(top, textvariable=self.bus_var, width=4).grid(row=0, column=5, padx=4)

        ttk.Label(top, text="Transit").grid(row=0, column=6, sticky="e")
        self.transit_var = tk.StringVar(value=transit)
        ttk.Entry(top, textvariable=self.transit_var, width=7).grid(row=0, column=7, padx=4)

        ttk.Button(top, text="Read FRU", command=self.read_fru).grid(row=0, column=8, padx=(12, 0))
        ttk.Button(top, text="Write FRU", command=self.write_fru).grid(row=0, column=9, padx=4)

        self.nb = ttk.Notebook(self)
        self.nb.pack(fill="both", expand=True, padx=8, pady=4)

        self.fields_tab = ttk.Frame(self.nb, padding=8)
        self.raw_tab = ttk.Frame(self.nb, padding=8)
        self.nb.add(self.fields_tab, text="FRU fields")
        self.nb.add(self.raw_tab, text="Raw EEPROM")

        board_frame = ttk.LabelFrame(self.fields_tab, text="Board info area", padding=6)
        board_frame.pack(fill="x", pady=(0, 8))
        self._build_area(board_frame, "board", with_time=True)

        product_frame = ttk.LabelFrame(self.fields_tab, text="Product info area", padding=6)
        product_frame.pack(fill="x")
        self._build_area(product_frame, "product", with_time=False)

        self.hex_text = tk.Text(self.raw_tab, font=("Courier", 10), state="disabled",
                                height=18, width=64)
        self.hex_text.pack(fill="both", expand=True)

        bottom = ttk.Frame(self, padding=(8, 4))
        bottom.pack(fill="x")
        ttk.Button(bottom, text="Save image...", command=self.save_image).pack(side="left")
        ttk.Button(bottom, text="Load image to EEPROM", command=self.load_image).pack(side="left", padx=6)
        ttk.Button(bottom, text="Exit", command=self.destroy).pack(side="right")

        self.status = tk.StringVar(value="")
        ttk.Label(self, textvariable=self.status, relief="sunken", anchor="w",
                  padding=(6, 2)).pack(fill="x", side="bottom")

        logf = ttk.LabelFrame(self, text="Log", padding=4)
        logf.pack(fill="x", padx=8, pady=(0, 4))
        self.log_text = tk.Text(logf, height=4, font=("Courier", 9), state="disabled")
        self.log_text.pack(fill="x")

    def _build_area(self, parent, key, with_time):
        row = 0
        if with_time:
            ttk.Label(parent, text="Manufacturing time (UTC)").grid(row=row, column=0, sticky="e", pady=2)
            self.time_var = tk.StringVar()
            ttk.Entry(parent, textvariable=self.time_var, width=24).grid(row=row, column=1, sticky="w", padx=4)
            ttk.Button(parent, text="Now", width=5, command=self._set_time_now).grid(
                row=row, column=2, sticky="w", padx=(0, 4))
            self.time_hint = ttk.Label(parent, text="", foreground="#666")
            self.time_hint.grid(row=row, column=3, sticky="w")
            row += 1
        for name in ("manufacturer", "product_name", "serial", "part_number",
                     "version", "asset_tag", "fru_file_id"):
            if key == "board" and name in ("version", "asset_tag"):
                continue
            if key == "product" and name == "product_name":
                pass
            ttk.Label(parent, text=FIELD_LABELS[name]).grid(row=row, column=0, sticky="e", pady=2)
            var = tk.StringVar()
            ent = ttk.Entry(parent, textvariable=var, width=8)
            ent.grid(row=row, column=1, sticky="w", padx=4)
            hint = ttk.Label(parent, text="", foreground="#666")
            hint.grid(row=row, column=2, sticky="w")
            self.entries[key][name] = (var, ent, hint)
            self._auto_width(ent, var)
            row += 1

    # ------------------------------------------------------------- helpers
    def _set_time_now(self):
        now = datetime.datetime.now(datetime.timezone.utc)
        self.time_var.set(now.strftime("%Y-%m-%d %H:%M"))
        self.time_hint.configure(text="(current UTC time)")

    @staticmethod
    def _auto_width(ent, var):
        """Resize the entry to its text, capped at the field capacity."""
        def update(*_args):
            n = len(var.get())
            # FRU spec limit: show the longest legal value for every field
            width = max(FRU_MAX_STRING + 1, n + 1)
            ent.configure(width=width)
        var.trace_add("write", update)
        update()

    def log(self, text):
        self.log_text.configure(state="normal")
        self.log_text.insert("end", text + "\n")
        self.log_text.see("end")
        self.log_text.configure(state="disabled")

    def _access(self):
        return FruAccess(self.mch_var.get().strip(), self.target_var.get().strip(),
                         self.transit_var.get().strip(), self.bus_var.get().strip())

    def _fill(self, raw, parsed):
        self.raw = raw
        self.parsed = parsed
        minutes = parsed["board"].get("mfg_time", 0)
        self.time_var.set(mfg_minutes_to_str(minutes))
        self.time_hint.configure(text="(YYYY-MM-DD HH:MM, %d min since 1996)" % minutes)
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
                    hint.configure(text="max %d" % FRU_MAX_STRING)
        self.hex_text.configure(state="normal")
        self.hex_text.delete("1.0", "end")
        for i in range(0, len(raw), 16):
            self.hex_text.insert("end", "%04x  %s\n" % (
                i, " ".join("%02x" % b for b in raw[i:i + 16])))
        self.hex_text.configure(state="disabled")

    # -------------------------------------------------------------- actions
    def read_fru(self):
        try:
            raw = self._access().dump()
            parsed = parse_fru(raw)
            self._fill(raw, parsed)
            self.status.set("FRU read: %d bytes (board and product areas parsed)" % len(raw))
            self.log("Read %d bytes from FRU EEPROM." % len(raw))
        except Exception as e:
            self.status.set("Read failed")
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
        values["board"]["mfg_time"] = self.time_var.get()  # parsed by build_areas
        try:
            image = build_image(self.parsed, values)
            payload = image_for_eeprom(image)
            acc = self._access()
            acc.write(0, payload)
            raw = acc.dump()
            parsed = parse_fru(raw)
            self._fill(raw, parsed)
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
            self.status.set("FRU written and verified (%d bytes)" % len(image))
            self.log("Rebuilt and wrote the FRU image (%d bytes), verification OK." % len(image))
        except Exception as e:
            self.status.set("Write failed")
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
            with open(path, "rb") as f:
                data = f.read(EEPROM_SIZE)
            if len(data) != EEPROM_SIZE:
                raise ValueError("expected a 256-byte image")
            parse_fru(data)  # validate before writing
            if not messagebox.askyesno(APP_TITLE, "Write the whole 256-byte image to the FRU EEPROM?"):
                return
            acc = self._access()
            acc.write(0, data[:FRU_MAX_SIZE])
            raw = acc.dump()
            self._fill(raw, parse_fru(raw))
            self.status.set("Image written")
            self.log("Wrote %d-byte image from %s" % (len(data), path))
        except Exception as e:
            self.status.set("Load failed")
            self.log("ERROR: %s" % e)
            messagebox.showerror(APP_TITLE, "Load failed:\n%s" % e)


def main():
    ap = argparse.ArgumentParser(description="uFC FRU desktop editor")
    ap.add_argument("--mch", default="192.168.1.41")
    ap.add_argument("--target", default="0x7e")
    ap.add_argument("--bus", default="7")
    ap.add_argument("--transit", default="0x82")
    args = ap.parse_args()
    app = FruEditor(args.mch, args.target, args.bus, args.transit)
    app.mainloop()


if __name__ == "__main__":
    main()
