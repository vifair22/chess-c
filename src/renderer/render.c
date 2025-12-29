#include "renderer/render.h"

#include <stdint.h>
#include <stdio.h>

#include "game/logic.h"
#include "renderer/draw.h"

typedef struct {
    char id;
    uint8_t rows[7]; // 5-bit wide bitmap per row
} Glyph;

static const Glyph GLYPHS[] = {
    {'P', {0x1E, 0x11, 0x1E, 0x10, 0x10, 0x10, 0x00}}, // 11110,10001,...
    {'N', {0x19, 0x15, 0x15, 0x15, 0x15, 0x15, 0x00}},
    {'B', {0x1E, 0x10, 0x1C, 0x12, 0x12, 0x1C, 0x00}},
    {'R', {0x1F, 0x11, 0x1F, 0x11, 0x11, 0x11, 0x00}},
    {'Q', {0x1E, 0x11, 0x1E, 0x11, 0x11, 0x1F, 0x00}},
    {'K', {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x00}},
};

static const Glyph *find_glyph(char piece) {
    char upper = (piece >= 'a' && piece <= 'z') ? (char)(piece - 32) : piece;
    for (size_t i = 0; i < sizeof(GLYPHS) / sizeof(GLYPHS[0]); ++i) {
        if (GLYPHS[i].id == upper) {
            return &GLYPHS[i];
        }
    }
    return NULL;
}

bool render_init(RenderContext *ctx, const char *title, int width, int height) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return false;
    }

    ctx->window = SDL_CreateWindow(title,
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        width, height,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!ctx->window) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        return false;
    }

    ctx->renderer = SDL_CreateRenderer(ctx->window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ctx->renderer) {
        SDL_Log("SDL_CreateRenderer failed: %s", SDL_GetError());
        return false;
    }

    ctx->window_width = width;
    ctx->window_height = height;
    return true;
}

static void draw_glyph(SDL_Renderer *renderer, const Glyph *glyph, int x, int y, int size, SDL_Color color) {
    if (!glyph) {
        return;
    }

    const int glyph_w = 5;
    const int glyph_h = 7;
    int scale = size / 6;
    if (scale < 2) {
        scale = 2;
    }
    int start_x = x + (size - glyph_w * scale) / 2;
    int start_y = y + (size - glyph_h * scale) / 2;

    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    for (int row = 0; row < glyph_h; ++row) {
        uint8_t bits = glyph->rows[row];
        for (int col = 0; col < glyph_w; ++col) {
            if (bits & (1 << (glyph_w - 1 - col))) {
                SDL_Rect pixel = {start_x + col * scale, start_y + row * scale, scale, scale};
                SDL_RenderFillRect(renderer, &pixel);
            }
        }
    }
}

static void draw_highlight(SDL_Renderer *renderer, SDL_Rect rect, SDL_Color color) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderFillRect(renderer, &rect);
}

static void draw_board(SDL_Renderer *renderer, const GameState *state, int offset_x, int offset_y, int cell) {
    SDL_Color light = {255, 102, 255, 255};
    SDL_Color dark = {118, 150, 86, 255};
    SDL_Color select_fill = {246, 211, 101, 120};
    SDL_Color select_edge = {209, 139, 71, 255};
    SDL_Color legal_fill = {120, 200, 140, 120};
    SDL_Color hover_fill = {120, 160, 220, 100};
    SDL_Color blocked_fill = {200, 90, 90, 70};
    SDL_Color highlight_fill = {210, 180, 80, 120};

    int hover_file = state->hover_pos.file;
    int hover_rank = state->hover_pos.rank;
    for (int rank = 0; rank < 8; ++rank) {
        for (int file = 0; file < 8; ++file) {
            const SquareState *sq = &state->board_state.squares[rank][file];
            SDL_Color square = ((file + rank) % 2 == 0) ? light : dark;
            SDL_Rect r = {offset_x + file * cell, offset_y + rank * cell, cell, cell};

            SDL_SetRenderDrawColor(renderer, square.r, square.g, square.b, square.a);
            SDL_RenderFillRect(renderer, &r);

            if (state->selected_file == file && state->selected_rank == rank) {
                draw_highlight(renderer, r, select_fill);
                SDL_SetRenderDrawColor(renderer, select_edge.r, select_edge.g, select_edge.b, select_edge.a);
                SDL_RenderDrawRect(renderer, &r);
            }

            if (sq->flags & SQUARE_LEGAL) {
                draw_highlight(renderer, r, legal_fill);
            }
            if (sq->flags & SQUARE_HIGHLIGHT) {
                draw_highlight(renderer, r, highlight_fill);
            }
            if (sq->flags & SQUARE_BLOCKED) {
                draw_highlight(renderer, r, blocked_fill);
            }
            if (hover_file == file && hover_rank == rank) {
                draw_highlight(renderer, r, hover_fill);
            }

            char piece = sq->piece;
            if (piece != '.') {
                SDL_Color piece_color = (piece >= 'a' && piece <= 'z') ?
                    (SDL_Color){40, 40, 40, 255} : (SDL_Color){240, 240, 240, 255};
                draw_glyph(renderer, find_glyph(piece), r.x, r.y, cell, piece_color);
            }
        }
    }
}

static void draw_board_labels(SDL_Renderer *renderer, int offset_x, int offset_y, int cell, int board_size, int window_width, int window_height) {
    SDL_Color label_color = {212, 220, 228, 230};
    int text_scale = cell / 6;
    if (text_scale < 2) {
        text_scale = 2;
    }
    int glyph_w = 5 * text_scale;
    int glyph_h = 7 * text_scale;

    int digit_size = cell / 3;
    if (digit_size < 10) {
        digit_size = 10;
    }
    int digit_thickness = digit_size / 5;
    if (digit_thickness < 2) {
        digit_thickness = 2;
    }
    int digit_height = 2 * digit_size + 3 * digit_thickness;

    int rank_x = offset_x - digit_size - digit_thickness - 4;
    for (int rank = 0; rank < 8; ++rank) {
        int value = 8 - rank;
        int y = offset_y + rank * cell + (cell - digit_height) / 2;
        if (rank_x >= 0) {
            draw_number(renderer, rank_x, y, digit_size, label_color, value);
        }
    }

    int file_y = offset_y + board_size + text_scale;
    for (int file = 0; file < 8; ++file) {
        char label[2] = {(char)('a' + file), '\0'};
        int center_x = offset_x + file * cell + cell / 2;
        int x = center_x - glyph_w / 2;
        if (file_y + glyph_h <= window_height) {
        draw_text(renderer, x, file_y, text_scale, label_color, label);
        }
    }
}

static void draw_hud(SDL_Renderer *renderer, const GameState *state, int window_width) {
    SDL_Color hud_color = {212, 220, 228, 230};
    int scale = 2;
    int padding = 12;
    char line[64];

    snprintf(line, sizeof(line), "MODE: %s", state->game_mode);
    draw_text(renderer, padding, padding, scale, hud_color, line);

    snprintf(line, sizeof(line), "PHASE: %s", state->game_phase);
    draw_text(renderer, padding, padding + 12 * scale, scale, hud_color, line);

    snprintf(line, sizeof(line), "TURN: %d", state->turn_number);
    draw_text(renderer, padding, padding + 24 * scale, scale, hud_color, line);

    int total_seconds = (int)state->game_time.total_seconds;
    int minutes = total_seconds / 60;
    int seconds = total_seconds % 60;
    snprintf(line, sizeof(line), "TIME: %02d:%02d", minutes, seconds);
    draw_text(renderer, padding, padding + 36 * scale, scale, hud_color, line);

    // Keep HUD from colliding with right edge on very small windows.
    (void)window_width;
}

BoardGeometry render_compute_board_geometry(const RenderContext *ctx) {
    BoardGeometry geo = {0};
    int label_margin = 64;
    int available_width = ctx->window_width - 2 * label_margin;
    int available_height = ctx->window_height - 2 * label_margin;
    int base_size = (available_width > 0 && available_height > 0) ?
        (available_width < available_height ? available_width : available_height) :
        (ctx->window_width < ctx->window_height ? ctx->window_width : ctx->window_height);
    int cell = base_size / 8;
    if (cell < 1) {
        cell = 1;
    }
    int board_size = cell * 8; // enforce even cells
    int offset_x = (ctx->window_width - board_size) / 2;
    int offset_y = (ctx->window_height - board_size) / 2;
    geo.offset_x = offset_x;
    geo.offset_y = offset_y;
    geo.cell = cell;
    geo.board_size = board_size;
    return geo;
}

void render_frame(RenderContext *ctx, GameState *state) {
    if (!state || !state->mutex) {
        return;
    }
    GameState snapshot = {0};

    if (SDL_LockMutex(state->mutex) != 0) {
        SDL_Log("SDL_LockMutex failed: %s", SDL_GetError());
        return;
    }
    snapshot = *state;
    SDL_UnlockMutex(state->mutex);

    SDL_GetWindowSize(ctx->window, &ctx->window_width, &ctx->window_height);

    SDL_SetRenderDrawColor(ctx->renderer, 15, 20, 26, 255);
    SDL_RenderClear(ctx->renderer);

    BoardGeometry geo = render_compute_board_geometry(ctx);

    draw_board(ctx->renderer, &snapshot, geo.offset_x, geo.offset_y, geo.cell);
    draw_board_labels(ctx->renderer, geo.offset_x, geo.offset_y, geo.cell, geo.board_size, ctx->window_width, ctx->window_height);
    draw_hud(ctx->renderer, &snapshot, ctx->window_width);

    SDL_RenderPresent(ctx->renderer);
}

void render_shutdown(RenderContext *ctx) {
    if (ctx->renderer) {
        SDL_DestroyRenderer(ctx->renderer);
        ctx->renderer = NULL;
    }
    if (ctx->window) {
        SDL_DestroyWindow(ctx->window);
        ctx->window = NULL;
    }
    SDL_Quit();
}
