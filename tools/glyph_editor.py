"""Small Tkinter app to draw 5x7 glyphs and copy C array literals.

Run with: python tools/glyph_editor.py
"""

import tkinter as tk
from tkinter import ttk
import re

CELL_SIZE = 40
COLS = 5
ROWS = 7


class GlyphEditor(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("5x7 Glyph Editor")
        self.resizable(False, False)

        self.grid_state = [[False for _ in range(COLS)] for _ in range(ROWS)]

        self.canvas = tk.Canvas(
            self, width=COLS * CELL_SIZE, height=ROWS * CELL_SIZE, bg="#f4f4f4"
        )
        self.canvas.grid(row=0, column=0, columnspan=5, padx=8, pady=8)
        self.canvas.bind("<Button-1>", self._toggle_cell)

        ttk.Label(self, text="Glyph char:").grid(row=1, column=0, sticky="e", padx=4)
        self.char_var = tk.StringVar(value="A")
        ttk.Entry(self, textvariable=self.char_var, width=4).grid(
            row=1, column=1, sticky="w"
        )
        self.char_var.trace_add("write", self._update_literal)

        ttk.Button(self, text="Clear", command=self.clear).grid(
            row=1, column=2, padx=4, pady=4
        )
        ttk.Button(self, text="Copy literal", command=self.copy_literal).grid(
            row=1, column=3, padx=4, pady=4
        )
        ttk.Button(self, text="Paste literal", command=self.paste_literal).grid(
            row=1, column=4, padx=4, pady=4
        )

        self.output = tk.Text(self, width=50, height=3, wrap="word")
        self.output.grid(row=2, column=0, columnspan=5, padx=8, pady=(0, 8))
        self.output.insert("1.0", "Click cells to toggle pixels, then Copy literal.")
        self.output.configure(state="disabled")

        self._draw_grid()
        self._update_literal()

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
                    x0,
                    y0,
                    x1,
                    y1,
                    fill=fill,
                    outline="#999",
                    tags=("cell", f"r{r}c{c}"),
                )

    def _toggle_cell(self, event):
        col = event.x // CELL_SIZE
        row = event.y // CELL_SIZE
        if 0 <= col < COLS and 0 <= row < ROWS:
            self.grid_state[row][col] = not self.grid_state[row][col]
            self._draw_grid()
            self._update_literal()

    def clear(self):
        for r in range(ROWS):
            for c in range(COLS):
                self.grid_state[r][c] = False
        self._draw_grid()
        self._update_literal()

    def _row_value(self, row_idx):
        # Bits are stored left-to-right with MSB as the leftmost pixel.
        value = 0
        for c in range(COLS):
            if self.grid_state[row_idx][c]:
                value |= 1 << (COLS - 1 - c)
        return value

    def _current_literal(self):
        char_text = self.char_var.get() or '?'
        ch = char_text[0] if char_text else '?'
        row_bytes = [self._row_value(r) for r in range(ROWS)]
        return "{{'{ch}', {{{rows}}}}},".format(
            ch=ch, rows=", ".join(f"0x{v:02X}" for v in row_bytes)
        )

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
        self._update_literal()

    def _update_literal(self, *args):
        self._show_message(self._current_literal())

    def _show_message(self, text):
        self.output.configure(state="normal")
        self.output.delete("1.0", "end")
        self.output.insert("1.0", text)
        self.output.configure(state="disabled")

    def _parse_literal(self, text):
        # Accept formats like {'A', {0x1E, 0x11, ...}}, with or without spaces.
        match = re.search(r"\{\s*'(.?)'\s*,\s*\{([^\}]*)\}", text)
        if not match:
            return None
        ch = match.group(1)
        row_part = match.group(2)
        try:
            values = [
                int(token.strip(), 0)
                for token in row_part.split(",")
                if token.strip()
            ]
        except ValueError:
            return None
        if len(values) < ROWS:
            return None
        return ch, values[:ROWS]


if __name__ == "__main__":
    app = GlyphEditor()
    app.mainloop()
