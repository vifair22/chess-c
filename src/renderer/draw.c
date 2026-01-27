#include "renderer/draw.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include "renderer/assets.h"

static const Glyph5x7 *find_glyph(char c) {
    if (c >= 'a' && c <= 'z') {
        c = (char)(c - 'a' + 'A');
    }
    for (size_t i = 0; i < kGlyphCount; ++i) {
        if (kGlyphs[i].ch == c) {
            return &kGlyphs[i];
        }
    }
    return NULL;
}

static void draw_filled_rect_internal(SDL_Renderer *renderer, int32_t x, int32_t y, int32_t w, int32_t h) {
    SDL_Rect r = {x, y, w, h};
    SDL_RenderFillRect(renderer, &r);
}

void draw_filled_circle(SDL_Renderer *renderer, int32_t cx, int32_t cy, int32_t radius) {
    // Draw horizontal spans across the circle for a simple fill.
    for (int32_t y = -radius; y <= radius; ++y) {
        int64_t r2 = (int64_t)radius * (int64_t)radius;
        int64_t y2 = (int64_t)y * (int64_t)y;
        int32_t dx = (int32_t)sqrtf((float)(r2 - y2));
        SDL_RenderDrawLine(renderer, cx - dx, cy + y, cx + dx, cy + y);
    }
}

void draw_filled_rect(SDL_Renderer *renderer, int32_t x, int32_t y, int32_t w, int32_t h) {
    draw_filled_rect_internal(renderer, x, y, w, h);
}

// Basic 7-seg digit drawing for scoreboard numbers.
void draw_digit(SDL_Renderer *renderer, int x, int y, int size, SDL_Color color, int digit) {
    if (digit < 0 || digit > 9) {
        return;
    }

    int t = size / 5;
    if (t < 2) {
        t = 2;
    }
    int len = size;
    int vert_h = len - t;

    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    // Horizontal: top, middle, bottom
    if (kSegments[digit][0]) {
        draw_filled_rect_internal(renderer, x + t, y, len, t);
    }
    if (kSegments[digit][3]) {
        draw_filled_rect_internal(renderer, x + t, y + len, len, t);
    }
    if (kSegments[digit][6]) {
        draw_filled_rect_internal(renderer, x + t, y + 2 * len, len, t);
    }

    // Vertical: upper left, upper right, lower left, lower right
    if (kSegments[digit][1]) {
        draw_filled_rect_internal(renderer, x, y + t, t, vert_h);
    }
    if (kSegments[digit][2]) {
        draw_filled_rect_internal(renderer, x + len + t, y + t, t, vert_h);
    }
    if (kSegments[digit][4]) {
        draw_filled_rect_internal(renderer, x, y + len + t, t, vert_h);
    }
    if (kSegments[digit][5]) {
        draw_filled_rect_internal(renderer, x + len + t, y + len + t, t, vert_h);
    }
}

void draw_number(SDL_Renderer *renderer, int x, int y, int size, SDL_Color color, int value) {
    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%d", value);
    int t = size / 5;
    if (t < 2) {
        t = 2;
    }
    int digit_advance = size + 3 * t + 6; // width of a digit plus spacing
    for (int i = 0; buffer[i] != '\0'; ++i) {
        int digit = buffer[i] - '0';
        if (digit >= 0 && digit <= 9) {
            draw_digit(renderer, x + i * digit_advance, y, size, color, digit);
        }
    }
}

void draw_text(SDL_Renderer *renderer, int x, int y, int scale, SDL_Color color, const char *text) {
    const int glyph_w = 5;
    const int glyph_h = 7;
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    int origin_x = x;
    for (const char *p = text; *p; ++p) {
        if (*p == '\n') {
            y += (glyph_h + 1) * scale;
            x = origin_x;
            continue;
        }
        if (*p == ' ') {
            x += (glyph_w + 1) * scale;
            continue;
        }
        const Glyph5x7 *g = find_glyph(*p);
        if (!g) {
            x += (glyph_w + 1) * scale;
            continue;
        }
        for (int row = 0; row < glyph_h; ++row) {
            uint8_t bits = g->rows[row];
            for (int col = 0; col < glyph_w; ++col) {
                if (bits & (1 << (glyph_w - 1 - col))) {
                    draw_filled_rect_internal(renderer,
                        x + col * scale,
                        y + row * scale,
                        scale,
                        scale);
                }
            }
        }
        x += (glyph_w + 1) * scale;
    }
}
