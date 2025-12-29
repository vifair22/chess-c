#include "renderer/render.h"

#include <stdint.h>
#include <stdio.h>
#include <ctype.h>

#include "game/logic.h"
#include "renderer/draw.h"
#include "renderer/palette.h"
#include "renderer/debug_overlay.h"
#include "renderer/ui.h"

typedef struct {
    char id;
    uint8_t rows[7]; // 5-bit wide bitmap per row
} Glyph;

static const Glyph GLYPHS[] = {
    // 5x7 silhouettes for chess pieces (rows are MSB-left).
    {'P', {0x04, 0x0E, 0x04, 0x0E, 0x0E, 0x1F, 0x0A}}, // pawn: head, stem, rounded base
    {'N', {0x06, 0x0E, 0x0C, 0x0F, 0x07, 0x1F, 0x0E}}, // knight: arched neck + jaw
    {'B', {0x04, 0x0E, 0x04, 0x0E, 0x0A, 0x1F, 0x0A}}, // bishop: mitre notch and base
    {'R', {0x15, 0x1F, 0x1F, 0x0E, 0x0E, 0x1F, 0x1F}}, // rook: crenellations + tower
    {'Q', {0x15, 0x0A, 0x1F, 0x0E, 0x1F, 0x1F, 0x0E}}, // queen: crown with jewel
    {'K', {0x04, 0x1F, 0x0A, 0x0E, 0x0A, 0x1F, 0x0E}}, // king: cross and brim
};

static SDL_Texture *load_texture(SDL_Renderer *renderer, const char *path) {
    SDL_Surface *surf = SDL_LoadBMP(path);
    if (!surf) {
        SDL_Log("SDL_LoadBMP failed for %s: %s", path, SDL_GetError());
        return NULL;
    }
    SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, surf);
    SDL_FreeSurface(surf);
    if (!tex) {
        SDL_Log("SDL_CreateTextureFromSurface failed for %s: %s", path, SDL_GetError());
    }
    return tex;
}

static SDL_Texture *texture_for_piece(RenderContext *ctx, char piece) {
    // Order: 0-5 white (P N B R Q K), 6-11 black
    int idx = -1;
    switch (piece) {
        case 'P': idx = 0; break;
        case 'N': idx = 1; break;
        case 'B': idx = 2; break;
        case 'R': idx = 3; break;
        case 'Q': idx = 4; break;
        case 'K': idx = 5; break;
        case 'p': idx = 6; break;
        case 'n': idx = 7; break;
        case 'b': idx = 8; break;
        case 'r': idx = 9; break;
        case 'q': idx = 10; break;
        case 'k': idx = 11; break;
        default: break;
    }
    if (idx < 0) return NULL;
    return ctx->piece_textures[idx];
}

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
    SDL_SetRenderDrawBlendMode(ctx->renderer, SDL_BLENDMODE_BLEND);

    ctx->window_width = width;
    ctx->window_height = height;

    // Load chess piece textures (BMP versions of provided PNG assets).
    const char *paths[12] = {
        "assets/chess/white_pawn.bmp",
        "assets/chess/white_knight.bmp",
        "assets/chess/white_bishop.bmp",
        "assets/chess/white_rook.bmp",
        "assets/chess/white_queen.bmp",
        "assets/chess/white_king.bmp",
        "assets/chess/black_pawn.bmp",
        "assets/chess/black_knight.bmp",
        "assets/chess/black_bishop.bmp",
        "assets/chess/black_rook.bmp",
        "assets/chess/black_queen.bmp",
        "assets/chess/black_king.bmp",
    };
    for (int i = 0; i < 12; ++i) {
        ctx->piece_textures[i] = load_texture(ctx->renderer, paths[i]);
    }
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

static void draw_checker_piece(SDL_Renderer *renderer, char piece, const SDL_Rect *cell_rect, SDL_Color color) {
    int radius = cell_rect->w / 2 - cell_rect->w / 8;
    if (radius < 6) radius = cell_rect->w / 3;
    int cx = cell_rect->x + cell_rect->w / 2;
    int cy = cell_rect->y + cell_rect->h / 2;
    draw_filled_circle(renderer, cx, cy, radius);
    if (piece == 'K' || piece == 'k') {
        SDL_SetRenderDrawColor(renderer, kPalette.panel_border.r, kPalette.panel_border.g, kPalette.panel_border.b, kPalette.panel_border.a);
        int inner = radius - 4;
        if (inner > 2) {
            draw_filled_circle(renderer, cx, cy, inner);
            SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
            int inner2 = inner - 3;
            if (inner2 > 1) {
                draw_filled_circle(renderer, cx, cy, inner2);
            }
        }
    }
}

static void draw_board(RenderContext *ctx, const GameState *state, int offset_x, int offset_y, int cell) {
    SDL_Renderer *renderer = ctx->renderer;
    int hover_file = state->hover_pos.file;
    int hover_rank = state->hover_pos.rank;
    for (int rank = 0; rank < 8; ++rank) {
        for (int file = 0; file < 8; ++file) {
            const SquareState *sq = &state->board_state.squares[rank][file];
            SDL_Color square = ((file + rank) % 2 == 0) ? kPalette.board_light : kPalette.board_dark;
            SDL_Rect r = {offset_x + file * cell, offset_y + rank * cell, cell, cell};

            SDL_SetRenderDrawColor(renderer, square.r, square.g, square.b, square.a);
            SDL_RenderFillRect(renderer, &r);

            if (state->selected_file == file && state->selected_rank == rank) {
                draw_highlight(renderer, r, kPalette.select_fill);
                SDL_SetRenderDrawColor(renderer, kPalette.select_edge.r, kPalette.select_edge.g, kPalette.select_edge.b, kPalette.select_edge.a);
                SDL_RenderDrawRect(renderer, &r);
            }

            if (sq->flags & SQUARE_LEGAL) {
                draw_highlight(renderer, r, kPalette.legal_fill);
            }
            if (sq->flags & SQUARE_HIGHLIGHT) {
                draw_highlight(renderer, r, kPalette.highlight_fill);
            }
            if (sq->flags & SQUARE_BLOCKED) {
                draw_highlight(renderer, r, kPalette.blocked_fill);
            }
            if (hover_file == file && hover_rank == rank) {
                draw_highlight(renderer, r, kPalette.hover_fill);
            }
            if (sq->flags & SQUARE_CHECK) {
                draw_highlight(renderer, r, kPalette.check_fill);
            }

            char piece = sq->piece;
            if (piece != '.') {
                SDL_Texture *tex = texture_for_piece(ctx, piece);
                if (tex) {
                    int tw = 0, th = 0;
                    SDL_QueryTexture(tex, NULL, NULL, &tw, &th);
                    int target = cell * 9 / 10;
                    if (target < 8) target = cell;
                    int denom = (tw > th) ? tw : th;
                    double scale = denom > 0 ? (double)target / (double)denom : 1.0;
                    int dw = (int)(tw * scale);
                    int dh = (int)(th * scale);
                    SDL_Rect dst = {
                        r.x + (cell - dw) / 2,
                        r.y + (cell - dh) / 2,
                        dw,
                        dh
                    };
                    SDL_RenderCopy(renderer, tex, NULL, &dst);
                } else {
                    SDL_Color piece_color = (piece >= 'a' && piece <= 'z') ? kPalette.piece_dark : kPalette.piece_light;
                    const Glyph *glyph = find_glyph(piece);
                    if (glyph) {
                        draw_glyph(renderer, glyph, r.x, r.y, cell, piece_color);
                    } else {
                        SDL_SetRenderDrawColor(renderer, piece_color.r, piece_color.g, piece_color.b, piece_color.a);
                        draw_checker_piece(renderer, piece, &r, piece_color);
                    }
                }
            }
        }
    }
}

static void draw_board_labels(SDL_Renderer *renderer, int offset_x, int offset_y, int cell, int board_size, int window_width, int window_height) {
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
            draw_number(renderer, rank_x, y, digit_size, kPalette.label_text, value);
        }
    }

    int file_y = offset_y + board_size + text_scale;
    for (int file = 0; file < 8; ++file) {
        char label[2] = {(char)('a' + file), '\0'};
        int center_x = offset_x + file * cell + cell / 2;
        int x = center_x - glyph_w / 2;
        if (file_y + glyph_h <= window_height) {
        draw_text(renderer, x, file_y, text_scale, kPalette.label_text, label);
        }
    }
}

static void draw_hud(SDL_Renderer *renderer, const GameState *state, int window_width) {
    (void)renderer;
    (void)state;
    (void)window_width;
}

static void draw_debug_overlay(SDL_Renderer *renderer, const GameState *state, int window_width, int window_height) {
    int scale = 2;
    int padding = 10;
    char body[256];

    // In this loop, ticks and rendering are aligned; show tick rate (fps equivalent).
    double fps = state->last_fps;
    double ms = state->last_frame_seconds * 1000.0;

    snprintf(body, sizeof(body),
        "MODE: %s\n"
        "PHASE: %s\n"
        "TURN: %d\n"
        "TICK: %.1f/s (%.2f ms)",
        state->game_mode,
        state->game_phase,
        state->turn_number,
        fps,
        ms);

    int text_width = measure_text_width(body, scale);
    int lines = measure_text_lines(body);
    int line_height = (7 + 1) * scale;
    int text_height = lines * line_height;

    int box_w = text_width + padding * 2;
    int box_h = text_height + padding * 2;
    SDL_Rect box = {padding, window_height - box_h - padding, box_w, box_h};

    SDL_Color bg = kPalette.panel_backdrop;
    bg.a = 160; // translucent background for readability
    SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a);
    SDL_RenderFillRect(renderer, &box);

    draw_text(renderer, box.x + padding, box.y + padding, scale, kPalette.panel_text, body);
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

    SDL_SetRenderDrawColor(ctx->renderer, kPalette.background.r, kPalette.background.g, kPalette.background.b, kPalette.background.a);
    SDL_RenderClear(ctx->renderer);

    BoardGeometry geo = render_compute_board_geometry(ctx);

    draw_board(ctx, &snapshot, geo.offset_x, geo.offset_y, geo.cell);
    draw_board_labels(ctx->renderer, geo.offset_x, geo.offset_y, geo.cell, geo.board_size, ctx->window_width, ctx->window_height);

    if (snapshot.show_main_menu) {
        ui_draw_main_menu(ctx, &snapshot);
    } else if (snapshot.show_pause_menu) {
        ui_draw_pause_menu(ctx, &snapshot);
    } else if (strncmp(snapshot.game_phase, "checkmate", 9) == 0 ||
               strncmp(snapshot.game_phase, "stalemate", 9) == 0 ||
               strncmp(snapshot.game_phase, "win_", 4) == 0 ||
               strncmp(snapshot.game_phase, "draw", 4) == 0) {
        ui_draw_win_screen(ctx, &snapshot);
    }
    if (snapshot.show_debug) {
        debug_overlay_render(ctx, &snapshot);
    }

    SDL_RenderPresent(ctx->renderer);
}

void render_shutdown(RenderContext *ctx) {
    for (int i = 0; i < 12; ++i) {
        if (ctx->piece_textures[i]) {
            SDL_DestroyTexture(ctx->piece_textures[i]);
            ctx->piece_textures[i] = NULL;
        }
    }
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
