#include "renderer/draw.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    char ch;
    uint8_t rows[7];
} Glyph5x7;

// Minimal uppercase + digits 5x7 font for simple UI labels.
static const Glyph5x7 kGlyphs[] = {
    {'A', {0x1E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'B', {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}},
    {'C', {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}},
    {'D', {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E}},
    {'E', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}},
    {'F', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}},
    {'G', {0x0E, 0x11, 0x10, 0x13, 0x11, 0x11, 0x0E}},
    {'H', {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'I', {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'J', {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C}},
    {'K', {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}},
    {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}},
    {'M', {0x11, 0x1B, 0x15, 0x11, 0x11, 0x11, 0x11}},
    {'N', {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}},
    {'O', {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'P', {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}},
    {'Q', {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}},
    {'R', {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}},
    {'S', {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}},
    {'T', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
    {'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'V', {0x11, 0x11, 0x11, 0x0A, 0x0A, 0x04, 0x04}},
    {'W', {0x11, 0x11, 0x11, 0x11, 0x15, 0x1B, 0x11}},
    {'X', {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}},
    {'Y', {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}},
    {'Z', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}},
    {'0', {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}},
    {'1', {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x1F}},
    {'2', {0x0E, 0x11, 0x01, 0x06, 0x08, 0x10, 0x1F}},
    {'3', {0x1F, 0x01, 0x02, 0x06, 0x01, 0x01, 0x1E}},
    {'4', {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}},
    {'5', {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}},
    {'6', {0x0E, 0x10, 0x1E, 0x11, 0x11, 0x11, 0x0E}},
    {'7', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
    {'8', {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}},
    {'9', {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x0E}},
    {':', {0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00}},
    {'-', {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}},
    {'>', {0x10, 0x08, 0x04, 0x02, 0x04, 0x08, 0x10}},
    {'?', {0x0E, 0x11, 0x02, 0x04, 0x04, 0x00, 0x04}},
    {'_', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F}},
    {'!', {0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04}},
};

static const Glyph5x7 *find_glyph(char c) {
    if (c >= 'a' && c <= 'z') {
        c = (char)(c - 'a' + 'A');
    }
    for (size_t i = 0; i < sizeof(kGlyphs) / sizeof(kGlyphs[0]); ++i) {
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
        int32_t dx = (int32_t)sqrtf((float)(radius * radius - y * y));
        SDL_RenderDrawLine(renderer, cx - dx, cy + y, cx + dx, cy + y);
    }
}

void draw_filled_rect(SDL_Renderer *renderer, int32_t x, int32_t y, int32_t w, int32_t h) {
    draw_filled_rect_internal(renderer, x, y, w, h);
}

// Basic 7-seg digit drawing for scoreboard numbers.
void draw_digit(SDL_Renderer *renderer, int x, int y, int size, SDL_Color color, int digit) {
    static const int segments[10][7] = {
        {1, 1, 1, 0, 1, 1, 1}, // 0
        {0, 0, 1, 0, 0, 1, 0}, // 1
        {1, 0, 1, 1, 1, 0, 1}, // 2
        {1, 0, 1, 1, 0, 1, 1}, // 3
        {0, 1, 1, 1, 0, 1, 0}, // 4
        {1, 1, 0, 1, 0, 1, 1}, // 5
        {1, 1, 0, 1, 1, 1, 1}, // 6
        {1, 0, 1, 0, 0, 1, 0}, // 7
        {1, 1, 1, 1, 1, 1, 1}, // 8
        {1, 1, 1, 1, 0, 1, 1}, // 9
    };
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
    if (segments[digit][0]) {
        draw_filled_rect_internal(renderer, x + t, y, len, t);
    }
    if (segments[digit][3]) {
        draw_filled_rect_internal(renderer, x + t, y + len, len, t);
    }
    if (segments[digit][6]) {
        draw_filled_rect_internal(renderer, x + t, y + 2 * len, len, t);
    }

    // Vertical: upper left, upper right, lower left, lower right
    if (segments[digit][1]) {
        draw_filled_rect_internal(renderer, x, y + t, t, vert_h);
    }
    if (segments[digit][2]) {
        draw_filled_rect_internal(renderer, x + len + t, y + t, t, vert_h);
    }
    if (segments[digit][4]) {
        draw_filled_rect_internal(renderer, x, y + len + t, t, vert_h);
    }
    if (segments[digit][5]) {
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
