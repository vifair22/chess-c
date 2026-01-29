#ifndef AI_COMMON_H
#define AI_COMMON_H

#include <stdbool.h>
#include <stdint.h>

// Shared constants
#define AI_TT_SIZE      262144
#define AI_INF          1000000
#define AI_MATE         100000
#define AI_MAX_DEPTH    32

// Transposition table flags
typedef enum {
    TT_EXACT = 0,
    TT_LOWER = 1,
    TT_UPPER = 2
} TtFlag;

// Piece values for evaluation
// Returns centipawn value (100 = 1 pawn)
static inline int piece_value(char piece) {
    switch (piece) {
        case 'P': case 'p': return 100;   // Pawn
        case 'N': case 'n': return 320;   // Knight
        case 'B': case 'b': return 330;   // Bishop
        case 'R': case 'r': return 500;   // Rook
        case 'Q': case 'q': return 900;   // Queen
        case 'K': case 'k': return 20000; // King
        case 'M': case 'm': return 100;   // Checkers man
        case 'W': case 'w': return 300;   // Checkers king
        default: return 0;
    }
}

// Color helpers
static inline bool piece_is_white(char p) {
    return p >= 'A' && p <= 'Z';
}

static inline bool piece_is_black(char p) {
    return p >= 'a' && p <= 'z';
}

static inline int piece_color_idx(char p) {
    return piece_is_white(p) ? 1 : 0;
}

#endif // AI_COMMON_H
