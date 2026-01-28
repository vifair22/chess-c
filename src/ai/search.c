#include "ai/search.h"

#include <stdint.h>
#include <string.h>

#define SEARCH_MAX_PIECES 16
#define SEARCH_TT_SIZE 262144
#define SEARCH_INF 1000000
#define SEARCH_MATE 100000
#define SEARCH_MAX_DEPTH 32

enum {
    SEARCH_BLACK = 0,
    SEARCH_WHITE = 1
};

typedef struct {
    char board[64];
    int pieces[2][SEARCH_MAX_PIECES];
    int piece_count[2];
    int piece_index[64];
    int king_sq[2];
    bool white_to_move;
    uint8_t castle_rights; // 1=WK,2=WQ,4=BK,8=BQ
    int ep_sq; // -1 when none
    char promo_white;
    char promo_black;
    uint64_t key;
} SearchState;

typedef struct {
    char moved_piece;
    char captured_piece;
    int captured_sq;
    int rook_from;
    int rook_to;
    char rook_piece;
    uint8_t prev_castle;
    int prev_ep;
    int prev_king_white;
    int prev_king_black;
    bool prev_white_to_move;
    uint64_t prev_key;
} SearchUndo;

typedef enum {
    TT_EXACT = 0,
    TT_LOWER = 1,
    TT_UPPER = 2
} SearchTtFlag;

typedef struct {
    uint64_t key;
    int depth;
    int score;
    SearchTtFlag flag;
    SearchMove best;
    bool valid;
} SearchTtEntry;

static SearchTtEntry g_tt[SEARCH_TT_SIZE];
static int g_history[2][64][64];
static SearchMove g_killers[SEARCH_MAX_DEPTH][2];

static uint64_t g_zobrist_piece[12][64];
static uint64_t g_zobrist_castle[16];
static uint64_t g_zobrist_ep[9];
static uint64_t g_zobrist_side;
static bool g_zobrist_ready = false;

static uint64_t splitmix64(uint64_t *x) {
    uint64_t z = (*x += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

static void init_zobrist(void) {
    if (g_zobrist_ready) return;
    uint64_t seed = 0x243f6a8885a308d3ULL;
    for (int p = 0; p < 12; ++p) {
        for (int sq = 0; sq < 64; ++sq) {
            g_zobrist_piece[p][sq] = splitmix64(&seed);
        }
    }
    for (int i = 0; i < 16; ++i) {
        g_zobrist_castle[i] = splitmix64(&seed);
    }
    for (int i = 0; i < 9; ++i) {
        g_zobrist_ep[i] = splitmix64(&seed);
    }
    g_zobrist_side = splitmix64(&seed);
    g_zobrist_ready = true;
}

static inline int sq_of(int file, int rank) {
    return (rank << 3) | file;
}

static inline int file_of(int sq) {
    return sq & 7;
}

static inline int rank_of(int sq) {
    return sq >> 3;
}

static inline int piece_color(char p) {
    return (p >= 'A' && p <= 'Z') ? SEARCH_WHITE : SEARCH_BLACK;
}

static inline bool piece_is_white(char p) {
    return p >= 'A' && p <= 'Z';
}

static int piece_index(char p) {
    switch (p) {
        case 'P': return 0;
        case 'N': return 1;
        case 'B': return 2;
        case 'R': return 3;
        case 'Q': return 4;
        case 'K': return 5;
        case 'p': return 6;
        case 'n': return 7;
        case 'b': return 8;
        case 'r': return 9;
        case 'q': return 10;
        case 'k': return 11;
        default: return -1;
    }
}

static int piece_value(char piece) {
    switch (piece) {
        case 'P':
        case 'p':
            return 100;
        case 'N':
        case 'n':
            return 320;
        case 'B':
        case 'b':
            return 330;
        case 'R':
        case 'r':
            return 500;
        case 'Q':
        case 'q':
            return 900;
        default:
            return 0;
    }
}

static bool move_equal(const SearchMove *a, const SearchMove *b) {
    return a->from.file == b->from.file && a->from.rank == b->from.rank &&
           a->to.file == b->to.file && a->to.rank == b->to.rank;
}

static void list_add(SearchState *s, int color, int sq) {
    int idx = s->piece_count[color]++;
    s->pieces[color][idx] = sq;
    s->piece_index[sq] = idx;
}

static void list_remove(SearchState *s, int color, int sq) {
    int idx = s->piece_index[sq];
    int last = --s->piece_count[color];
    if (idx < 0 || idx > last) return;
    int last_sq = s->pieces[color][last];
    s->pieces[color][idx] = last_sq;
    s->piece_index[last_sq] = idx;
    s->piece_index[sq] = -1;
}

static void list_move(SearchState *s, int color, int from, int to) {
    int idx = s->piece_index[from];
    s->pieces[color][idx] = to;
    s->piece_index[to] = idx;
    s->piece_index[from] = -1;
}

static uint64_t zobrist_piece_key(char piece, int sq) {
    int idx = piece_index(piece);
    return idx >= 0 ? g_zobrist_piece[idx][sq] : 0;
}

static uint64_t zobrist_ep_key(int ep_sq) {
    if (ep_sq < 0) return g_zobrist_ep[8];
    return g_zobrist_ep[file_of(ep_sq)];
}

static void search_state_init(SearchState *dst, const GameState *src) {
    memset(dst, 0, sizeof(*dst));
    for (int i = 0; i < 64; ++i) {
        dst->piece_index[i] = -1;
        dst->board[i] = '.';
    }
    dst->king_sq[SEARCH_WHITE] = -1;
    dst->king_sq[SEARCH_BLACK] = -1;
    dst->promo_white = src->variant.promotion_choice_white ? src->variant.promotion_choice_white : 'Q';
    dst->promo_black = src->variant.promotion_choice_black ? src->variant.promotion_choice_black : 'q';
    dst->white_to_move = src->match.white_to_move;
    dst->castle_rights = 0;
    if (src->chess.rights.white_can_castle_kingside) dst->castle_rights |= 1;
    if (src->chess.rights.white_can_castle_queenside) dst->castle_rights |= 2;
    if (src->chess.rights.black_can_castle_kingside) dst->castle_rights |= 4;
    if (src->chess.rights.black_can_castle_queenside) dst->castle_rights |= 8;
    if (src->chess.rights.en_passant_target.file >= 0 && src->chess.rights.en_passant_target.rank >= 0) {
        dst->ep_sq = sq_of(src->chess.rights.en_passant_target.file, src->chess.rights.en_passant_target.rank);
    } else {
        dst->ep_sq = -1;
    }

    for (int r = 0; r < 8; ++r) {
        for (int f = 0; f < 8; ++f) {
            char p = src->board_state.squares[r][f].piece;
            int sq = sq_of(f, r);
            dst->board[sq] = p;
            if (p != '.') {
                int color = piece_color(p);
                list_add(dst, color, sq);
                if (p == 'K') dst->king_sq[SEARCH_WHITE] = sq;
                if (p == 'k') dst->king_sq[SEARCH_BLACK] = sq;
            }
        }
    }

    dst->key = 0;
    for (int sq = 0; sq < 64; ++sq) {
        char p = dst->board[sq];
        if (p != '.') {
            dst->key ^= zobrist_piece_key(p, sq);
        }
    }
    dst->key ^= g_zobrist_castle[dst->castle_rights & 0xF];
    dst->key ^= zobrist_ep_key(dst->ep_sq);
    if (dst->white_to_move) {
        dst->key ^= g_zobrist_side;
    }
}

static bool in_bounds(int file, int rank) {
    return file >= 0 && file < 8 && rank >= 0 && rank < 8;
}

static bool is_square_attacked(const SearchState *s, int sq, bool by_white) {
    int file = file_of(sq);
    int rank = rank_of(sq);
    int pawn_dir = by_white ? 1 : -1;
    int pawn_rank = rank + pawn_dir;
    if (in_bounds(file - 1, pawn_rank)) {
        char p = s->board[sq_of(file - 1, pawn_rank)];
        if (by_white ? (p == 'P') : (p == 'p')) return true;
    }
    if (in_bounds(file + 1, pawn_rank)) {
        char p = s->board[sq_of(file + 1, pawn_rank)];
        if (by_white ? (p == 'P') : (p == 'p')) return true;
    }

    static const int knight_offsets[8][2] = {
        {1, 2}, {2, 1}, {-1, 2}, {-2, 1},
        {1, -2}, {2, -1}, {-1, -2}, {-2, -1},
    };
    for (int i = 0; i < 8; ++i) {
        int f = file + knight_offsets[i][0];
        int r = rank + knight_offsets[i][1];
        if (!in_bounds(f, r)) continue;
        char p = s->board[sq_of(f, r)];
        if (by_white ? (p == 'N') : (p == 'n')) return true;
    }

    static const int diag_dirs[4][2] = {{1,1},{1,-1},{-1,1},{-1,-1}};
    for (int d = 0; d < 4; ++d) {
        int f = file + diag_dirs[d][0];
        int r = rank + diag_dirs[d][1];
        while (in_bounds(f, r)) {
            char p = s->board[sq_of(f, r)];
            if (p != '.') {
                if (by_white ? (p == 'B' || p == 'Q') : (p == 'b' || p == 'q')) return true;
                break;
            }
            f += diag_dirs[d][0];
            r += diag_dirs[d][1];
        }
    }

    static const int ortho_dirs[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
    for (int d = 0; d < 4; ++d) {
        int f = file + ortho_dirs[d][0];
        int r = rank + ortho_dirs[d][1];
        while (in_bounds(f, r)) {
            char p = s->board[sq_of(f, r)];
            if (p != '.') {
                if (by_white ? (p == 'R' || p == 'Q') : (p == 'r' || p == 'q')) return true;
                break;
            }
            f += ortho_dirs[d][0];
            r += ortho_dirs[d][1];
        }
    }

    for (int df = -1; df <= 1; ++df) {
        for (int dr = -1; dr <= 1; ++dr) {
            if (df == 0 && dr == 0) continue;
            int f = file + df;
            int r = rank + dr;
            if (!in_bounds(f, r)) continue;
            char p = s->board[sq_of(f, r)];
            if (by_white ? (p == 'K') : (p == 'k')) return true;
        }
    }
    return false;
}

static void update_castle_rights(SearchState *s, char moved, int from, int to, char captured) {
    if (moved == 'K') {
        s->castle_rights &= ~(1 | 2);
    } else if (moved == 'k') {
        s->castle_rights &= ~(4 | 8);
    } else if (moved == 'R') {
        if (from == sq_of(0, 7)) s->castle_rights &= ~2;
        else if (from == sq_of(7, 7)) s->castle_rights &= ~1;
    } else if (moved == 'r') {
        if (from == sq_of(0, 0)) s->castle_rights &= ~8;
        else if (from == sq_of(7, 0)) s->castle_rights &= ~4;
    }
    if (captured == 'R') {
        if (to == sq_of(0, 7)) s->castle_rights &= ~2;
        else if (to == sq_of(7, 7)) s->castle_rights &= ~1;
    } else if (captured == 'r') {
        if (to == sq_of(0, 0)) s->castle_rights &= ~8;
        else if (to == sq_of(7, 0)) s->castle_rights &= ~4;
    }
}

static void search_make_move(SearchState *s, const SearchMove *m, SearchUndo *u) {
    int from = sq_of(m->from.file, m->from.rank);
    int to = sq_of(m->to.file, m->to.rank);
    char moved = s->board[from];
    int color = piece_is_white(moved) ? SEARCH_WHITE : SEARCH_BLACK;
    int opp = color ^ 1;

    u->moved_piece = moved;
    u->captured_piece = '.';
    u->captured_sq = -1;
    u->rook_from = -1;
    u->rook_to = -1;
    u->rook_piece = '.';
    u->prev_castle = s->castle_rights;
    u->prev_ep = s->ep_sq;
    u->prev_king_white = s->king_sq[SEARCH_WHITE];
    u->prev_king_black = s->king_sq[SEARCH_BLACK];
    u->prev_white_to_move = s->white_to_move;
    u->prev_key = s->key;

    s->key ^= zobrist_ep_key(s->ep_sq);
    s->ep_sq = -1;

    if (m->hint.flags & MOVE_EN_PASSANT) {
        int cap_sq = to + (color == SEARCH_WHITE ? 8 : -8);
        u->captured_sq = cap_sq;
        u->captured_piece = s->board[cap_sq];
        if (u->captured_piece != '.') {
            s->board[cap_sq] = '.';
            list_remove(s, opp, cap_sq);
            s->key ^= zobrist_piece_key(u->captured_piece, cap_sq);
        }
    } else {
        u->captured_piece = s->board[to];
        if (u->captured_piece != '.') {
            u->captured_sq = to;
            s->board[to] = '.';
            list_remove(s, opp, to);
            s->key ^= zobrist_piece_key(u->captured_piece, to);
        }
    }

    s->key ^= zobrist_piece_key(moved, from);
    s->board[from] = '.';
    list_move(s, color, from, to);

    char placed = moved;
    if ((m->hint.flags & MOVE_PROMOTION) && m->hint.promotion) {
        placed = m->hint.promotion;
    }
    s->board[to] = placed;
    s->key ^= zobrist_piece_key(placed, to);

    if (moved == 'K') s->king_sq[SEARCH_WHITE] = to;
    if (moved == 'k') s->king_sq[SEARCH_BLACK] = to;

    if (m->hint.flags & MOVE_CASTLE_KING) {
        int rook_from = color == SEARCH_WHITE ? sq_of(7, 7) : sq_of(7, 0);
        int rook_to = color == SEARCH_WHITE ? sq_of(5, 7) : sq_of(5, 0);
        char rook = s->board[rook_from];
        u->rook_from = rook_from;
        u->rook_to = rook_to;
        u->rook_piece = rook;
        s->board[rook_from] = '.';
        s->board[rook_to] = rook;
        list_move(s, color, rook_from, rook_to);
        s->key ^= zobrist_piece_key(rook, rook_from);
        s->key ^= zobrist_piece_key(rook, rook_to);
    } else if (m->hint.flags & MOVE_CASTLE_QUEEN) {
        int rook_from = color == SEARCH_WHITE ? sq_of(0, 7) : sq_of(0, 0);
        int rook_to = color == SEARCH_WHITE ? sq_of(3, 7) : sq_of(3, 0);
        char rook = s->board[rook_from];
        u->rook_from = rook_from;
        u->rook_to = rook_to;
        u->rook_piece = rook;
        s->board[rook_from] = '.';
        s->board[rook_to] = rook;
        list_move(s, color, rook_from, rook_to);
        s->key ^= zobrist_piece_key(rook, rook_from);
        s->key ^= zobrist_piece_key(rook, rook_to);
    }

    uint8_t prev_castle = s->castle_rights;
    update_castle_rights(s, moved, from, to, u->captured_piece);
    if (prev_castle != s->castle_rights) {
        s->key ^= g_zobrist_castle[prev_castle & 0xF];
        s->key ^= g_zobrist_castle[s->castle_rights & 0xF];
    }

    if (moved == 'P' && rank_of(from) == 6 && rank_of(to) == 4) {
        s->ep_sq = sq_of(file_of(from), 5);
    } else if (moved == 'p' && rank_of(from) == 1 && rank_of(to) == 3) {
        s->ep_sq = sq_of(file_of(from), 2);
    }
    s->key ^= zobrist_ep_key(s->ep_sq);

    s->white_to_move = !s->white_to_move;
    s->key ^= g_zobrist_side;
}

static void search_unmake_move(SearchState *s, const SearchMove *m, const SearchUndo *u) {
    int from = sq_of(m->from.file, m->from.rank);
    int to = sq_of(m->to.file, m->to.rank);
    char moved = u->moved_piece;
    int color = piece_is_white(moved) ? SEARCH_WHITE : SEARCH_BLACK;
    int opp = color ^ 1;

    s->white_to_move = u->prev_white_to_move;
    s->castle_rights = u->prev_castle;
    s->ep_sq = u->prev_ep;
    s->king_sq[SEARCH_WHITE] = u->prev_king_white;
    s->king_sq[SEARCH_BLACK] = u->prev_king_black;
    s->key = u->prev_key;

    if (u->rook_from >= 0) {
        s->board[u->rook_from] = u->rook_piece;
        s->board[u->rook_to] = '.';
        list_move(s, color, u->rook_to, u->rook_from);
    }

    s->board[from] = moved;
    s->board[to] = '.';
    list_move(s, color, to, from);

    if (u->captured_piece != '.') {
        int cap_sq = u->captured_sq;
        s->board[cap_sq] = u->captured_piece;
        list_add(s, opp, cap_sq);
    }
}

static void search_make_null(SearchState *s, SearchUndo *u) {
    u->prev_white_to_move = s->white_to_move;
    u->prev_ep = s->ep_sq;
    u->prev_key = s->key;
    s->key ^= zobrist_ep_key(s->ep_sq);
    s->ep_sq = -1;
    s->key ^= zobrist_ep_key(s->ep_sq);
    s->white_to_move = !s->white_to_move;
    s->key ^= g_zobrist_side;
}

static void search_unmake_null(SearchState *s, const SearchUndo *u) {
    s->white_to_move = u->prev_white_to_move;
    s->ep_sq = u->prev_ep;
    s->key = u->prev_key;
}

static bool is_capture_move(const SearchState *s, const SearchMove *m) {
    if (m->hint.flags & MOVE_EN_PASSANT) return true;
    int to = sq_of(m->to.file, m->to.rank);
    return s->board[to] != '.';
}

static int capture_score(const SearchState *s, const SearchMove *m) {
    int from = sq_of(m->from.file, m->from.rank);
    int to = sq_of(m->to.file, m->to.rank);
    char attacker = s->board[from];
    char victim = s->board[to];
    if (m->hint.flags & MOVE_EN_PASSANT) {
        victim = s->white_to_move ? 'p' : 'P';
    }
    return (piece_value(victim) * 10) - piece_value(attacker);
}

static void add_legal_move(SearchState *s, SearchMove *out, int *count, int cap, int color, const SearchMove *m, bool captures_only) {
    if (*count >= cap) return;
    if (captures_only && !is_capture_move(s, m)) return;

    SearchUndo u;
    search_make_move(s, m, &u);
    int king_sq = s->king_sq[color];
    bool in_check = is_square_attacked(s, king_sq, color == SEARCH_WHITE ? false : true);
    if (!in_check) {
        out[(*count)++] = *m;
    }
    search_unmake_move(s, m, &u);
}

static void gen_pawn_moves(SearchState *s, SearchMove *out, int *count, int cap, int color, bool captures_only) {
    int dir = (color == SEARCH_WHITE) ? -1 : 1;
    int start_rank = (color == SEARCH_WHITE) ? 6 : 1;
    int promo_rank = (color == SEARCH_WHITE) ? 0 : 7;
    char promo = (color == SEARCH_WHITE) ? s->promo_white : s->promo_black;

    for (int i = 0; i < s->piece_count[color]; ++i) {
        int sq = s->pieces[color][i];
        char p = s->board[sq];
        if (p != 'P' && p != 'p') continue;
        int file = file_of(sq);
        int rank = rank_of(sq);
        int one_rank = rank + dir;
        if (one_rank >= 0 && one_rank < 8) {
            int one_sq = sq_of(file, one_rank);
            if (!captures_only && s->board[one_sq] == '.') {
                SearchMove m = { .from = {file, rank}, .to = {file, one_rank}, .hint = {0} };
                if (one_rank == promo_rank) {
                    m.hint.flags |= MOVE_PROMOTION;
                    m.hint.promotion = promo;
                }
                add_legal_move(s, out, count, cap, color, &m, captures_only);
                int two_rank = rank + 2 * dir;
                if (rank == start_rank && s->board[sq_of(file, two_rank)] == '.') {
                    SearchMove m2 = { .from = {file, rank}, .to = {file, two_rank}, .hint = {0} };
                    add_legal_move(s, out, count, cap, color, &m2, captures_only);
                }
            }
        }
        int cap_rank = rank + dir;
        for (int df = -1; df <= 1; df += 2) {
            int tf = file + df;
            if (!in_bounds(tf, cap_rank)) continue;
            int tsq = sq_of(tf, cap_rank);
            char target = s->board[tsq];
            if (target != '.' && piece_color(target) != color) {
                SearchMove m = { .from = {file, rank}, .to = {tf, cap_rank}, .hint = {0} };
                if (cap_rank == promo_rank) {
                    m.hint.flags |= MOVE_PROMOTION;
                    m.hint.promotion = promo;
                }
                add_legal_move(s, out, count, cap, color, &m, captures_only);
            }
        }
        if (s->ep_sq >= 0) {
            int ep_file = file_of(s->ep_sq);
            int ep_rank = rank_of(s->ep_sq);
            if (ep_rank == cap_rank && (ep_file == file - 1 || ep_file == file + 1)) {
                SearchMove m = { .from = {file, rank}, .to = {ep_file, ep_rank}, .hint = {.flags = MOVE_EN_PASSANT} };
                add_legal_move(s, out, count, cap, color, &m, captures_only);
            }
        }
    }
}

static void gen_knight_moves(SearchState *s, SearchMove *out, int *count, int cap, int color, bool captures_only) {
    static const int offsets[8][2] = {
        {1, 2}, {2, 1}, {-1, 2}, {-2, 1},
        {1, -2}, {2, -1}, {-1, -2}, {-2, -1},
    };
    for (int i = 0; i < s->piece_count[color]; ++i) {
        int sq = s->pieces[color][i];
        char p = s->board[sq];
        if (p != 'N' && p != 'n') continue;
        int file = file_of(sq);
        int rank = rank_of(sq);
        for (int j = 0; j < 8; ++j) {
            int tf = file + offsets[j][0];
            int tr = rank + offsets[j][1];
            if (!in_bounds(tf, tr)) continue;
            int tsq = sq_of(tf, tr);
            char target = s->board[tsq];
            if (target != '.' && piece_color(target) == color) continue;
            SearchMove m = { .from = {file, rank}, .to = {tf, tr}, .hint = {0} };
            add_legal_move(s, out, count, cap, color, &m, captures_only);
        }
    }
}

static void gen_sliding_moves(SearchState *s, SearchMove *out, int *count, int cap, int color, bool captures_only, char piece, const int dirs[][2], int dir_count) {
    for (int i = 0; i < s->piece_count[color]; ++i) {
        int sq = s->pieces[color][i];
        char p = s->board[sq];
        if (p != piece) continue;
        int file = file_of(sq);
        int rank = rank_of(sq);
        for (int d = 0; d < dir_count; ++d) {
            int df = dirs[d][0];
            int dr = dirs[d][1];
            int tf = file + df;
            int tr = rank + dr;
            while (in_bounds(tf, tr)) {
                int tsq = sq_of(tf, tr);
                char target = s->board[tsq];
                if (target != '.') {
                    if (piece_color(target) != color) {
                        SearchMove m = { .from = {file, rank}, .to = {tf, tr}, .hint = {0} };
                        add_legal_move(s, out, count, cap, color, &m, captures_only);
                    }
                    break;
                }
                if (!captures_only) {
                    SearchMove m = { .from = {file, rank}, .to = {tf, tr}, .hint = {0} };
                    add_legal_move(s, out, count, cap, color, &m, captures_only);
                }
                tf += df;
                tr += dr;
            }
        }
    }
}

static void gen_king_moves(SearchState *s, SearchMove *out, int *count, int cap, int color, bool captures_only) {
    for (int i = 0; i < s->piece_count[color]; ++i) {
        int sq = s->pieces[color][i];
        char p = s->board[sq];
        if (p != 'K' && p != 'k') continue;
        int file = file_of(sq);
        int rank = rank_of(sq);
        for (int df = -1; df <= 1; ++df) {
            for (int dr = -1; dr <= 1; ++dr) {
                if (df == 0 && dr == 0) continue;
                int tf = file + df;
                int tr = rank + dr;
                if (!in_bounds(tf, tr)) continue;
                int tsq = sq_of(tf, tr);
                char target = s->board[tsq];
                if (target != '.' && piece_color(target) == color) continue;
                SearchMove m = { .from = {file, rank}, .to = {tf, tr}, .hint = {0} };
                add_legal_move(s, out, count, cap, color, &m, captures_only);
            }
        }

        if (captures_only) {
            continue;
        }

        bool in_check = is_square_attacked(s, sq, color == SEARCH_WHITE ? false : true);
        if (in_check) continue;
        int back_rank = (color == SEARCH_WHITE) ? 7 : 0;
        if ((color == SEARCH_WHITE && (s->castle_rights & 1)) ||
            (color == SEARCH_BLACK && (s->castle_rights & 4))) {
            if (s->board[sq_of(5, back_rank)] == '.' && s->board[sq_of(6, back_rank)] == '.') {
                if (!is_square_attacked(s, sq_of(5, back_rank), color == SEARCH_WHITE ? false : true) &&
                    !is_square_attacked(s, sq_of(6, back_rank), color == SEARCH_WHITE ? false : true)) {
                    SearchMove m = { .from = {file, rank}, .to = {6, back_rank}, .hint = {.flags = MOVE_CASTLE_KING} };
                    add_legal_move(s, out, count, cap, color, &m, false);
                }
            }
        }
        if ((color == SEARCH_WHITE && (s->castle_rights & 2)) ||
            (color == SEARCH_BLACK && (s->castle_rights & 8))) {
            if (s->board[sq_of(1, back_rank)] == '.' &&
                s->board[sq_of(2, back_rank)] == '.' &&
                s->board[sq_of(3, back_rank)] == '.') {
                if (!is_square_attacked(s, sq_of(2, back_rank), color == SEARCH_WHITE ? false : true) &&
                    !is_square_attacked(s, sq_of(3, back_rank), color == SEARCH_WHITE ? false : true)) {
                    SearchMove m = { .from = {file, rank}, .to = {2, back_rank}, .hint = {.flags = MOVE_CASTLE_QUEEN} };
                    add_legal_move(s, out, count, cap, color, &m, false);
                }
            }
        }
    }
}

static int generate_legal_moves(SearchState *s, SearchMove *moves, int cap, bool captures_only) {
    int count = 0;
    int color = s->white_to_move ? SEARCH_WHITE : SEARCH_BLACK;

    gen_pawn_moves(s, moves, &count, cap, color, captures_only);
    gen_knight_moves(s, moves, &count, cap, color, captures_only);
    {
        static const int dirs[4][2] = {{1,1},{1,-1},{-1,1},{-1,-1}};
        gen_sliding_moves(s, moves, &count, cap, color, captures_only, color == SEARCH_WHITE ? 'B' : 'b', dirs, 4);
    }
    {
        static const int dirs[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
        gen_sliding_moves(s, moves, &count, cap, color, captures_only, color == SEARCH_WHITE ? 'R' : 'r', dirs, 4);
    }
    {
        static const int dirs[8][2] = {{1,1},{1,-1},{-1,1},{-1,-1},{1,0},{-1,0},{0,1},{0,-1}};
        gen_sliding_moves(s, moves, &count, cap, color, captures_only, color == SEARCH_WHITE ? 'Q' : 'q', dirs, 8);
    }
    gen_king_moves(s, moves, &count, cap, color, captures_only);

    return count;
}

static int evaluate(const SearchState *s) {
    int score = 0;
    for (int i = 0; i < s->piece_count[SEARCH_WHITE]; ++i) {
        char p = s->board[s->pieces[SEARCH_WHITE][i]];
        score += piece_value(p);
    }
    for (int i = 0; i < s->piece_count[SEARCH_BLACK]; ++i) {
        char p = s->board[s->pieces[SEARCH_BLACK][i]];
        score -= piece_value(p);
    }
    return s->white_to_move ? score : -score;
}

static int move_score(const SearchState *s, const SearchMove *m, const SearchMove *tt_best, int ply) {
    int score = 0;
    if (tt_best && move_equal(m, tt_best)) {
        score += 10000;
    }
    if (is_capture_move(s, m)) {
        score += 20000 + capture_score(s, m);
    } else {
        int color = s->white_to_move ? SEARCH_WHITE : SEARCH_BLACK;
        int from = sq_of(m->from.file, m->from.rank);
        int to = sq_of(m->to.file, m->to.rank);
        score += g_history[color][from][to];
        if (ply >= 0 && ply < SEARCH_MAX_DEPTH) {
            if (move_equal(m, &g_killers[ply][0])) score += 9000;
            else if (move_equal(m, &g_killers[ply][1])) score += 8000;
        }
    }
    if (m->hint.flags & MOVE_PROMOTION) {
        score += 500;
    }
    return score;
}

static void order_moves(const SearchState *s, SearchMove *moves, int count, const SearchMove *tt_best, int ply) {
    for (int i = 1; i < count; ++i) {
        SearchMove cur = moves[i];
        int cur_score = move_score(s, &cur, tt_best, ply);
        int j = i - 1;
        while (j >= 0) {
            int prev_score = move_score(s, &moves[j], tt_best, ply);
            if (prev_score >= cur_score) break;
            moves[j + 1] = moves[j];
            --j;
        }
        moves[j + 1] = cur;
    }
}

static bool tt_probe(uint64_t key, int depth, int alpha, int beta, int *out_score, SearchMove *out_best) {
    if (!out_score || !out_best) return false;
    SearchTtEntry *entry = &g_tt[key & (SEARCH_TT_SIZE - 1)];
    if (!entry->valid || entry->key != key) return false;
    *out_best = entry->best;
    if (entry->depth < depth) return false;
    if (entry->flag == TT_EXACT) {
        *out_score = entry->score;
        return true;
    }
    if (entry->flag == TT_LOWER && entry->score >= beta) {
        *out_score = entry->score;
        return true;
    }
    if (entry->flag == TT_UPPER && entry->score <= alpha) {
        *out_score = entry->score;
        return true;
    }
    return false;
}

static void tt_store(uint64_t key, int depth, int score, SearchTtFlag flag, const SearchMove *best) {
    SearchTtEntry *entry = &g_tt[key & (SEARCH_TT_SIZE - 1)];
    if (entry->valid && entry->key == key && entry->depth > depth) {
        return;
    }
    entry->key = key;
    entry->depth = depth;
    entry->score = score;
    entry->flag = flag;
    entry->best = best ? *best : (SearchMove){0};
    entry->valid = true;
}

static int terminal_score(const SearchState *s, int depth) {
    int color = s->white_to_move ? SEARCH_WHITE : SEARCH_BLACK;
    int king_sq = s->king_sq[color];
    bool in_check = is_square_attacked(s, king_sq, color == SEARCH_WHITE ? false : true);
    if (in_check) {
        return -SEARCH_MATE + depth;
    }
    return 0;
}

static int quiescence(SearchState *s, int alpha, int beta) {
    int stand = evaluate(s);
    if (stand >= beta) return beta;
    if (stand > alpha) alpha = stand;

    SearchMove moves[256];
    int count = generate_legal_moves(s, moves, 256, true);
    if (count <= 0) return stand;

    order_moves(s, moves, count, NULL, 0);
    for (int i = 0; i < count; ++i) {
        if (!is_capture_move(s, &moves[i])) continue;
        SearchUndo u;
        search_make_move(s, &moves[i], &u);
        int score = -quiescence(s, -beta, -alpha);
        search_unmake_move(s, &moves[i], &u);
        if (score >= beta) return beta;
        if (score > alpha) alpha = score;
    }
    return alpha;
}

static int negamax(SearchState *s, int depth, int alpha, int beta, SearchMove *out_best) {
    int alpha_orig = alpha;
    uint64_t key = s->key;
    SearchMove tt_best = {.from = {-1, -1}, .to = {-1, -1}, .hint = {0}};
    int tt_score = 0;
    bool tt_cut = tt_probe(key, depth, alpha, beta, &tt_score, &tt_best);
    if (tt_cut) {
        if (out_best && (tt_best.from.file >= 0)) {
            *out_best = tt_best;
        }
        return tt_score;
    }

    int color = s->white_to_move ? SEARCH_WHITE : SEARCH_BLACK;
    int king_sq = s->king_sq[color];
    bool in_check = is_square_attacked(s, king_sq, color == SEARCH_WHITE ? false : true);

    SearchMove moves[256];
    int count = generate_legal_moves(s, moves, 256, false);
    if (count == 0) {
        return terminal_score(s, depth);
    }
    if (depth == 0) {
        return quiescence(s, alpha, beta);
    }

    if (!in_check && depth >= 3) {
        SearchUndo nu;
        search_make_null(s, &nu);
        int score = -negamax(s, depth - 1 - 2, -beta, -beta + 1, NULL);
        search_unmake_null(s, &nu);
        if (score >= beta) {
            return beta;
        }
    }

    order_moves(s, moves, count, (tt_best.from.file >= 0) ? &tt_best : NULL, depth);

    int best = -SEARCH_INF;
    SearchMove best_move = {.from = {-1, -1}, .to = {-1, -1}, .hint = {0}};
    bool have_best = false;

    for (int i = 0; i < count; ++i) {
        bool was_capture = is_capture_move(s, &moves[i]);
        SearchUndo u;
        search_make_move(s, &moves[i], &u);
        int score;
        if (!in_check && depth >= 3 && !was_capture && i >= 3) {
            score = -negamax(s, depth - 1 - 1, -alpha - 1, -alpha, NULL);
            if (score > alpha) {
                score = -negamax(s, depth - 1, -beta, -alpha, NULL);
            }
        } else {
            score = -negamax(s, depth - 1, -beta, -alpha, NULL);
        }
        search_unmake_move(s, &moves[i], &u);
        if (score > best) {
            best = score;
            best_move = moves[i];
            have_best = true;
        }
        if (score > alpha) alpha = score;
        if (alpha >= beta) {
            if (!was_capture) {
                int from = sq_of(moves[i].from.file, moves[i].from.rank);
                int to = sq_of(moves[i].to.file, moves[i].to.rank);
                g_history[color][from][to] += depth * depth;
                if (!move_equal(&g_killers[depth][0], &moves[i])) {
                    g_killers[depth][1] = g_killers[depth][0];
                    g_killers[depth][0] = moves[i];
                }
            }
            break;
        }
    }

    if (out_best && have_best) {
        *out_best = best_move;
    }

    SearchTtFlag flag = TT_EXACT;
    if (best <= alpha_orig) {
        flag = TT_UPPER;
    } else if (best >= beta) {
        flag = TT_LOWER;
    }
    if (have_best) {
        tt_store(key, depth, best, flag, &best_move);
    }

    return best;
}

bool search_best_move(const GameState *state, int depth, SearchMove *out_move) {
    if (!state || !out_move) return false;
    init_zobrist();
    if (depth < 1) depth = 1;
    if (depth > 10) depth = 10;

    memset(g_history, 0, sizeof(g_history));
    memset(g_killers, 0, sizeof(g_killers));

    SearchState s;
    search_state_init(&s, state);

    SearchMove best = {.from = {-1, -1}, .to = {-1, -1}, .hint = {0}};
    bool found = false;
    int last_score = 0;
    for (int d = 1; d <= depth; ++d) {
        SearchMove iter_best = {.from = {-1, -1}, .to = {-1, -1}, .hint = {0}};
        int alpha = -SEARCH_INF;
        int beta = SEARCH_INF;
        if (d >= 2) {
            int window = 50;
            alpha = last_score - window;
            beta = last_score + window;
        }
        int score = negamax(&s, d, alpha, beta, &iter_best);
        if (score <= alpha || score >= beta) {
            score = negamax(&s, d, -SEARCH_INF, SEARCH_INF, &iter_best);
        }
        last_score = score;
        if (iter_best.from.file >= 0) {
            best = iter_best;
            found = true;
        }
    }

    if (!found) return false;
    *out_move = best;
    return true;
}
