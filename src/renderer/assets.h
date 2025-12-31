#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct {
    char ch;
    uint8_t rows[7];
} Glyph5x7;

typedef struct {
    const char *name;
    uint8_t width;
    uint8_t height;
    // SDL-ready pixels in 32-bit RGBA format (A in the highest byte).
    const uint32_t *pixels;
} PieceSprite;

extern const Glyph5x7 kGlyphs[];
extern const size_t kGlyphCount;

extern const uint8_t kSegments[10][7];

extern const PieceSprite kPieceSprites[];
extern const size_t kPieceSpriteCount;
