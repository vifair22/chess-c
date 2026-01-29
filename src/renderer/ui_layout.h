#ifndef UI_LAYOUT_H
#define UI_LAYOUT_H

#include <SDL.h>
#include <stdbool.h>
#include "game/logic.h"

// Layout structures
typedef struct {
    SDL_Rect panel;
    int scale;
    int body_x;
    int body_y;
    int line_height;
} MenuLayout;

typedef struct {
    int scale;
    int line_height;
    int button_w;
    int button_h;
    int button_x;
    int button_start_y;
} MainMenuLayout;

typedef struct {
    SDL_Rect new_game;
    SDL_Rect load;
    SDL_Rect replays;
    SDL_Rect quit;
} MenuButtons;

typedef struct {
    SDL_Rect chess;
    SDL_Rect checkers;
    SDL_Rect ai_side;
    SDL_Rect depth[9];
    SDL_Rect start;
    SDL_Rect back;
} NewGameButtons;

// Layout calculation functions
MenuLayout menu_layout(int window_w, int window_h);
MainMenuLayout main_menu_layout(int window_w, int window_h);
MenuButtons menu_buttons(const MenuLayout *layout);
MenuButtons main_menu_buttons(const MainMenuLayout *layout);
NewGameButtons new_game_buttons(const MenuLayout *layout);

// Helper functions
SDL_Rect center_rect(int window_w, int window_h, int w, int h);
int ui_scale_for_window(int window_w, int divisor, int min_scale);
int ui_line_height(int scale);
bool point_in_rect(int x, int y, SDL_Rect rect);
void button_column(int x, int start_y, int width, int height, int gap, SDL_Rect *out, int count);
void menu_vertical_buttons(const MenuLayout *layout, SDL_Rect *out, int count, int start_y);

// Formatting helpers
void format_square(int file, int rank, char *out, size_t cap);
void format_last_move(const GameState *state, char *out, size_t cap);

#endif // UI_LAYOUT_H
