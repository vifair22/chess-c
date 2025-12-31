"""Tk app to edit 5x7 glyph literals stored in src/renderer/draw.c."""

import re
import tkinter as tk
from pathlib import Path
from tkinter import ttk

CELL_SIZE = 36  # slightly narrower footprint
COLS = 5
ROWS = 7


class GlyphEditor(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("5x7 Glyph Editor")
        self.resizable(False, False)

        self.glyph_file = Path(__file__).resolve().parent.parent / "src" / "renderer" / "assets.c"
        self.glyphs = []  # list of (char, [7 bytes])
        self.grid_state = [[False for _ in range(COLS)] for _ in range(ROWS)]

        self._build_ui()
        self._draw_grid()
        self._update_output()
        self.load_from_file()

    def _build_ui(self):
        # Canvas + sidebar
        main = ttk.Frame(self)
        main.grid(row=0, column=0, padx=6, pady=6)

        self.canvas = tk.Canvas(main, width=COLS * CELL_SIZE, height=ROWS * CELL_SIZE, bg="#f4f4f4")
        self.canvas.grid(row=0, column=0, padx=(0, 8), pady=2)
        self.canvas.bind("<Button-1>", self._toggle_cell)

        sidebar = ttk.Frame(main)
        sidebar.grid(row=0, column=1, sticky="n")
        ttk.Label(sidebar, text="Glyphs in draw.c").grid(row=0, column=0, columnspan=2, pady=(0, 4))

        self.listbox = tk.Listbox(sidebar, height=15, width=10, exportselection=False)
        self.listbox.grid(row=1, column=0, pady=(0, 4), sticky="ns")
        scrollbar = ttk.Scrollbar(sidebar, orient="vertical", command=self.listbox.yview)
        scrollbar.grid(row=1, column=1, sticky="ns")
        self.listbox.configure(yscrollcommand=scrollbar.set)
        self.listbox.bind("<<ListboxSelect>>", self._on_select_glyph)
        self.listbox.bind("<Key>", self._jump_to_char)

        ttk.Button(sidebar, text="Load from file", command=self.load_from_file).grid(
            row=2, column=0, columnspan=2, sticky="ew", pady=2
        )
        ttk.Button(sidebar, text="Save to file", command=self.save_to_file).grid(
            row=3, column=0, columnspan=2, sticky="ew", pady=2
        )
        ttk.Button(sidebar, text="Delete", command=self.delete_selected).grid(
            row=4, column=0, columnspan=2, sticky="ew", pady=2
        )

        # Glyph metadata + actions
        char_frame = ttk.Frame(self)
        char_frame.grid(row=1, column=0, sticky="w", padx=6, pady=(2, 0))
        ttk.Label(char_frame, text="Glyph char:").grid(row=0, column=0, sticky="w", padx=(0, 4))
        self.char_var = tk.StringVar(value="A")
        ttk.Entry(char_frame, textvariable=self.char_var, width=6).grid(row=0, column=1, sticky="w")
        self.char_var.trace_add("write", self._update_output)

        controls = ttk.Frame(self)
        controls.grid(row=2, column=0, padx=6, pady=4, sticky="w")
        ttk.Button(controls, text="Clear", command=self.clear).grid(row=0, column=0, padx=4, pady=4)
        ttk.Button(controls, text="Copy literal", command=self.copy_literal).grid(row=0, column=1, padx=4, pady=4)
        ttk.Button(controls, text="Paste literal", command=self.paste_literal).grid(row=0, column=2, padx=4, pady=4)
        ttk.Button(controls, text="Save glyph to list", command=self.save_glyph_to_list).grid(row=0, column=3, padx=4, pady=4)

        self.output = tk.Text(self, width=55, height=2, wrap="word")
        self.output.grid(row=3, column=0, padx=6, pady=(2, 6))
        self.output.insert("1.0", "Click cells to toggle pixels, then Copy literal.")
        self.output.configure(state="disabled")

    def _draw_grid(self):
        self.canvas.delete("cell")
        for r in range(ROWS):
            for c in range(COLS):
                x0 = c * CELL_SIZE
                y0 = r * CELL_SIZE
                x1 = x0 + CELL_SIZE
                y1 = y0 + CELL_SIZE
                fill = "#111" if self.grid_state[r][c] else "#fff"
                self.canvas.create_rectangle(
                    x0, y0, x1, y1, fill=fill, outline="#999", tags=("cell", f"r{r}c{c}")
                )

    def _toggle_cell(self, event):
        col = event.x // CELL_SIZE
        row = event.y // CELL_SIZE
        if 0 <= col < COLS and 0 <= row < ROWS:
            self.grid_state[row][col] = not self.grid_state[row][col]
            self._draw_grid()
            self._update_output()

    def clear(self):
        for r in range(ROWS):
            for c in range(COLS):
                self.grid_state[r][c] = False
        self._draw_grid()
        self._update_output()

    def _row_value(self, row_idx):
        value = 0
        for c in range(COLS):
            if self.grid_state[row_idx][c]:
                value |= 1 << (COLS - 1 - c)
        return value

    def _current_literal(self):
        ch = (self.char_var.get() or "?")[0]
        row_bytes = [self._row_value(r) for r in range(ROWS)]
        return "{{'{ch}', {{{rows}}}}},".format(ch=ch, rows=", ".join(f"0x{v:02X}" for v in row_bytes))

    def copy_literal(self):
        literal = self._current_literal()
        self.clipboard_clear()
        self.clipboard_append(literal)
        self._show_message(literal)

    def paste_literal(self):
        try:
            text = self.clipboard_get()
        except tk.TclError:
            self._show_message("Clipboard is empty.")
            return
        parsed = self._parse_literal(text)
        if not parsed:
            self._show_message("Could not parse literal.")
            return
        ch, rows = parsed
        self.char_var.set(ch)
        for r in range(ROWS):
            row_val = rows[r] if r < len(rows) else 0
            for c in range(COLS):
                self.grid_state[r][c] = bool(row_val & (1 << (COLS - 1 - c)))
        self._draw_grid()
        self._update_output()

    def _update_output(self, *args):
        self._show_message(self._current_literal())

    def _show_message(self, text):
        self.output.configure(state="normal")
        self.output.delete("1.0", "end")
        self.output.insert("1.0", text)
        self.output.configure(state="disabled")

    def _parse_glyphs(self, text):
        match = re.search(
            r"((?:static\s+)?const\s+Glyph5x7\s+kGlyphs\[\]\s*=\s*\{\s*)(.*?)(\s*\};)",
            text,
            re.S,
        )
        if not match:
            raise ValueError("Could not locate kGlyphs initializer in draw.c")
        body = match.group(2)
        indent_match = re.search(r"\n(\s*)\{'", body)
        indent = indent_match.group(1) if indent_match else "    "
        entries = []
        for entry in re.finditer(r"\{\s*'(.)'\s*,\s*\{([^\}]*)\}\s*\}", body):
            ch = entry.group(1)
            rows_text = entry.group(2)
            try:
                row_vals = [
                    int(token.strip(), 0)
                    for token in rows_text.split(",")
                    if token.strip()
                ]
            except ValueError:
                continue
            if len(row_vals) >= ROWS:
                entries.append((ch, row_vals[:ROWS]))
        return entries, match, indent

    def _serialize_glyphs(self, entries, indent):
        lines = []
        for ch, rows in entries:
            row_text = ", ".join(f"0x{v:02X}" for v in rows)
            lines.append("{indent}{{'{ch}', {{{rows}}}}},".format(indent=indent, ch=ch, rows=row_text))
        if lines:
            return "\n" + "\n".join(lines) + "\n"
        return "\n"

    def load_from_file(self):
        try:
            text = self.glyph_file.read_text()
            entries, _, _ = self._parse_glyphs(text)
            self.glyphs = entries
            self._refresh_listbox()
            if self.glyphs:
                self._load_glyph(self.glyphs[0])
                self.listbox.selection_set(0)
            self._show_message(f"Loaded {len(self.glyphs)} glyphs from {self.glyph_file.name}")
        except Exception as exc:
            self._show_message(f"Failed to load: {exc}")

    def save_to_file(self):
        try:
            text = self.glyph_file.read_text()
            _, match, indent = self._parse_glyphs(text)
            new_body = self._serialize_glyphs(self.glyphs, indent)
            new_text = text[: match.start(2)] + new_body + text[match.end(2) :]
            self.glyph_file.write_text(new_text)
            self._show_message(f"Saved {len(self.glyphs)} glyphs to {self.glyph_file.name}")
        except Exception as exc:
            self._show_message(f"Failed to save: {exc}")

    def _refresh_listbox(self):
        self.listbox.delete(0, "end")
        for ch, _ in self.glyphs:
            self.listbox.insert("end", ch)

    def _on_select_glyph(self, event):
        selection = self.listbox.curselection()
        if not selection:
            return
        idx = selection[0]
        if idx < 0 or idx >= len(self.glyphs):
            return
        self._load_glyph(self.glyphs[idx])

    def _jump_to_char(self, event):
        if not event.char:
            return "break"
        target = event.char.upper()
        for idx, (ch, _) in enumerate(self.glyphs):
            if ch.upper() == target:
                self.listbox.selection_clear(0, "end")
                self.listbox.selection_set(idx)
                self.listbox.see(idx)
                self._load_glyph(self.glyphs[idx])
                break
        return "break"

    def _load_glyph(self, glyph):
        ch, rows = glyph
        self.char_var.set(ch)
        for r in range(ROWS):
            row_val = rows[r] if r < len(rows) else 0
            for c in range(COLS):
                self.grid_state[r][c] = bool(row_val & (1 << (COLS - 1 - c)))
        self._draw_grid()
        self._update_output()

    def save_glyph_to_list(self):
        ch_text = self.char_var.get().strip()
        if not ch_text:
            self._show_message("Enter a character before saving.")
            return
        ch = ch_text[0]
        rows = [self._row_value(r) for r in range(ROWS)]
        updated = False
        for idx, (existing_ch, _) in enumerate(self.glyphs):
            if existing_ch == ch:
                self.glyphs[idx] = (ch, rows)
                updated = True
                break
        if not updated:
            self.glyphs.append((ch, rows))
            self.glyphs.sort(key=lambda g: g[0])
        self._refresh_listbox()
        try:
            new_idx = [i for i, (c, _) in enumerate(self.glyphs) if c == ch][0]
            self.listbox.selection_clear(0, "end")
            self.listbox.selection_set(new_idx)
        except IndexError:
            pass
        self._show_message(f"{'Updated' if updated else 'Added'} glyph '{ch}' in list.")

    def delete_selected(self):
        selection = self.listbox.curselection()
        if not selection:
            self._show_message("Select a glyph to delete.")
            return
        idx = selection[0]
        if 0 <= idx < len(self.glyphs):
            ch, _ = self.glyphs.pop(idx)
            self._refresh_listbox()
            if self.glyphs:
                new_idx = min(idx, len(self.glyphs) - 1)
                self.listbox.selection_set(new_idx)
                self._load_glyph(self.glyphs[new_idx])
            else:
                self.clear()
            self._show_message(f"Deleted glyph '{ch}'.")

    def _parse_literal(self, text):
        match = re.search(r"\{\s*'(.?)'\s*,\s*\{([^\}]*)\}", text)
        if not match:
            return None
        ch = match.group(1)
        row_part = match.group(2)
        try:
            values = [int(token.strip(), 0) for token in row_part.split(",") if token.strip()]
        except ValueError:
            return None
        if len(values) < ROWS:
            return None
        return ch, values[:ROWS]


if __name__ == "__main__":
    app = GlyphEditor()
    app.mainloop()
