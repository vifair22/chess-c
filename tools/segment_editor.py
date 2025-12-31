"""Tk app to edit 7-segment digit literals stored in src/renderer/draw.c."""

import re
import tkinter as tk
from pathlib import Path
from tkinter import ttk


class SegmentEditor(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("7-Segment Editor")
        self.resizable(False, False)

        self.glyph_file = Path(__file__).resolve().parent.parent / "src" / "renderer" / "assets.c"
        self.segments = []  # list of 10 items, each 7 ints (0/1)
        self.seg_state = [0] * 7
        self.current_digit = 0

        self._build_ui()
        self._draw_segments()
        self.load_from_file()
        self._freeze_geometry()

    def _build_ui(self):
        main = ttk.Frame(self)
        main.grid(row=0, column=0, padx=6, pady=6)

        self.scale = 0.7  # shrink overall size by ~30%
        self.seg_canvas = tk.Canvas(
            main,
            width=int(180 * self.scale),
            height=int(230 * self.scale),
            bg="#f4f4f4",
        )
        self.seg_canvas.grid(row=0, column=0, padx=(0, 8), pady=2)
        self.seg_canvas.bind("<Button-1>", self._toggle_segment)

        sidebar = ttk.Frame(main)
        sidebar.grid(row=0, column=1, sticky="n")
        ttk.Label(sidebar, text="Digits in segments").grid(row=0, column=0, columnspan=2, pady=(0, 4))

        self.listbox = tk.Listbox(sidebar, height=10, width=8, exportselection=False)
        self.listbox.grid(row=1, column=0, pady=(0, 4), sticky="ns")
        scrollbar = ttk.Scrollbar(sidebar, orient="vertical", command=self.listbox.yview)
        scrollbar.grid(row=1, column=1, sticky="ns")
        self.listbox.configure(yscrollcommand=scrollbar.set)
        self.listbox.bind("<<ListboxSelect>>", self._on_select_digit)
        self.listbox.bind("<Key>", self._jump_to_digit)

        ttk.Button(sidebar, text="Load from file", command=self.load_from_file).grid(
            row=2, column=0, columnspan=2, sticky="ew", pady=2
        )
        ttk.Button(sidebar, text="Save to file", command=self.save_to_file).grid(
            row=3, column=0, columnspan=2, sticky="ew", pady=2
        )
        ttk.Button(sidebar, text="Save digit to list", command=self.save_digit_to_list).grid(
            row=4, column=0, columnspan=2, sticky="ew", pady=2
        )

        self.output = tk.Text(self, width=50, height=2, wrap="word")
        self.output.grid(row=1, column=0, padx=6, pady=(2, 6))
        self.output.insert("1.0", "Click segments to toggle, then Save digit.")
        self.output.configure(state="disabled")

    def _draw_segments(self):
        self.seg_canvas.delete("segment")
        rects = self._segment_rects()
        for idx, (x0, y0, x1, y1) in enumerate(rects):
            fill = "#111" if self.seg_state[idx] else "#fff"
            self.seg_canvas.create_rectangle(
                x0, y0, x1, y1, fill=fill, outline="#999", tags=("segment", f"seg{idx}")
            )

    def _segment_rects(self):
        base = [
            (50, 20, 130, 36),   # top
            (34, 36, 50, 116),   # upper left
            (130, 36, 146, 116), # upper right
            (50, 116, 130, 132), # middle
            (34, 132, 50, 212),  # lower left
            (130, 132, 146, 212),# lower right
            (50, 212, 130, 228), # bottom
        ]
        return [
            (
                int(x0 * self.scale),
                int(y0 * self.scale),
                int(x1 * self.scale),
                int(y1 * self.scale),
            )
            for (x0, y0, x1, y1) in base
        ]

    def _toggle_segment(self, event):
        for idx, (x0, y0, x1, y1) in enumerate(self._segment_rects()):
            if x0 <= event.x <= x1 and y0 <= event.y <= y1:
                self.seg_state[idx] = 0 if self.seg_state[idx] else 1
                self._draw_segments()
                self._update_output()
                break

    def _current_segment_text(self):
        bits = " ".join(str(int(v)) for v in self.seg_state)
        return f"Digit {self.current_digit}: [{bits}]"

    def _show_message(self, text):
        self.output.configure(state="normal")
        self.output.delete("1.0", "end")
        self.output.insert("1.0", text)
        self.output.configure(state="disabled")

    def _update_output(self):
        self._show_message(self._current_segment_text())

    def load_from_file(self):
        try:
            text = self.glyph_file.read_text()
            entries, _, _ = self._parse_segments(text)
            if not entries:
                entries = [[0] * 7 for _ in range(10)]
            self.segments = entries[:10]
            self._refresh_listbox()
            self._load_digit(0)
            self.listbox.selection_set(0)
            self._show_message(f"Loaded {len(self.segments)} digits from {self.glyph_file.name}")
        except Exception as exc:
            self._show_message(f"Failed to load: {exc}")

    def save_to_file(self):
        try:
            text = self.glyph_file.read_text()
            _, match, indent = self._parse_segments(text)
            if len(self.segments) < 10:
                self.segments += [[0] * 7 for _ in range(10 - len(self.segments))]
            body = self._serialize_segments(self.segments, indent)
            new_text = text[: match.start(2)] + body + text[match.end(2) :]
            self.glyph_file.write_text(new_text)
            self._show_message(f"Saved {len(self.segments)} digits to {self.glyph_file.name}")
        except Exception as exc:
            self._show_message(f"Failed to save: {exc}")

    def _parse_segments(self, text):
        match = re.search(
            r"((?:static\s+)?const\s+uint8_t\s+kSegments\s*\[\s*10\s*\]\s*\[\s*7\s*\]\s*=\s*\{\s*)(.*?)(\s*\};)",
            text,
            re.S,
        )
        if not match:
            raise ValueError("Could not locate segments initializer in draw.c")
        body = match.group(2)
        entries = []
        for line in re.finditer(r"\{([^\}]*)\}", body):
            vals = []
            for token in line.group(1).split(","):
                token = token.strip()
                if not token:
                    continue
                try:
                    vals.append(int(token, 0))
                except ValueError:
                    vals.append(0)
            if len(vals) >= 7:
                entries.append(vals[:7])
        indent_match = re.search(r"\n(\s*)\{", body)
        indent = indent_match.group(1) if indent_match else "    "
        return entries, match, indent

    def _serialize_segments(self, entries, indent):
        lines = []
        for idx, vals in enumerate(entries):
            row_text = ", ".join(str(int(v)) for v in vals)
            lines.append(f"{indent}{{{row_text}}}, // {idx}")
        if lines:
            return "\n" + "\n".join(lines) + "\n"
        return "\n"

    def _refresh_listbox(self):
        self.listbox.delete(0, "end")
        for i in range(len(self.segments)):
            self.listbox.insert("end", str(i))

    def _on_select_digit(self, event):
        selection = self.listbox.curselection()
        if not selection:
            return
        self._load_digit(selection[0])

    def _jump_to_digit(self, event):
        if not event.char or not event.char.isdigit():
            return "break"
        digit = int(event.char)
        if digit < len(self.segments):
            self.listbox.selection_clear(0, "end")
            self.listbox.selection_set(digit)
            self.listbox.see(digit)
            self._load_digit(digit)
        return "break"

    def _load_digit(self, idx):
        if idx < 0 or idx >= len(self.segments):
            return
        self.current_digit = idx
        vals = self.segments[idx]
        for i in range(7):
            self.seg_state[i] = int(vals[i]) if i < len(vals) else 0
        self._draw_segments()
        self._update_output()

    def save_digit_to_list(self):
        if not self.segments:
            self.segments = [[0] * 7 for _ in range(10)]
        if len(self.segments) < 10:
            self.segments += [[0] * 7 for _ in range(10 - len(self.segments))]
        idx = self.current_digit if 0 <= self.current_digit < len(self.segments) else 0
        self.segments[idx] = [int(v) for v in self.seg_state]
        self._refresh_listbox()
        self.listbox.selection_clear(0, "end")
        self.listbox.selection_set(idx)
        self.listbox.see(idx)
        self._show_message(f"Updated digit {idx} in segments list.")

    def _freeze_geometry(self):
        self.update_idletasks()
        self.geometry(self.geometry())


if __name__ == "__main__":
    app = SegmentEditor()
    app.mainloop()
