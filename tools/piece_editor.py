"""Tk app to edit SDL-ready RGBA chess piece sprites embedded in src/renderer/assets.c.

Pixels are stored as 32-bit RGBA: 0xAARRGGBB. We map 0=transparent, 1=light, 2=dark for editing.
"""

import re
import tkinter as tk
from pathlib import Path
from tkinter import ttk, filedialog, simpledialog

CELL_SIZE = 18
TARGET_SIZE = 20  # fixed 20x20 sprites

PALETTE = {
    0: "#ff00ff",  # transparent shown as pink
    1: "#dddddd",  # light
    2: "#222222",  # dark
}
LIGHT_COLOR = 0xFFF5F5F5
DARK_COLOR = 0xFF1E1E1E
TRANSPARENT_COLOR = 0x00000000

try:
    from PIL import Image, ImageOps
except ImportError:
    Image = None
    ImageOps = None


class PieceEditor(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("Piece Sprite Editor (RGBA)")
        self.resizable(False, False)

        self.assets_path = Path(__file__).resolve().parent.parent / "src" / "renderer" / "assets.c"
        self.pieces = []  # list of dicts: name,width,height,pixels,grid
        self.current_idx = 0
        self.current_color = tk.IntVar(value=2)

        self._build_ui()
        self.load_from_file()

    def _build_ui(self):
        main = ttk.Frame(self)
        main.grid(row=0, column=0, padx=6, pady=6)

        # Canvas sized to fixed 20x20.
        self.canvas = tk.Canvas(main, width=TARGET_SIZE * CELL_SIZE, height=TARGET_SIZE * CELL_SIZE, bg="#f4f4f4")
        self.canvas.grid(row=0, column=0, padx=(0, 8), pady=2)
        self.canvas.bind("<Button-1>", self._on_canvas_click)
        self.canvas.bind("<B1-Motion>", self._on_canvas_drag)

        sidebar = ttk.Frame(main)
        sidebar.grid(row=0, column=1, sticky="n")
        ttk.Label(sidebar, text="Pieces").grid(row=0, column=0, columnspan=2, pady=(0, 4))

        self.listbox = tk.Listbox(sidebar, height=12, width=18, exportselection=False)
        self.listbox.grid(row=1, column=0, pady=(0, 4), sticky="ns")
        scrollbar = ttk.Scrollbar(sidebar, orient="vertical", command=self.listbox.yview)
        scrollbar.grid(row=1, column=1, sticky="ns")
        self.listbox.configure(yscrollcommand=scrollbar.set)
        self.listbox.bind("<<ListboxSelect>>", self._on_select_piece)

        ttk.Button(sidebar, text="Load from file", command=self.load_from_file).grid(
            row=2, column=0, columnspan=2, sticky="ew", pady=2
        )
        ttk.Button(sidebar, text="Save to file", command=self.save_to_file).grid(
            row=3, column=0, columnspan=2, sticky="ew", pady=2
        )
        ttk.Button(sidebar, text="Save piece to list", command=self.save_piece_to_list).grid(
            row=4, column=0, columnspan=2, sticky="ew", pady=2
        )
        ttk.Button(sidebar, text="Add new piece", command=self.add_new_piece).grid(
            row=5, column=0, columnspan=2, sticky="ew", pady=2
        )
        ttk.Button(sidebar, text="Import PNG", command=self.import_png).grid(
            row=6, column=0, columnspan=2, sticky="ew", pady=2
        )
        ttk.Button(sidebar, text="Export PNG", command=self.export_png).grid(
            row=7, column=0, columnspan=2, sticky="ew", pady=2
        )
        ttk.Button(sidebar, text="Clear", command=self.clear_piece).grid(
            row=8, column=0, columnspan=2, sticky="ew", pady=2
        )

        palette_frame = ttk.Frame(self)
        palette_frame.grid(row=1, column=0, padx=6, pady=(2, 0), sticky="w")
        ttk.Label(palette_frame, text="Paint:").grid(row=0, column=0, padx=(0, 6))
        ttk.Radiobutton(palette_frame, text="Transparent", variable=self.current_color, value=0).grid(row=0, column=1)
        ttk.Radiobutton(palette_frame, text="Light", variable=self.current_color, value=1).grid(row=0, column=2)
        ttk.Radiobutton(palette_frame, text="Dark", variable=self.current_color, value=2).grid(row=0, column=3)

        self.output = tk.Text(self, width=70, height=2, wrap="word")
        self.output.grid(row=2, column=0, padx=6, pady=(2, 6))
        self.output.insert("1.0", "Click to paint pixels, then Save piece.")
        self.output.configure(state="disabled")

    def _on_canvas_click(self, event):
        if self._paint_from_event(event):
            self._draw_grid()

    def _on_canvas_drag(self, event):
        if self._paint_from_event(event):
            self._draw_grid()

    def _paint_from_event(self, event):
        if not self.pieces:
            return False
        piece = self.pieces[self.current_idx]
        w, h = piece["width"], piece["height"]
        col = event.x // CELL_SIZE
        row = event.y // CELL_SIZE
        if not (0 <= col < w and 0 <= row < h):
            return False
        new_val = int(self.current_color.get())
        if piece["grid"][row][col] == new_val:
            return False
        piece["grid"][row][col] = new_val
        return True

    def _on_select_piece(self, event):
        selection = self.listbox.curselection()
        if not selection:
            return
        self.current_idx = selection[0]
        self._draw_grid()
        self._show_message(f"Selected {self.pieces[self.current_idx]['name']}")

    def clear_piece(self):
        if not self.pieces:
            return
        piece = self.pieces[self.current_idx]
        piece["grid"] = [[0 for _ in range(piece["width"])] for _ in range(piece["height"])]
        self._draw_grid()

    def _draw_grid(self):
        self.canvas.delete("cell")
        if not self.pieces:
            return
        piece = self.pieces[self.current_idx]
        w, h = TARGET_SIZE, TARGET_SIZE
        for r in range(h):
            for c in range(w):
                x0 = c * CELL_SIZE
                y0 = r * CELL_SIZE
                x1 = x0 + CELL_SIZE
                y1 = y0 + CELL_SIZE
                val = piece["grid"][r][c]
                fill = PALETTE.get(val, "#ffffff")
                self.canvas.create_rectangle(
                    x0, y0, x1, y1, fill=fill, outline="#999", tags=("cell",)
                )

    def _pixels_to_grid(self, pixels, width, height):
        grid = [[0 for _ in range(TARGET_SIZE)] for _ in range(TARGET_SIZE)]
        x_offset = max((TARGET_SIZE - width) // 2, 0)
        y_offset = max((TARGET_SIZE - height) // 2, 0)
        for y in range(height):
            for x in range(width):
                val = pixels[y * width + x]
                if val == LIGHT_COLOR:
                    mapped = 1
                elif val == DARK_COLOR:
                    mapped = 2
                else:
                    mapped = 0
                gy = y + y_offset
                gx = x + x_offset
                if 0 <= gx < TARGET_SIZE and 0 <= gy < TARGET_SIZE:
                    grid[gy][gx] = mapped
        return grid

    def _grid_to_pixels(self, grid, width, height):
        width = TARGET_SIZE
        height = TARGET_SIZE
        pixels = []
        for y in range(height):
            for x in range(width):
                val = grid[y][x] & 0x3
                if val == 1:
                    pixels.append(LIGHT_COLOR)
                elif val == 2:
                    pixels.append(DARK_COLOR)
                else:
                    pixels.append(TRANSPARENT_COLOR)
        return pixels

    def _parse_assets(self, text):
        arrays = {}
        for match in re.finditer(
            r"static\s+const\s+uint32_t\s+(sprite_[A-Za-z0-9_]+)\s*\[\]\s*=\s*\{([^}]*)\};\s*//\s*(\d+)x(\d+)\s*RGBA",
            text,
            re.S,
        ):
            name = match.group(1)
            body = match.group(2)
            width = int(match.group(3))
            height = int(match.group(4))
            try:
                values = [int(tok.strip(), 0) for tok in body.split(",") if tok.strip()]
            except ValueError:
                continue
            arrays[name] = {
                "width": width,
                "height": height,
                "pixels": values,
            }

        pieces = []
        table_match = re.search(
            r"(const\s+PieceSprite\s+kPieceSprites\s*\[\s*\d*\s*\]\s*=\s*\{\s*)(.*?)(\s*\};)",
            text,
            re.S,
        )
        if not table_match:
            raise ValueError("Could not locate kPieceSprites table")
        table_body = table_match.group(2)
        for entry in re.finditer(
            r"\{\s*\"([^\"]+)\"\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(sprite_[A-Za-z0-9_]+)\s*\}",
            table_body,
        ):
            name = entry.group(1)
            width = int(entry.group(2))
            height = int(entry.group(3))
            arr_name = entry.group(4)
            arr = arrays.get(arr_name)
            if not arr:
                continue
            grid = self._pixels_to_grid(arr["pixels"], width, height)
            pieces.append(
                {
                    "name": name,
                    "array": arr_name,
                    "width": TARGET_SIZE,
                    "height": TARGET_SIZE,
                    "grid": grid,
                }
            )
        return pieces, table_match

    def load_from_file(self):
        try:
            text = self.assets_path.read_text()
            pieces, table_match = self._parse_assets(text)
            self.pieces = pieces
            self.current_idx = 0
            self._refresh_list()
            if self.pieces:
                self.listbox.selection_set(0)
            self._draw_grid()
            self._show_message(f"Loaded {len(self.pieces)} pieces from {self.assets_path.name}")
            self._table_match = table_match
            self._assets_text = text
        except Exception as exc:
            self._show_message(f"Failed to load: {exc}")

    def save_piece_to_list(self):
        if not self.pieces:
            return
        piece = self.pieces[self.current_idx]
        pixels = self._grid_to_pixels(piece["grid"], piece["width"], piece["height"])
        piece["pixels"] = pixels
        self._show_message(f"Updated {piece['name']} in memory.")

    def save_to_file(self):
        if not self.pieces:
            self._show_message("No pieces loaded.")
            return
        text = self._assets_text
        # Update arrays.
        new_arrays = []
        for piece in self.pieces:
            pixels = self._grid_to_pixels(piece["grid"], piece["width"], piece["height"])
            piece["pixels"] = pixels
            array_pattern = re.compile(
                r"(static\s+const\s+uint32_t\s+%s\s*\[\]\s*=\s*\{\s*)(.*?)(\s*\};\s*//\s*%dx%d\s*RGBA)" % (
                    re.escape(piece["array"]),
                    piece["width"],
                    piece["height"],
                ),
                re.S,
            )
            new_body = ", ".join(f"0x{p:08X}" for p in pixels)
            comment = f"}}; // {piece['width']}x{piece['height']} RGBA"
            text, count = array_pattern.subn(lambda m: m.group(1) + new_body + comment, text, count=1)
            if count == 0:
                # New array; will insert before the table later.
                lines = []
                for i in range(0, len(pixels), 8):
                    lines.append("    " + ", ".join(f"0x{p:08X}" for p in pixels[i:i+8]))
                body = ",\n".join(lines)
                new_arrays.append(f"\nstatic const uint32_t {piece['array']}[] = {{\n{body}\n}}; // {piece['width']}x{piece['height']} RGBA\n")
        # Insert any new arrays before the table.
        table_match = re.search(
            r"(const\s+PieceSprite\s+kPieceSprites\s*\[\s*\d*\s*\]\s*=\s*\{\s*)(.*?)(\s*\};)",
            text,
            re.S,
        )
        if not table_match:
            self._show_message("Failed to locate kPieceSprites for writing.")
            return
        if new_arrays:
            insert_pos = table_match.start(0)
            text = text[:insert_pos] + "".join(new_arrays) + text[insert_pos:]
            # Re-find table after insertion
            table_match = re.search(
                r"(const\s+PieceSprite\s+kPieceSprites\s*\[\s*\d*\s*\]\s*=\s*\{\s*)(.*?)(\s*\};)",
                text,
                re.S,
            )
            if not table_match:
                self._show_message("Failed to locate kPieceSprites after inserting new arrays.")
                return
        indent_match = re.search(r"\n(\s*)\{", table_match.group(2))
        indent = indent_match.group(1) if indent_match else "    "
        lines = []
        for p in self.pieces:
            lines.append(f'{indent}{{"{p["name"]}", {p["width"]}, {p["height"]}, {p["array"]}}},')
        new_table_body = "\n" + "\n".join(lines) + "\n"
        text = text[: table_match.start(2)] + new_table_body + text[table_match.end(2) :]
        self.assets_path.write_text(text)
        self._assets_text = text
        self._show_message(f"Saved {len(self.pieces)} pieces to {self.assets_path.name}")

    def _refresh_list(self):
        self.listbox.delete(0, "end")
        for p in self.pieces:
            self.listbox.insert("end", p["name"])

    def _show_message(self, text):
        self.output.configure(state="normal")
        self.output.delete("1.0", "end")
        self.output.insert("1.0", text)
        self.output.configure(state="disabled")

    def add_new_piece(self):
        name = simpledialog.askstring("New Piece", "Name for new piece (letters/numbers/underscores):", parent=self)
        if not name:
            return
        sanitized = re.sub(r"[^A-Za-z0-9_]+", "_", name).strip("_")
        if not sanitized:
            self._show_message("Invalid name.")
            return
        if any(p["name"] == sanitized for p in self.pieces):
            self._show_message("Name already exists.")
            return
        array_name = f"sprite_{sanitized}_rgba"
        grid = [[0 for _ in range(TARGET_SIZE)] for _ in range(TARGET_SIZE)]
        self.pieces.append(
            {
                "name": sanitized,
                "array": array_name,
                "width": TARGET_SIZE,
                "height": TARGET_SIZE,
                "grid": grid,
            }
        )
        self._refresh_list()
        self.current_idx = len(self.pieces) - 1
        self.listbox.selection_clear(0, "end")
        self.listbox.selection_set(self.current_idx)
        self._draw_grid()
        self._show_message(f"Added new piece '{sanitized}'.")

    def import_png(self):
        if Image is None or ImageOps is None:
            self._show_message("Pillow not installed; cannot import PNG.")
            return
        if not self.pieces:
            self._show_message("No piece selected.")
            return
        path = filedialog.askopenfilename(
            title="Import PNG",
            filetypes=[("PNG files", "*.png")],
        )
        if not path:
            return
        try:
            img = Image.open(path).convert("RGBA")
            fitted = ImageOps.contain(img, (TARGET_SIZE, TARGET_SIZE), method=Image.NEAREST)
            canvas = Image.new("RGBA", (TARGET_SIZE, TARGET_SIZE), (255, 0, 255, 0))
            x_off = (TARGET_SIZE - fitted.width) // 2
            y_off = (TARGET_SIZE - fitted.height) // 2
            canvas.paste(fitted, (x_off, y_off), fitted)
            grid = [[0 for _ in range(TARGET_SIZE)] for _ in range(TARGET_SIZE)]
            pixels = canvas.load()
            for y in range(TARGET_SIZE):
                for x in range(TARGET_SIZE):
                    r, g, b, a = pixels[x, y]
                    if a < 128:
                        val = 0
                    else:
                        lum = (r * 299 + g * 587 + b * 114) // 1000
                        val = 1 if lum >= 128 else 2
                    grid[y][x] = val
            self.pieces[self.current_idx]["grid"] = grid
            self._draw_grid()
            self._show_message(f"Imported PNG into {self.pieces[self.current_idx]['name']}")
        except Exception as exc:
            self._show_message(f"Import failed: {exc}")

    def export_png(self):
        if Image is None:
            self._show_message("Pillow not installed; cannot export PNG.")
            return
        if not self.pieces:
            self._show_message("No piece selected.")
            return
        path = filedialog.asksaveasfilename(
            title="Export PNG",
            defaultextension=".png",
            filetypes=[("PNG files", "*.png")],
        )
        if not path:
            return
        try:
            img = Image.new("RGBA", (TARGET_SIZE, TARGET_SIZE))
            px = []
            for y in range(TARGET_SIZE):
                for x in range(TARGET_SIZE):
                    val = self.pieces[self.current_idx]["grid"][y][x]
                    if val == 1:
                        px.append((245, 245, 245, 255))
                    elif val == 2:
                        px.append((30, 30, 30, 255))
                    else:
                        px.append((255, 0, 255, 0))
            img.putdata(px)
            img.save(path)
            self._show_message(f"Exported PNG for {self.pieces[self.current_idx]['name']}")
        except Exception as exc:
            self._show_message(f"Export failed: {exc}")


if __name__ == "__main__":
    app = PieceEditor()
    app.mainloop()
