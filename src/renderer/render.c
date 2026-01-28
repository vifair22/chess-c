#include "renderer/render.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "game/logic.h"
#include "renderer/draw.h"
#include "renderer/assets.h"
#include "renderer/palette.h"
#include "renderer/debug_overlay.h"
#include "renderer/ui.h"

static SDL_Texture *create_texture_from_sprite(SDL_Renderer *renderer, const PieceSprite *sprite) {
    if (!sprite || !renderer) {
        return NULL;
    }
    SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(0, sprite->width, sprite->height, 32, SDL_PIXELFORMAT_RGBA32);
    if (!surf) {
        SDL_Log("SDL_CreateRGBSurfaceWithFormat failed: %s", SDL_GetError());
        return NULL;
    }
    uint8_t *dst_bytes = (uint8_t *)surf->pixels;
    for (int y = 0; y < sprite->height; ++y) {
        uint32_t *dst = (uint32_t *)(dst_bytes + y * surf->pitch);
        const uint32_t *src = sprite->pixels + (size_t)y * sprite->width;
        memcpy(dst, src, sprite->width * sizeof(uint32_t));
    }
    SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, surf);
    SDL_FreeSurface(surf);
    if (!tex) {
        SDL_Log("SDL_CreateTextureFromSurface (mask) failed: %s", SDL_GetError());
        return NULL;
    }
    if (SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND) != 0) {
        SDL_Log("SDL_SetTextureBlendMode failed: %s", SDL_GetError());
    }
    return tex;
}

static SDL_Texture *texture_for_piece(RenderContext *ctx, char piece, bool is_checkers) {
    // Order: 0-5 white (P N B R Q K), 6-11 black, 12-15 checkers (light man/king, dark man/king)
    int idx = -1;
    if (is_checkers) {
        switch (piece) {
            case 'M': idx = 12; break;
            case 'K': idx = 13; break;
            case 'm': idx = 14; break;
            case 'k': idx = 15; break;
            default: break;
        }
    } else {
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
    }
    if (idx < 0) return NULL;
    return ctx->assets.piece_textures[idx];
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
    shadow.x += cell / 8;  // push shadow further so it doesn't peek out on the left
    shadow.y += cell / 8;

    SDL_SetTextureColorMod(tex, 0, 0, 0);   // solid dark shadow regardless of piece tint
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

    ctx->surface.window = SDL_CreateWindow(title,
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        width, height,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!ctx->surface.window) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_Quit();
        return false;
    }

    ctx->surface.renderer = SDL_CreateRenderer(ctx->surface.window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ctx->surface.renderer) {
        SDL_Log("SDL_CreateRenderer failed: %s", SDL_GetError());
        SDL_DestroyWindow(ctx->surface.window);
        ctx->surface.window = NULL;
        SDL_Quit();
        return false;
    }
    SDL_SetRenderDrawBlendMode(ctx->surface.renderer, SDL_BLENDMODE_BLEND);

    ctx->surface.window_width = width;
    ctx->surface.window_height = height;
    ctx->last_turn_number = 0;
    ctx->anim.active = false;
    memset(ctx->overlays.legal_alpha, 0, sizeof(ctx->overlays.legal_alpha));
    memset(ctx->overlays.highlight_alpha, 0, sizeof(ctx->overlays.highlight_alpha));
    memset(ctx->overlays.blocked_alpha, 0, sizeof(ctx->overlays.blocked_alpha));
    memset(ctx->overlays.hover_alpha, 0, sizeof(ctx->overlays.hover_alpha));
    memset(ctx->overlays.check_alpha, 0, sizeof(ctx->overlays.check_alpha));
    memset(ctx->assets.piece_textures, 0, sizeof(ctx->assets.piece_textures));

    // Build chess piece textures from embedded masks.
    size_t tex_cap = sizeof(ctx->assets.piece_textures) / sizeof(ctx->assets.piece_textures[0]);
    for (size_t i = 0; i < kPieceSpriteCount && i < tex_cap; ++i) {
        ctx->assets.piece_textures[i] = create_texture_from_sprite(ctx->surface.renderer, &kPieceSprites[i]);
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
    SDL_Renderer *renderer = ctx->surface.renderer;
    int hover_file = state->moves.hover_pos.file;
    int hover_rank = state->moves.hover_pos.rank;
    bool is_checkers = strncmp(state->match.mode, "checkers", 8) == 0;
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

            if (state->moves.selected.file == file && state->moves.selected.rank == rank) {
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
            float *la = &ctx->overlays.legal_alpha[rank][file];
            float *ha = &ctx->overlays.highlight_alpha[rank][file];
            float *ba = &ctx->overlays.blocked_alpha[rank][file];
            float *ho = &ctx->overlays.hover_alpha[rank][file];
            float *ca = &ctx->overlays.check_alpha[rank][file];
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
                SDL_Texture *tex = texture_for_piece(ctx, piece, is_checkers);
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
        bool is_checkers = strncmp(state->match.mode, "checkers", 8) == 0;
        SDL_Texture *tex = texture_for_piece(ctx, ctx->anim.piece, is_checkers);
        if (!tex) {
            SDL_Log("anim overlay skipped: no texture for piece %c", ctx->anim.piece);
        } else if (!draw_piece_texture(renderer, tex, cell, (int)x, (int)y)) {
            SDL_Log("anim overlay draw failed for piece %c", ctx->anim.piece);
        }
    }
}

static void draw_capture_list(RenderContext *ctx, const GameState *state, const char *list, int len,
    int start_x, int start_y, int icon_cell, int gap, bool is_checkers, bool bottom_up) {
    if (len <= 0) return;
    int rows = 8;
    int cols = 2;
    int max_slots = rows * cols;
    int total_w = cols * icon_cell + (cols - 1) * gap;
    if (start_x + total_w > ctx->surface.window_width - 2) {
        start_x = ctx->surface.window_width - 2 - total_w;
    }
    if (start_x < 2) start_x = 2;

    int total_h = rows * icon_cell + (rows - 1) * gap;
    int y = start_y;
    if (y + total_h > ctx->surface.window_height - 2) {
        y = ctx->surface.window_height - 2 - total_h;
    }
    if (y < 2) y = 2;

    int limit = len < max_slots ? len : max_slots;
    for (int i = 0; i < limit; ++i) {
        int col = i / rows;
        int row = i % rows;
        if (bottom_up) {
            row = (rows - 1) - row;
        }
        int cx = start_x + col * (icon_cell + gap) + icon_cell / 2;
        int cy = y + row * (icon_cell + gap) + icon_cell / 2;
        char piece = list[i];
        SDL_Texture *tex = texture_for_piece(ctx, piece, is_checkers);
        if (tex) {
            draw_piece_texture(ctx->surface.renderer, tex, icon_cell, cx, cy);
        } else if (is_checkers) {
            SDL_Rect cell_rect = {cx - icon_cell / 2, cy - icon_cell / 2, icon_cell, icon_cell};
            SDL_Color color = game_is_white_piece(piece) ? kPalette.piece_light : kPalette.piece_dark;
            draw_checker_piece(ctx->surface.renderer, piece, &cell_rect, color);
        }
    }
}

static void draw_captures(RenderContext *ctx, const GameState *state, const BoardGeometry *geo) {
    if (!ctx || !state || !geo) return;
    int right_margin = ctx->surface.window_width - (geo->offset_x + geo->board_size);
    int min_margin = geo->offset_x;
    if (right_margin < min_margin) return;

    int gap = geo->cell / 10;
    if (gap < 2) gap = 2;
    int text_scale = geo->cell / 20;
    if (text_scale < 2) text_scale = 2;
    int icon_cell = 7 * text_scale;
    if (icon_cell < 8) icon_cell = 8;
    int max_width = right_margin - 6;
    int total_w = 2 * icon_cell + gap;
    if (total_w > max_width && max_width > 0) {
        icon_cell = (max_width - gap) / 2;
    }
    if (icon_cell < 8) return;

    bool is_checkers = strncmp(state->match.mode, "checkers", 8) == 0;

    int right_x = geo->offset_x + geo->board_size + gap;
    int list_height = icon_cell * 8 + gap * 7;
    int top_y = geo->offset_y;
    int bottom_y = geo->offset_y + geo->board_size - list_height;

    draw_capture_list(ctx, state, state->black_captures, state->black_captures_len,
        right_x, top_y, icon_cell, gap, is_checkers, false);
    draw_capture_list(ctx, state, state->white_captures, state->white_captures_len,
        right_x, bottom_y, icon_cell, gap, is_checkers, true);
}

static void draw_board_labels(SDL_Renderer *renderer, int offset_x, int offset_y, int cell, int board_size, int window_width, int window_height) {
    int text_scale = cell / 20;
    if (text_scale < 2) {
        text_scale = 2;
    }
    int glyph_w = 5 * text_scale;
    int glyph_h = 7 * text_scale;

    int rank_x = offset_x - glyph_w - 6;
    for (int rank = 0; rank < 8; ++rank) {
        char label[2] = {(char)('8' - rank), '\0'};
        int y = offset_y + rank * cell + (cell - glyph_h) / 2;
        if (rank_x >= 0 && y >= 0 && y + glyph_h <= window_height) {
            draw_text(renderer, rank_x, y, text_scale, kPalette.label_text, label);
        }
    }

    int file_y = offset_y + board_size + 4;
    for (int file = 0; file < 8; ++file) {
        char label[2] = {(char)('a' + file), '\0'};
        int center_x = offset_x + file * cell + cell / 2;
        int x = center_x - glyph_w / 2;
        if (x >= 0 && x + glyph_w <= window_width && file_y + glyph_h <= window_height) {
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
    double fps = state->perf.last_fps;
    double ms = state->perf.last_frame_seconds * 1000.0;

    snprintf(body, sizeof(body),
        "MODE: %s\n"
        "PHASE: %s\n"
        "TURN: %d\n"
        "TICK: %.1f/s (%.2f ms)",
        state->match.mode,
        state->match.phase,
        state->match.turn_number,
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
    int label_margin = 32;
    int capture_margin = 0;
    int cell = 0;
    for (int pass = 0; pass < 3; ++pass) {
        int left_margin = label_margin;
        int right_margin = label_margin + capture_margin;
        int available_width = ctx->surface.window_width - left_margin - right_margin;
        int available_height = ctx->surface.window_height - 2 * label_margin;
        int base_size = (available_width > 0 && available_height > 0) ?
            (available_width < available_height ? available_width : available_height) :
            (ctx->surface.window_width < ctx->surface.window_height ? ctx->surface.window_width : ctx->surface.window_height);
        cell = base_size / 8;
        if (cell < 1) cell = 1;
        int text_scale = cell / 20;
        if (text_scale < 2) text_scale = 2;
        int glyph_h = 7 * text_scale;
        int needed = glyph_h + 10;
        if (needed > label_margin) {
            label_margin = needed;
        }
        int gap = text_scale;
        if (gap < 2) gap = 2;
        capture_margin = (2 * glyph_h) + gap + 6;
    }
    if (cell < 1) {
        cell = 1;
    }
    int board_size = cell * 8; // enforce even cells
    int left_margin = label_margin;
    int right_margin = label_margin + capture_margin;
    int available_width = ctx->surface.window_width - left_margin - right_margin;
    int offset_x = left_margin;
    if (available_width > board_size) {
        offset_x += (available_width - board_size) / 2;
    }
    int offset_y = (ctx->surface.window_height - board_size) / 2;
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

    SDL_GetWindowSize(ctx->surface.window, &ctx->surface.window_width, &ctx->surface.window_height);

    SDL_SetRenderDrawColor(ctx->surface.renderer, kPalette.background.r, kPalette.background.g, kPalette.background.b, kPalette.background.a);
    SDL_RenderClear(ctx->surface.renderer);

    BoardGeometry geo = render_compute_board_geometry(ctx);

    // Start a piece animation when the turn number advances.
    if (snapshot.match.turn_number != ctx->last_turn_number &&
        snapshot.last_move.start.file >= 0 && snapshot.last_move.start.rank >= 0 &&
        snapshot.last_move.end.file >= 0 && snapshot.last_move.end.rank >= 0) {
        ctx->anim.active = true;
        ctx->anim.piece = snapshot.board_state.squares[snapshot.last_move.end.rank][snapshot.last_move.end.file].piece;
        ctx->anim.from_file = snapshot.last_move.start.file;
        ctx->anim.from_rank = snapshot.last_move.start.rank;
        ctx->anim.to_file = snapshot.last_move.end.file;
        ctx->anim.to_rank = snapshot.last_move.end.rank;
        ctx->anim.start_ms = SDL_GetTicks();
        ctx->anim.duration_ms = 180;
        ctx->anim.from_px.x = geo.offset_x + (snapshot.last_move.start.file * geo.cell) + geo.cell / 2.0f;
        ctx->anim.from_px.y = geo.offset_y + (snapshot.last_move.start.rank * geo.cell) + geo.cell / 2.0f;
        ctx->anim.to_px.x = geo.offset_x + (snapshot.last_move.end.file * geo.cell) + geo.cell / 2.0f;
        ctx->anim.to_px.y = geo.offset_y + (snapshot.last_move.end.rank * geo.cell) + geo.cell / 2.0f;
    }
    ctx->last_turn_number = snapshot.match.turn_number;

    Uint32 now_ms = SDL_GetTicks();

    draw_board(ctx, &snapshot, geo.offset_x, geo.offset_y, geo.cell, now_ms);
    draw_board_labels(ctx->surface.renderer, geo.offset_x, geo.offset_y, geo.cell, geo.board_size, ctx->surface.window_width, ctx->surface.window_height);
    draw_captures(ctx, &snapshot, &geo);

    if (snapshot.ui.show_main_menu) {
        ui_draw_main_menu(ctx, &snapshot);
    } else if (snapshot.ui.show_pause_menu) {
        ui_draw_pause_menu(ctx, &snapshot);
    } else if (game_is_terminal_phase(snapshot.match.phase)) {
        ui_draw_win_screen(ctx, &snapshot);
    }
    if (snapshot.ui.show_debug) {
        debug_overlay_render(ctx, &snapshot);
    }

    SDL_RenderPresent(ctx->surface.renderer);
}

void render_shutdown(RenderContext *ctx) {
    size_t tex_cap = sizeof(ctx->assets.piece_textures) / sizeof(ctx->assets.piece_textures[0]);
    for (size_t i = 0; i < tex_cap; ++i) {
        if (ctx->assets.piece_textures[i]) {
            SDL_DestroyTexture(ctx->assets.piece_textures[i]);
            ctx->assets.piece_textures[i] = NULL;
        }
    }
    if (ctx->surface.renderer) {
        SDL_DestroyRenderer(ctx->surface.renderer);
        ctx->surface.renderer = NULL;
    }
    if (ctx->surface.window) {
        SDL_DestroyWindow(ctx->surface.window);
        ctx->surface.window = NULL;
    }
    SDL_Quit();
}
