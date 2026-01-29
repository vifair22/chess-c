// Chess rules tests - move generation, check detection, attack detection
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game/logic.h"

// Test globals from test_main.c
extern int g_tests_run;
extern int g_tests_passed;
extern int g_tests_failed;
extern const char *g_current_test;

// Macros from test_main.c
#define TEST_PASS() do { \
    g_tests_passed++; \
    printf("\033[0;32m[PASS]\033[0m %s\n", g_current_test); \
} while(0)

#define TEST_FAIL(msg) do { \
    g_tests_failed++; \
    printf("\033[0;31m[FAIL]\033[0m %s: %s\n", g_current_test, msg); \
} while(0)

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        TEST_FAIL(msg); \
        return; \
    } \
} while(0)

#define TEST_ASSERT_TRUE(cond) TEST_ASSERT(cond, #cond " should be true")
#define TEST_ASSERT_FALSE(cond) TEST_ASSERT(!(cond), #cond " should be false")
#define TEST_ASSERT_EQUAL(expected, actual) do { \
    if ((expected) != (actual)) { \
        char buf[256]; \
        snprintf(buf, sizeof(buf), "Expected %d, got %d", (int)(expected), (int)(actual)); \
        TEST_FAIL(buf); \
        return; \
    } \
} while(0)

#define TEST(name) static void name(void)
#define RUN_TEST(test_func) do { \
    g_current_test = #test_func; \
    g_tests_run++; \
    int failed_before = g_tests_failed; \
    test_func(); \
    if (g_tests_failed == failed_before) { \
        TEST_PASS(); \
    } \
} while(0)

// Helper: setup minimal chess game state
static void setup_chess_state(GameState *state) {
    memset(state, 0, sizeof(*state));
    strncpy(state->match.mode, "chess", sizeof(state->match.mode));
    strncpy(state->match.phase, "play", sizeof(state->match.phase));
    state->match.white_to_move = true;
    state->match.turn_number = 1;
}

// Helper: place a piece on the board
static void place_piece(GameState *state, int file, int rank, char piece) {
    state->board_state.squares[rank][file].piece = piece;
}

// Helper: clear the entire board
static void clear_board(GameState *state) {
    for (int r = 0; r < 8; r++) {
        for (int f = 0; f < 8; f++) {
            state->board_state.squares[r][f].piece = '.';
            state->board_state.squares[r][f].flags = 0;
        }
    }
}

// Test: Initial chess position has kings in correct positions
TEST(test_find_king_initial_position) {
    GameState state = {0};
    game_init(&state);
    game_reset(&state, "chess");

    // Rank 0 = black's back rank (rank 8), Rank 7 = white's back rank (rank 1)
    BoardPos white_king = (BoardPos){4, 7}; // e1
    BoardPos black_king = (BoardPos){4, 0}; // e8

    char white_king_piece = game_piece_at(&state, white_king.file, white_king.rank);
    char black_king_piece = game_piece_at(&state, black_king.file, black_king.rank);

    if (white_king_piece != 'K') {
        TEST_FAIL("White king should be at e1");
        game_destroy(&state);
        return;
    }
    if (black_king_piece != 'k') {
        TEST_FAIL("Black king should be at e8");
        game_destroy(&state);
        return;
    }

    game_destroy(&state);
}

// Test: Empty board has no king (should return -1, -1)
TEST(test_find_king_empty_board) {
    GameState state = {0};
    setup_chess_state(&state);
    clear_board(&state);

    // Testing game_is_in_check which internally uses find_king
    // An empty board should not crash
    bool white_in_check = game_is_in_check(&state, true);
    bool black_in_check = game_is_in_check(&state, false);

    // No king means no check (defensive behavior)
    if (white_in_check) {
        TEST_FAIL("White should not be in check on an empty board");
        return;
    }
    if (black_in_check) {
        TEST_FAIL("Black should not be in check on an empty board");
        return;
    }
}

// Test: Square attacked by pawn
TEST(test_square_attacked_by_pawn) {
    GameState state = {0};
    setup_chess_state(&state);
    clear_board(&state);

    // White pawn on e2 (rank 6)
    place_piece(&state, 4, 6, 'P');

    // White pawn attacks d3 and f3 diagonally forward (rank decreases for white)
    // e2 attacks d3 (3,5) and f3 (5,5)
    bool d3_attacked = game_square_attacked_by(&state, 3, 5, true);
    bool f3_attacked = game_square_attacked_by(&state, 5, 5, true);
    bool e3_attacked = game_square_attacked_by(&state, 4, 5, true);

    if (!d3_attacked) {
        TEST_FAIL("d3 should be attacked by pawn");
        game_destroy(&state);
        return;
    }
    if (!f3_attacked) {
        TEST_FAIL("f3 should be attacked by pawn");
        game_destroy(&state);
        return;
    }
    if (e3_attacked) {
        TEST_FAIL("e3 should not be attacked (pawns don't attack forward)");
        game_destroy(&state);
        return;
    }

    game_destroy(&state);
}

// Test: Square attacked by knight
TEST(test_square_attacked_by_knight) {
    GameState state = {0};
    setup_chess_state(&state);
    clear_board(&state);

    // White knight on e4 (4, 3)
    place_piece(&state, 4, 3, 'N');

    // Knight attacks in L-shape
    bool d6_attacked = game_square_attacked_by(&state, 3, 5, true); // +2 rank, -1 file
    bool f6_attacked = game_square_attacked_by(&state, 5, 5, true); // +2 rank, +1 file
    bool e5_attacked = game_square_attacked_by(&state, 4, 4, true); // not knight move

    if (!d6_attacked) {
        TEST_FAIL("d6 should be attacked by knight");
        return;
    }
    if (!f6_attacked) {
        TEST_FAIL("f6 should be attacked by knight");
        return;
    }
    if (e5_attacked) {
        TEST_FAIL("e5 should not be attacked by knight");
        return;
    }
}

// Test: Square attacked by rook (horizontal)
TEST(test_square_attacked_by_rook_horizontal) {
    GameState state = {0};
    setup_chess_state(&state);
    clear_board(&state);

    // White rook on a1
    place_piece(&state, 0, 0, 'R');

    // Rook attacks entire first rank
    bool h1_attacked = game_square_attacked_by(&state, 7, 0, true);
    bool a8_attacked = game_square_attacked_by(&state, 0, 7, true);

    if (!h1_attacked) {
        TEST_FAIL("h1 should be attacked by rook");
        return;
    }
    if (!a8_attacked) {
        TEST_FAIL("a8 should be attacked by rook");
        return;
    }
}

// Test: Square attacked by rook (blocked)
TEST(test_square_attacked_by_rook_blocked) {
    GameState state = {0};
    setup_chess_state(&state);
    clear_board(&state);

    // White rook on a1
    place_piece(&state, 0, 0, 'R');
    // Blocking piece on a4
    place_piece(&state, 0, 3, 'P');

    // a8 should not be attacked (blocked by pawn)
    bool a8_attacked = game_square_attacked_by(&state, 0, 7, true);
    bool a3_attacked = game_square_attacked_by(&state, 0, 2, true);

    if (a8_attacked) {
        TEST_FAIL("a8 should not be attacked when blocked");
        return;
    }
    if (!a3_attacked) {
        TEST_FAIL("a3 should be attacked by rook");
        return;
    }
}

// Test: Square attacked by bishop (diagonal)
TEST(test_square_attacked_by_bishop) {
    GameState state = {0};
    setup_chess_state(&state);
    clear_board(&state);

    // White bishop on d4
    place_piece(&state, 3, 3, 'B');

    // Bishop attacks diagonals
    bool a1_attacked = game_square_attacked_by(&state, 0, 0, true);
    bool g7_attacked = game_square_attacked_by(&state, 6, 6, true);
    bool a7_attacked = game_square_attacked_by(&state, 0, 6, true);
    bool e5_attacked = game_square_attacked_by(&state, 4, 4, true);

    if (!a1_attacked) {
        TEST_FAIL("a1 should be attacked by bishop");
        return;
    }
    if (!g7_attacked) {
        TEST_FAIL("g7 should be attacked by bishop");
        return;
    }
    if (!a7_attacked) {
        TEST_FAIL("a7 should be attacked by bishop");
        return;
    }
    if (!e5_attacked) {
        TEST_FAIL("e5 should be attacked by bishop");
        return;
    }
}

// Test: Square attacked by queen (combines rook + bishop)
TEST(test_square_attacked_by_queen) {
    GameState state = {0};
    setup_chess_state(&state);
    clear_board(&state);

    // White queen on d4
    place_piece(&state, 3, 3, 'Q');

    // Queen attacks orthogonally
    bool d8_attacked = game_square_attacked_by(&state, 3, 7, true);
    bool a4_attacked = game_square_attacked_by(&state, 0, 3, true);

    // Queen attacks diagonally
    bool a1_attacked = game_square_attacked_by(&state, 0, 0, true);
    bool g7_attacked = game_square_attacked_by(&state, 6, 6, true);

    if (!d8_attacked) {
        TEST_FAIL("d8 should be attacked by queen");
        return;
    }
    if (!a4_attacked) {
        TEST_FAIL("a4 should be attacked by queen");
        return;
    }
    if (!a1_attacked) {
        TEST_FAIL("a1 should be attacked by queen");
        return;
    }
    if (!g7_attacked) {
        TEST_FAIL("g7 should be attacked by queen");
        return;
    }
}

// Test: Square attacked by king
TEST(test_square_attacked_by_king) {
    GameState state = {0};
    setup_chess_state(&state);
    clear_board(&state);

    // White king on e4
    place_piece(&state, 4, 3, 'K');

    // King attacks all 8 adjacent squares
    bool e5_attacked = game_square_attacked_by(&state, 4, 4, true);
    bool f5_attacked = game_square_attacked_by(&state, 5, 4, true);
    bool f4_attacked = game_square_attacked_by(&state, 5, 3, true);

    // But not distant squares
    bool e7_attacked = game_square_attacked_by(&state, 4, 6, true);

    if (!e5_attacked) {
        TEST_FAIL("e5 should be attacked by king");
        return;
    }
    if (!f5_attacked) {
        TEST_FAIL("f5 should be attacked by king");
        return;
    }
    if (!f4_attacked) {
        TEST_FAIL("f4 should be attacked by king");
        return;
    }
    if (e7_attacked) {
        TEST_FAIL("e7 should not be attacked by king");
        return;
    }
}

// Test: Check detection - king directly attacked by rook
TEST(test_check_by_rook) {
    GameState state = {0};
    setup_chess_state(&state);
    clear_board(&state);

    // White king on e1
    place_piece(&state, 4, 0, 'K');
    // Black rook on e8 (attacks king on e-file)
    place_piece(&state, 4, 7, 'r');

    bool white_in_check = game_is_in_check(&state, true);

    if (!white_in_check) {
        TEST_FAIL("White king should be in check from rook");
        return;
    }
}

// Test: Check detection - king not in check
TEST(test_no_check) {
    GameState state = {0};
    setup_chess_state(&state);
    clear_board(&state);

    // White king on e1
    place_piece(&state, 4, 0, 'K');
    // Black rook on a8 (does not attack king)
    place_piece(&state, 0, 7, 'r');

    bool white_in_check = game_is_in_check(&state, true);

    if (white_in_check) {
        TEST_FAIL("White king should not be in check");
        return;
    }
}

// Test: Check detection - king attacked by knight
TEST(test_check_by_knight) {
    GameState state = {0};
    setup_chess_state(&state);
    clear_board(&state);

    // White king on e4
    place_piece(&state, 4, 3, 'K');
    // Black knight on d6 (attacks e4 via knight move)
    place_piece(&state, 3, 5, 'n');

    bool white_in_check = game_is_in_check(&state, true);

    if (!white_in_check) {
        TEST_FAIL("White king should be in check from knight");
        return;
    }
}

// Test: Check detection - king attacked by bishop
TEST(test_check_by_bishop) {
    GameState state = {0};
    setup_chess_state(&state);
    clear_board(&state);

    // White king on e1
    place_piece(&state, 4, 0, 'K');
    // Black bishop on a5 (attacks e1 diagonally)
    place_piece(&state, 0, 4, 'b');

    bool white_in_check = game_is_in_check(&state, true);

    if (!white_in_check) {
        TEST_FAIL("White king should be in check from bishop");
        return;
    }
}

// Test: Check detection - king attacked by queen
TEST(test_check_by_queen) {
    GameState state = {0};
    setup_chess_state(&state);
    clear_board(&state);

    // White king on e1
    place_piece(&state, 4, 0, 'K');
    // Black queen on e8
    place_piece(&state, 4, 7, 'q');

    bool white_in_check = game_is_in_check(&state, true);

    if (!white_in_check) {
        TEST_FAIL("White king should be in check from queen");
        return;
    }
}

// Test: Check detection - king attacked by pawn
TEST(test_check_by_pawn) {
    GameState state = {0};
    setup_chess_state(&state);
    clear_board(&state);

    // White king on e4 (rank 4)
    place_piece(&state, 4, 4, 'K');
    // Black pawn on d5 (rank 3) attacks e4 diagonally
    place_piece(&state, 3, 3, 'p');

    bool white_in_check = game_is_in_check(&state, true);

    if (!white_in_check) {
        TEST_FAIL("White king should be in check from pawn");
        game_destroy(&state);
        return;
    }

    game_destroy(&state);
}

// Test: Pawn move generation - initial position allows 1 or 2 squares forward
TEST(test_pawn_initial_move) {
    GameState state = {0};
    game_init(&state);
    game_reset(&state, "chess");

    // Evaluate legal moves for white pawn on e2 (rank 6)
    game_eval_legal_moves(&state, 4, 6);

    // e3 (rank 5) and e4 (rank 4) should be legal
    MoveHint e3_hint = state.moves.legal_moves[5][4]; // rank 5, file 4
    MoveHint e4_hint = state.moves.legal_moves[4][4]; // rank 4, file 4

    if (!(e3_hint.flags & MOVE_LEGAL)) {
        TEST_FAIL("e3 should be legal from e2");
        game_destroy(&state);
        return;
    }
    if (!(e4_hint.flags & MOVE_LEGAL)) {
        TEST_FAIL("e4 should be legal from e2");
        game_destroy(&state);
        return;
    }

    game_destroy(&state);
}

// Test: King cannot move into check
TEST(test_king_cannot_move_into_check) {
    GameState state = {0};
    setup_chess_state(&state);
    clear_board(&state);

    // White king on e1
    place_piece(&state, 4, 0, 'K');
    // Black rook on d8 (controls d-file)
    place_piece(&state, 3, 7, 'r');

    // Evaluate legal moves for white king
    game_eval_legal_moves(&state, 4, 0);

    // d1 should NOT be legal (would move into check from rook on d8)
    MoveHint d1_hint = state.moves.legal_moves[0][3];

    if (d1_hint.flags & MOVE_LEGAL) {
        TEST_FAIL("d1 should be illegal (king would move into check)");
        return;
    }
}

// Test: Knight move generation
TEST(test_knight_moves) {
    GameState state = {0};
    setup_chess_state(&state);
    clear_board(&state);

    // Need kings for legal move generation
    place_piece(&state, 4, 7, 'K'); // White king on e1
    place_piece(&state, 4, 0, 'k'); // Black king on e8

    // White knight on e4 (rank 4)
    place_piece(&state, 4, 4, 'N');

    // Set white to move
    state.match.white_to_move = true;

    // Evaluate legal moves
    game_eval_legal_moves(&state, 4, 4);

    // Knights have up to 8 possible moves
    // d6 (3, 2) should be legal - knight at e4 can go to d6
    MoveHint d6_hint = state.moves.legal_moves[2][3];
    // f6 (5, 2) should be legal
    MoveHint f6_hint = state.moves.legal_moves[2][5];

    if (!(d6_hint.flags & MOVE_LEGAL)) {
        TEST_FAIL("d6 should be legal from e4");
        game_destroy(&state);
        return;
    }
    if (!(f6_hint.flags & MOVE_LEGAL)) {
        TEST_FAIL("f6 should be legal from e4");
        game_destroy(&state);
        return;
    }

    game_destroy(&state);
}

void run_chess_rules_tests(void) {
    printf("\n=== Chess Rules Tests ===\n");

    RUN_TEST(test_find_king_initial_position);
    RUN_TEST(test_find_king_empty_board);
    RUN_TEST(test_square_attacked_by_pawn);
    RUN_TEST(test_square_attacked_by_knight);
    RUN_TEST(test_square_attacked_by_rook_horizontal);
    RUN_TEST(test_square_attacked_by_rook_blocked);
    RUN_TEST(test_square_attacked_by_bishop);
    RUN_TEST(test_square_attacked_by_queen);
    RUN_TEST(test_square_attacked_by_king);
    RUN_TEST(test_check_by_rook);
    RUN_TEST(test_no_check);
    RUN_TEST(test_check_by_knight);
    RUN_TEST(test_check_by_bishop);
    RUN_TEST(test_check_by_queen);
    RUN_TEST(test_check_by_pawn);
    RUN_TEST(test_pawn_initial_move);
    RUN_TEST(test_king_cannot_move_into_check);
    RUN_TEST(test_knight_moves);
}
