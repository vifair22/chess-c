#include "renderer/ui_button.h"
#include "renderer/draw.h"
#include "renderer/palette.h"

void draw_button_ex(
    RenderContext *ctx,
    SDL_Rect rect,
    const char *label_left,
    const char *label_right,
    ButtonAlign align,
    bool active,
    bool focused,
    int scale,
    int mouse_x,
    int mouse_y,
    bool allow_hover
) {
    bool hovered = allow_hover && point_in_rect(mouse_x, mouse_y, rect);

    // Base fill
    SDL_Color fill = kPalette.panel_fill;
    fill.r = (Uint8)(fill.r + 10);
    fill.g = (Uint8)(fill.g + 10);
    fill.b = (Uint8)(fill.b + 12);
    SDL_SetRenderDrawColor(ctx->surface.renderer, fill.r, fill.g, fill.b, fill.a);
    SDL_RenderFillRect(ctx->surface.renderer, &rect);

    // Active state overlay
    if (active) {
        SDL_Color sel = kPalette.select_fill;
        sel.a = 180;
        SDL_SetRenderDrawColor(ctx->surface.renderer, sel.r, sel.g, sel.b, sel.a);
        SDL_RenderFillRect(ctx->surface.renderer, &rect);
    }

    // Hover overlay
    if (hovered) {
        SDL_Color hover = kPalette.hover_fill;
        hover.a = 140;
        SDL_SetRenderDrawColor(ctx->surface.renderer, hover.r, hover.g, hover.b, hover.a);
        SDL_RenderFillRect(ctx->surface.renderer, &rect);
    }

    // Focus overlay (when not hovered)
    if (focused && !hovered) {
        SDL_Color focus = kPalette.hover_fill;
        focus.a = 140;
        SDL_SetRenderDrawColor(ctx->surface.renderer, focus.r, focus.g, focus.b, focus.a);
        SDL_RenderFillRect(ctx->surface.renderer, &rect);
    }

    // Border
    SDL_SetRenderDrawColor(ctx->surface.renderer,
        kPalette.panel_border.r, kPalette.panel_border.g,
        kPalette.panel_border.b, kPalette.panel_border.a);
    SDL_RenderDrawRect(ctx->surface.renderer, &rect);

    // Inner highlight for hover/focus
    if ((hovered || focused) && rect.w > 6 && rect.h > 6) {
        SDL_Color edge = kPalette.panel_border;
        edge.r = (Uint8)((edge.r + 40) > 255 ? 255 : (edge.r + 40));
        edge.g = (Uint8)((edge.g + 40) > 255 ? 255 : (edge.g + 40));
        edge.b = (Uint8)((edge.b + 40) > 255 ? 255 : (edge.b + 40));
        SDL_Rect inner = {rect.x + 2, rect.y + 2, rect.w - 4, rect.h - 4};
        SDL_SetRenderDrawColor(ctx->surface.renderer, edge.r, edge.g, edge.b, edge.a);
        SDL_RenderDrawRect(ctx->surface.renderer, &inner);
    }

    // Draw text based on alignment
    int text_y = rect.y + (rect.h - (7 * scale)) / 2;
    int pad = 6;

    if (align == BUTTON_ALIGN_CENTER) {
        if (label_left && label_left[0]) {
            int text_w = measure_text_width(label_left, scale);
            int text_x = rect.x + (rect.w - text_w) / 2;
            draw_text(ctx->surface.renderer, text_x, text_y, scale, kPalette.panel_text, label_left);
        }
    } else {
        // Left/right alignment
        if (label_left && label_left[0]) {
            draw_text(ctx->surface.renderer, rect.x + pad, text_y, scale, kPalette.panel_text, label_left);
        }
        if (label_right && label_right[0]) {
            int right_w = measure_text_width(label_right, scale);
            int right_x = rect.x + rect.w - right_w - pad;
            if (right_x < rect.x + pad) right_x = rect.x + pad;
            draw_text(ctx->surface.renderer, right_x, text_y, scale, kPalette.panel_text, label_right);
        }
    }
}

MenuLayout draw_panel_base(RenderContext *ctx, const char *title, bool opaque_backdrop) {
    MenuLayout layout = menu_layout(ctx->surface.window_width, ctx->surface.window_height);
    SDL_Rect panel = layout.panel;

    SDL_Color backdrop = kPalette.panel_backdrop;
    if (opaque_backdrop) {
        backdrop.a = 255;
    }
    SDL_SetRenderDrawColor(ctx->surface.renderer, backdrop.r, backdrop.g, backdrop.b, backdrop.a);
    SDL_RenderFillRect(ctx->surface.renderer, &(SDL_Rect){0, 0, ctx->surface.window_width, ctx->surface.window_height});

    SDL_SetRenderDrawColor(ctx->surface.renderer,
        kPalette.panel_fill.r, kPalette.panel_fill.g,
        kPalette.panel_fill.b, kPalette.panel_fill.a);
    SDL_RenderFillRect(ctx->surface.renderer, &panel);
    SDL_SetRenderDrawColor(ctx->surface.renderer,
        kPalette.panel_border.r, kPalette.panel_border.g,
        kPalette.panel_border.b, kPalette.panel_border.a);
    SDL_RenderDrawRect(ctx->surface.renderer, &panel);

    int title_x = panel.x + 24;
    int title_y = panel.y + 24;
    draw_text(ctx->surface.renderer, title_x, title_y, layout.scale + 1, kPalette.panel_text, title);
    return layout;
}

void draw_panel(RenderContext *ctx, const char *title, const char *body) {
    MenuLayout layout = draw_panel_base(ctx, title, false);
    draw_text(ctx->surface.renderer, layout.body_x, layout.body_y, layout.scale, kPalette.panel_text, body);
}

void draw_panel_text(RenderContext *ctx, const MenuLayout *layout, const char *text, int y) {
    draw_text(ctx->surface.renderer, layout->body_x, y, layout->scale, kPalette.panel_text, text);
}

void draw_main_menu_art(RenderContext *ctx, int window_w, int window_h) {
    int scale = window_w / 180;
    if (scale < 4) scale = 4;
    scale = (scale + 1) / 2;
    if (scale < 2) scale = 2;
    int text_w = measure_text_width("CHESS-C", scale);
    int x = (window_w - text_w) / 2;
    int y = window_h / 8;

    SDL_Color glow = kPalette.panel_border;
    glow.a = 200;
    for (int i = 3; i > 0; --i) {
        draw_text(ctx->surface.renderer, x - i, y - i, scale, glow, "CHESS-C");
        draw_text(ctx->surface.renderer, x + i, y + i, scale, glow, "CHESS-C");
    }
    draw_text(ctx->surface.renderer, x, y, scale, kPalette.panel_text, "CHESS-C");
}
