#ifndef UI_BUTTON_H
#define UI_BUTTON_H

#include <SDL.h>
#include <stdbool.h>
#include "renderer/render.h"
#include "renderer/ui_layout.h"

// Button text alignment
typedef enum {
    BUTTON_ALIGN_CENTER,    // Single centered label
    BUTTON_ALIGN_LEFT_RIGHT // Left and right labels
} ButtonAlign;

// Unified button drawing function
// For centered text: use label_left as the label, label_right = NULL
// For left/right text: use both label_left and label_right
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
);

// Convenience wrapper for centered button
static inline void draw_button(
    RenderContext *ctx,
    SDL_Rect rect,
    const char *label,
    bool active,
    bool focused,
    int scale,
    int mouse_x,
    int mouse_y,
    bool allow_hover
) {
    draw_button_ex(ctx, rect, label, NULL, BUTTON_ALIGN_CENTER, active, focused, scale, mouse_x, mouse_y, allow_hover);
}

// Convenience wrapper for left/right button
static inline void draw_button_lr(
    RenderContext *ctx,
    SDL_Rect rect,
    const char *left,
    const char *right,
    bool active,
    bool focused,
    int scale,
    int mouse_x,
    int mouse_y,
    bool allow_hover
) {
    draw_button_ex(ctx, rect, left, right, BUTTON_ALIGN_LEFT_RIGHT, active, focused, scale, mouse_x, mouse_y, allow_hover);
}

// Panel drawing functions
MenuLayout draw_panel_base(RenderContext *ctx, const char *title, bool opaque_backdrop);
void draw_panel(RenderContext *ctx, const char *title, const char *body);
void draw_panel_text(RenderContext *ctx, const MenuLayout *layout, const char *text, int y);
void draw_main_menu_art(RenderContext *ctx, int window_w, int window_h);

#endif // UI_BUTTON_H
