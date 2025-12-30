#include "renderer/render.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "game/logic.h"
#include "renderer/draw.h"
#include "renderer/palette.h"
#include "renderer/debug_overlay.h"
#include "renderer/ui.h"

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
        return NULL;
    }
    if (SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND) != 0) {
        SDL_Log("SDL_SetTextureBlendMode failed for %s: %s", path, SDL_GetError());
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

static bool draw_piece_texture(SDL_Renderer *renderer, SDL_Texture *tex, int cell, int center_x, int center_y) {
    if (!tex) {
        return false;
    }

    int tw = 0, th = 0;
    if (SDL_QueryTexture(tex, NULL, NULL, &tw, &th) != 0 || tw <= 0 || th <= 0) {
        return false;
    }

    int target = cell * 9 / 10;
    if (target < 8) target = cell;
    int denom = (tw > th) ? tw : th;
    if (denom <= 0) {
        return false;
    }
    double scale = (double)target / (double)denom;
    int dw = (int)(tw * scale);
    int dh = (int)(th * scale);
    if (dw <= 0 || dh <= 0) {
        return false;
    }

    SDL_Rect dst = {
        center_x - dw / 2,
        center_y - dh / 2,
        dw,
        dh
    };
    SDL_Rect shadow = dst;
    shadow.x += cell / 12;
    shadow.y += cell / 12;

    SDL_SetTextureColorMod(tex, 30, 30, 30);
    SDL_SetTextureAlphaMod(tex, 180);
    if (SDL_RenderCopy(renderer, tex, NULL, &shadow) != 0) {
        SDL_Log("SDL_RenderCopy (shadow) failed: %s", SDL_GetError());
        return false;
    }
    SDL_SetTextureColorMod(tex, 255, 255, 255);
    SDL_SetTextureAlphaMod(tex, 255);
    if (SDL_RenderCopy(renderer, tex, NULL, &dst) != 0) {
        SDL_Log("SDL_RenderCopy (piece) failed: %s", SDL_GetError());
        return false;
    }
    return true;
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
    ctx->last_turn_number = 0;
    ctx->anim.active = false;
    memset(ctx->legal_alpha, 0, sizeof(ctx->legal_alpha));
    memset(ctx->highlight_alpha, 0, sizeof(ctx->highlight_alpha));
    memset(ctx->blocked_alpha, 0, sizeof(ctx->blocked_alpha));
    memset(ctx->hover_alpha, 0, sizeof(ctx->hover_alpha));
    memset(ctx->check_alpha, 0, sizeof(ctx->check_alpha));

    // Load chess piece textures.
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

static void draw_board(RenderContext *ctx, const GameState *state, int offset_x, int offset_y, int cell, Uint32 now_ms) {
    SDL_Renderer *renderer = ctx->renderer;
    int hover_file = state->hover_pos.file;
    int hover_rank = state->hover_pos.rank;
    const float fade_rate = 0.25f;
    // Expire animation if needed.
    if (ctx->anim.active) {
        Uint32 elapsed = now_ms - ctx->anim.start_ms;
        if (elapsed >= ctx->anim.duration_ms) {
            ctx->anim.active = false;
        }
    }
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

            // Smooth overlay fades.
            float target_legal = (sq->flags & SQUARE_LEGAL) ? 1.0f : 0.0f;
            float target_highlight = (sq->flags & SQUARE_HIGHLIGHT) ? 1.0f : 0.0f;
            float target_blocked = (sq->flags & SQUARE_BLOCKED) ? 1.0f : 0.0f;
            float target_hover = (hover_file == file && hover_rank == rank) ? 1.0f : 0.0f;
            float target_check = (sq->flags & SQUARE_CHECK) ? 1.0f : 0.0f;
            float *la = &ctx->legal_alpha[rank][file];
            float *ha = &ctx->highlight_alpha[rank][file];
            float *ba = &ctx->blocked_alpha[rank][file];
            float *ho = &ctx->hover_alpha[rank][file];
            float *ca = &ctx->check_alpha[rank][file];
            #define FADE(cur, tgt) do { (cur) += (tgt - (cur)) * fade_rate; if ((cur) < 0.001f) (cur) = 0.0f; } while(0)
            FADE(*la, target_legal);
            FADE(*ha, target_highlight);
            FADE(*ba, target_blocked);
            FADE(*ho, target_hover);
            FADE(*ca, target_check);
            #undef FADE

            if (*la > 0.0f) {
                SDL_Color c = kPalette.legal_fill;
                c.a = (Uint8)((float)c.a * (*la));
                draw_highlight(renderer, r, c);
            }
            if (*ha > 0.0f) {
                SDL_Color c = kPalette.highlight_fill;
                c.a = (Uint8)((float)c.a * (*ha));
                draw_highlight(renderer, r, c);
            }
            if (*ba > 0.0f) {
                SDL_Color c = kPalette.blocked_fill;
                c.a = (Uint8)((float)c.a * (*ba));
                draw_highlight(renderer, r, c);
            }
            if (*ho > 0.0f) {
                SDL_Color c = kPalette.hover_fill;
                c.a = (Uint8)((float)c.a * (*ho));
                draw_highlight(renderer, r, c);
            }
            if (*ca > 0.0f) {
                SDL_Color c = kPalette.check_fill;
                c.a = (Uint8)((float)c.a * (*ca));
                draw_highlight(renderer, r, c);
            }

            char piece = sq->piece;
            if (piece != '.') {
                bool drawn = false;
                // Skip drawing the piece on its destination square while an animation is active;
                // the animated overlay is drawn once after the grid to avoid missing cases.
                if (ctx->anim.active &&
                    ctx->anim.to_file == file && ctx->anim.to_rank == rank &&
                    ctx->anim.piece == piece) {
                    continue;
                }
                SDL_Texture *tex = texture_for_piece(ctx, piece);
                if (draw_piece_texture(renderer, tex, cell, r.x + cell / 2, r.y + cell / 2)) {
                    // drawn via texture
                } else {
                    SDL_Color piece_color = (piece >= 'a' && piece <= 'z') ? kPalette.piece_dark : kPalette.piece_light;
                    SDL_SetRenderDrawColor(renderer, piece_color.r, piece_color.g, piece_color.b, piece_color.a);
                    draw_checker_piece(renderer, piece, &r, piece_color);
                }
            }
        }
    }

    // Draw the animated piece once as an overlay so it renders regardless of board traversal order.
    if (ctx->anim.active) {
        Uint32 elapsed = now_ms - ctx->anim.start_ms;
        float t = ctx->anim.duration_ms > 0 ? (float)elapsed / (float)ctx->anim.duration_ms : 1.0f;
        if (t >= 1.0f) {
            ctx->anim.active = false;
            t = 1.0f;
        }
        t = t * t * (3.0f - 2.0f * t); // smoothstep
        float x = ctx->anim.from_px.x + (ctx->anim.to_px.x - ctx->anim.from_px.x) * t;
        float y = ctx->anim.from_px.y + (ctx->anim.to_px.y - ctx->anim.from_px.y) * t;
        SDL_Texture *tex = texture_for_piece(ctx, ctx->anim.piece);
        if (!tex) {
            SDL_Log("anim overlay skipped: no texture for piece %c", ctx->anim.piece);
        } else if (!draw_piece_texture(renderer, tex, cell, (int)x, (int)y)) {
            SDL_Log("anim overlay draw failed for piece %c", ctx->anim.piece);
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

    // Start a piece animation when the turn number advances.
    if (snapshot.turn_number != ctx->last_turn_number &&
        snapshot.move_start.file >= 0 && snapshot.move_start.rank >= 0 &&
        snapshot.move_end.file >= 0 && snapshot.move_end.rank >= 0) {
        ctx->anim.active = true;
        ctx->anim.piece = snapshot.board_state.squares[snapshot.move_end.rank][snapshot.move_end.file].piece;
        ctx->anim.from_file = snapshot.move_start.file;
        ctx->anim.from_rank = snapshot.move_start.rank;
        ctx->anim.to_file = snapshot.move_end.file;
        ctx->anim.to_rank = snapshot.move_end.rank;
        ctx->anim.start_ms = SDL_GetTicks();
        ctx->anim.duration_ms = 180;
        ctx->anim.from_px.x = geo.offset_x + (snapshot.move_start.file * geo.cell) + geo.cell / 2.0f;
        ctx->anim.from_px.y = geo.offset_y + (snapshot.move_start.rank * geo.cell) + geo.cell / 2.0f;
        ctx->anim.to_px.x = geo.offset_x + (snapshot.move_end.file * geo.cell) + geo.cell / 2.0f;
        ctx->anim.to_px.y = geo.offset_y + (snapshot.move_end.rank * geo.cell) + geo.cell / 2.0f;
    }
    ctx->last_turn_number = snapshot.turn_number;

    Uint32 now_ms = SDL_GetTicks();

    draw_board(ctx, &snapshot, geo.offset_x, geo.offset_y, geo.cell, now_ms);
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
