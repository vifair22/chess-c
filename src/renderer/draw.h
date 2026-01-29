#pragma once

#include <SDL.h>
#include <stdint.h>

void draw_filled_circle(SDL_Renderer *renderer, int32_t cx, int32_t cy, int32_t radius);
void draw_filled_rect(SDL_Renderer *renderer, int32_t x, int32_t y, int32_t w, int32_t h);
void draw_digit(SDL_Renderer *renderer, int x, int y, int size, SDL_Color color, int digit);
void draw_number(SDL_Renderer *renderer, int x, int y, int size, SDL_Color color, int value);
// Simple 5x7 bitmap font for UI labels.
void draw_text(SDL_Renderer *renderer, int x, int y, int scale, SDL_Color color, const char *text);
int measure_text_width(const char *text, int scale);
int measure_text_lines(const char *text);
