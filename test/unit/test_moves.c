// Move utilities tests - SAN generation, move recording, basic validation
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

#define TEST_ASSERT_STRING(expected, actual) do { \
    if (strcmp(expected, actual) != 0) { \
        char buf[256]; \
        snprintf(buf, sizeof(buf), "Expected \"%s\", got \"%s\"", expected, actual); \
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

// Test: Standard opening move e4
TEST(test_standard_opening_e4) {
    GameState state = {0};
    game_init(&state);
    game_reset(&state, "chess");

    // Make move e2-e4
    BoardPos from = {4, 6}; // e2 (rank 6)
    BoardPos to = {4, 4};   // e4 (rank 4)
    MoveHint hint = {MOVE_LEGAL, '\0'};

    bool success = game_apply_move(&state, from, to, hint);

    if (!success) {
        TEST_FAIL("e2-e4 should succeed");
        game_destroy(&state);
        return;
    }
    if (game_piece_at(&state, 4, 6) != '.') {
        TEST_FAIL("e2 should be empty");
        game_destroy(&state);
        return;
    }
    if (game_piece_at(&state, 4, 4) != 'P') {
        TEST_FAIL("e4 should have pawn");
        game_destroy(&state);
        return;
    }

    game_destroy(&state);
}

// Test: Knight move Nf3
TEST(test_knight_move) {
    GameState state = {0};
    game_init(&state);
    game_reset(&state, "chess");

    // First move pawn to make space
    if (!game_apply_move(&state, (BoardPos){4, 6}, (BoardPos){4, 4}, (MoveHint){MOVE_LEGAL, '\0'})) {
        TEST_FAIL("e4 should succeed");
        game_destroy(&state);
        return;
    }
    // Black pawn move
    if (!game_apply_move(&state, (BoardPos){4, 1}, (BoardPos){4, 3}, (MoveHint){MOVE_LEGAL, '\0'})) {
        TEST_FAIL("e5 should succeed");
        game_destroy(&state);
        return;
    }

    // Now move knight from g1 to f3 (rank 7 to rank 5)
    BoardPos from = {6, 7}; // g1
    BoardPos to = {5, 5};   // f3
    MoveHint hint = {MOVE_LEGAL, '\0'};

    bool success = game_apply_move(&state, from, to, hint);

    if (!success) {
        TEST_FAIL("Nf3 should succeed");
        game_destroy(&state);
        return;
    }
    if (game_piece_at(&state, 6, 7) != '.') {
        TEST_FAIL("g1 should be empty");
        game_destroy(&state);
        return;
    }
    if (game_piece_at(&state, 5, 5) != 'N') {
        TEST_FAIL("f3 should have knight");
        game_destroy(&state);
        return;
    }

    game_destroy(&state);
}

// Test: Pawn capture
TEST(test_pawn_capture) {
    GameState state = {0};
    game_init(&state);
    game_reset(&state, "chess");

    // Setup: Move pawns to create capture opportunity
    // 1. e4
    if (!game_apply_move(&state, (BoardPos){4, 6}, (BoardPos){4, 4}, (MoveHint){MOVE_LEGAL, '\0'})) {
        TEST_FAIL("e4 should succeed");
        game_destroy(&state);
        return;
    }
    // 1... d5
    if (!game_apply_move(&state, (BoardPos){3, 1}, (BoardPos){3, 3}, (MoveHint){MOVE_LEGAL, '\0'})) {
        TEST_FAIL("d5 should succeed");
        game_destroy(&state);
        return;
    }
    // 2. exd5 (pawn captures)
    bool success = game_apply_move(&state, (BoardPos){4, 4}, (BoardPos){3, 3}, (MoveHint){MOVE_LEGAL, '\0'});

    if (!success) {
        TEST_FAIL("exd5 should succeed");
        game_destroy(&state);
        return;
    }
    if (game_piece_at(&state, 4, 4) != '.') {
        TEST_FAIL("e4 should be empty after capture");
        game_destroy(&state);
        return;
    }
    if (game_piece_at(&state, 3, 3) != 'P') {
        TEST_FAIL("d5 should have white pawn");
        game_destroy(&state);
        return;
    }
    if (state.white_captures_len != 1) {
        TEST_FAIL("White should have captured one piece");
        game_destroy(&state);
        return;
    }

    game_destroy(&state);
}

// Test: Castling kingside
TEST(test_castling_kingside) {
    GameState state = {0};
    game_init(&state);
    game_reset(&state, "chess");

    // Clear squares between king and rook (rank 7 = white's back rank)
    state.board_state.squares[7][5].piece = '.'; // f1
    state.board_state.squares[7][6].piece = '.'; // g1

    // Ensure castling rights are set
    state.chess.rights.white_can_castle_kingside = true;

    // Perform kingside castle
    BoardPos from = {4, 7}; // e1
    BoardPos to = {6, 7};   // g1
    MoveHint hint = {MOVE_LEGAL | MOVE_CASTLE_KING, '\0'};

    bool success = game_apply_move(&state, from, to, hint);

    if (!success) {
        TEST_FAIL("O-O should succeed");
        game_destroy(&state);
        return;
    }
    if (game_piece_at(&state, 6, 7) != 'K') {
        TEST_FAIL("King should be on g1");
        game_destroy(&state);
        return;
    }
    if (game_piece_at(&state, 5, 7) != 'R') {
        TEST_FAIL("Rook should be on f1");
        game_destroy(&state);
        return;
    }

    game_destroy(&state);
}

// Test: Castling queenside
TEST(test_castling_queenside) {
    GameState state = {0};
    game_init(&state);
    game_reset(&state, "chess");

    // Clear squares between king and rook (rank 7 = white's back rank)
    state.board_state.squares[7][1].piece = '.'; // b1
    state.board_state.squares[7][2].piece = '.'; // c1
    state.board_state.squares[7][3].piece = '.'; // d1

    // Ensure castling rights are set
    state.chess.rights.white_can_castle_queenside = true;

    // Perform queenside castle
    BoardPos from = {4, 7}; // e1
    BoardPos to = {2, 7};   // c1
    MoveHint hint = {MOVE_LEGAL | MOVE_CASTLE_QUEEN, '\0'};

    bool success = game_apply_move(&state, from, to, hint);

    if (!success) {
        TEST_FAIL("O-O-O should succeed");
        game_destroy(&state);
        return;
    }
    if (game_piece_at(&state, 2, 7) != 'K') {
        TEST_FAIL("King should be on c1");
        game_destroy(&state);
        return;
    }
    if (game_piece_at(&state, 3, 7) != 'R') {
        TEST_FAIL("Rook should be on d1");
        game_destroy(&state);
        return;
    }

    game_destroy(&state);
}

// Test: Pawn promotion
TEST(test_pawn_promotion) {
    GameState state = {0};
    game_init(&state);
    game_reset(&state, "chess");

    // Setup: Place white pawn on 7th rank (rank 1 in array)
    state.board_state.squares[1][4].piece = 'P'; // e7
    state.board_state.squares[6][4].piece = '.'; // clear e2

    // Promote to queen: e7-e8
    BoardPos from = {4, 1}; // e7
    BoardPos to = {4, 0};   // e8
    MoveHint hint = {MOVE_LEGAL | MOVE_PROMOTION, 'Q'};

    bool success = game_apply_move(&state, from, to, hint);

    if (!success) {
        TEST_FAIL("Promotion should succeed");
        game_destroy(&state);
        return;
    }
    if (game_piece_at(&state, 4, 0) != 'Q') {
        TEST_FAIL("e8 should have queen");
        game_destroy(&state);
        return;
    }

    game_destroy(&state);
}

// Test: En passant capture
TEST(test_en_passant) {
    GameState state = {0};
    game_init(&state);
    game_reset(&state, "chess");

    // Setup position for en passant:
    // 1. Place white pawn on e5 (rank 3 in array)
    state.board_state.squares[3][4].piece = 'P'; // e5
    state.board_state.squares[6][4].piece = '.'; // clear e2

    // 2. Simulate black pawn double-move from d7 to d5
    state.board_state.squares[3][3].piece = 'p'; // d5
    state.board_state.squares[1][3].piece = '.'; // clear d7
    state.chess.rights.en_passant_target = (BoardPos){3, 2}; // d6 is en passant target

    // Now white can capture en passant: e5xd6
    BoardPos from = {4, 3}; // e5
    BoardPos to = {3, 2};   // d6
    MoveHint hint = {MOVE_LEGAL | MOVE_EN_PASSANT, '\0'};

    bool success = game_apply_move(&state, from, to, hint);

    if (!success) {
        TEST_FAIL("En passant should succeed");
        game_destroy(&state);
        return;
    }
    if (game_piece_at(&state, 3, 2) != 'P') {
        TEST_FAIL("d6 should have white pawn");
        game_destroy(&state);
        return;
    }
    if (game_piece_at(&state, 3, 3) != '.') {
        TEST_FAIL("d5 should be empty (captured)");
        game_destroy(&state);
        return;
    }

    game_destroy(&state);
}

// Test: Move changes turn
TEST(test_move_changes_turn) {
    GameState state = {0};
    game_init(&state);
    game_reset(&state, "chess");

    if (!state.match.white_to_move) {
        TEST_FAIL("Should start with white to move");
        game_destroy(&state);
        return;
    }

    // White moves e4
    if (!game_apply_move(&state, (BoardPos){4, 6}, (BoardPos){4, 4}, (MoveHint){MOVE_LEGAL, '\0'})) {
        TEST_FAIL("e4 should succeed");
        game_destroy(&state);
        return;
    }

    if (state.match.white_to_move) {
        TEST_FAIL("Should be black's turn after white move");
        game_destroy(&state);
        return;
    }

    // Black moves e5
    if (!game_apply_move(&state, (BoardPos){4, 1}, (BoardPos){4, 3}, (MoveHint){MOVE_LEGAL, '\0'})) {
        TEST_FAIL("e5 should succeed");
        game_destroy(&state);
        return;
    }

    if (!state.match.white_to_move) {
        TEST_FAIL("Should be white's turn after black move");
        game_destroy(&state);
        return;
    }

    game_destroy(&state);
}

// Test: Initial position has correct pieces
TEST(test_initial_position) {
    GameState state = {0};
    game_init(&state);
    game_reset(&state, "chess");

    // Check white pieces (rank 7 = back rank, rank 6 = pawns)
    if (game_piece_at(&state, 0, 7) != 'R') { TEST_FAIL("a1 should have white rook"); game_destroy(&state); return; }
    if (game_piece_at(&state, 1, 7) != 'N') { TEST_FAIL("b1 should have white knight"); game_destroy(&state); return; }
    if (game_piece_at(&state, 2, 7) != 'B') { TEST_FAIL("c1 should have white bishop"); game_destroy(&state); return; }
    if (game_piece_at(&state, 3, 7) != 'Q') { TEST_FAIL("d1 should have white queen"); game_destroy(&state); return; }
    if (game_piece_at(&state, 4, 7) != 'K') { TEST_FAIL("e1 should have white king"); game_destroy(&state); return; }
    if (game_piece_at(&state, 4, 6) != 'P') { TEST_FAIL("e2 should have white pawn"); game_destroy(&state); return; }

    // Check black pieces (rank 0 = back rank, rank 1 = pawns)
    if (game_piece_at(&state, 0, 0) != 'r') { TEST_FAIL("a8 should have black rook"); game_destroy(&state); return; }
    if (game_piece_at(&state, 1, 0) != 'n') { TEST_FAIL("b8 should have black knight"); game_destroy(&state); return; }
    if (game_piece_at(&state, 4, 0) != 'k') { TEST_FAIL("e8 should have black king"); game_destroy(&state); return; }
    if (game_piece_at(&state, 4, 1) != 'p') { TEST_FAIL("e7 should have black pawn"); game_destroy(&state); return; }

    game_destroy(&state);
}

// Test: Capture increments capture list
TEST(test_capture_list) {
    GameState state = {0};
    game_init(&state);
    game_reset(&state, "chess");

    if (state.white_captures_len != 0) { TEST_FAIL("White captures should start at 0"); game_destroy(&state); return; }
    if (state.black_captures_len != 0) { TEST_FAIL("Black captures should start at 0"); game_destroy(&state); return; }

    // Setup and perform a capture
    game_apply_move(&state, (BoardPos){4, 6}, (BoardPos){4, 4}, (MoveHint){MOVE_LEGAL, '\0'}); // e4
    game_apply_move(&state, (BoardPos){3, 1}, (BoardPos){3, 3}, (MoveHint){MOVE_LEGAL, '\0'}); // d5
    game_apply_move(&state, (BoardPos){4, 4}, (BoardPos){3, 3}, (MoveHint){MOVE_LEGAL, '\0'}); // exd5

    if (state.white_captures_len != 1) { TEST_FAIL("White should have 1 capture"); game_destroy(&state); return; }
    if (state.white_captures[0] != 'p') { TEST_FAIL("White should have captured black pawn"); game_destroy(&state); return; }

    game_destroy(&state);
}

// Test: game_is_white_piece and game_is_black_piece
TEST(test_piece_color_detection) {
    // No state needed, just function tests
    if (!game_is_white_piece('P')) { TEST_FAIL("P should be white"); return; }
    if (!game_is_white_piece('N')) { TEST_FAIL("N should be white"); return; }
    if (!game_is_white_piece('K')) { TEST_FAIL("K should be white"); return; }
    if (!game_is_white_piece('Q')) { TEST_FAIL("Q should be white"); return; }

    if (!game_is_black_piece('p')) { TEST_FAIL("p should be black"); return; }
    if (!game_is_black_piece('n')) { TEST_FAIL("n should be black"); return; }
    if (!game_is_black_piece('k')) { TEST_FAIL("k should be black"); return; }
    if (!game_is_black_piece('q')) { TEST_FAIL("q should be black"); return; }

    if (game_is_white_piece('p')) { TEST_FAIL("p should not be white"); return; }
    if (game_is_black_piece('P')) { TEST_FAIL("P should not be black"); return; }
    if (game_is_white_piece('.')) { TEST_FAIL(". should not be white"); return; }
    if (game_is_black_piece('.')) { TEST_FAIL(". should not be black"); return; }
}

// Test: Move history is recorded
TEST(test_move_history) {
    GameState state = {0};
    game_init(&state);
    game_reset(&state, "chess");

    if (state.move_history_len != 0) { TEST_FAIL("History should start empty"); game_destroy(&state); return; }

    // Make a move e2-e4
    game_apply_move(&state, (BoardPos){4, 6}, (BoardPos){4, 4}, (MoveHint){MOVE_LEGAL, '\0'});

    if (state.move_history_len != 1) { TEST_FAIL("History should have 1 move"); game_destroy(&state); return; }
    if (state.move_history[0].from.file != 4) { TEST_FAIL("From file should be 4"); game_destroy(&state); return; }
    if (state.move_history[0].from.rank != 6) { TEST_FAIL("From rank should be 6 (e2)"); game_destroy(&state); return; }
    if (state.move_history[0].to.file != 4) { TEST_FAIL("To file should be 4"); game_destroy(&state); return; }
    if (state.move_history[0].to.rank != 4) { TEST_FAIL("To rank should be 4 (e4)"); game_destroy(&state); return; }

    game_destroy(&state);
}

// Test: Checkers mode initialization
TEST(test_checkers_init) {
    GameState state = {0};
    game_init(&state);
    game_reset(&state, "checkers");

    // Checkers has pieces on dark squares of first 3 ranks
    // Rank 7,6,5 (white side): should have white pieces on dark squares
    // In checkers, pieces start on dark squares where (file + rank) % 2 == 1
    // Check a few positions
    char a1 = game_piece_at(&state, 0, 7); // a1 - light square (0+7=7 odd) should have piece
    char b1 = game_piece_at(&state, 1, 7); // b1 - dark square (1+7=8 even) should be empty

    // For checkers, check that pieces are placed correctly
    bool has_white_pieces = false;
    for (int r = 5; r <= 7; r++) {
        for (int f = 0; f < 8; f++) {
            char p = game_piece_at(&state, f, r);
            if (p == 'M') has_white_pieces = true;
        }
    }

    if (!has_white_pieces) { TEST_FAIL("Should have white pieces in checkers"); game_destroy(&state); return; }

    game_destroy(&state);
}

void run_moves_tests(void) {
    printf("\n=== Move Utilities Tests ===\n");

    printf("Test 1/13: test_standard_opening_e4\n"); fflush(stdout);
    RUN_TEST(test_standard_opening_e4);
    printf("Test 2/13: test_knight_move\n"); fflush(stdout);
    RUN_TEST(test_knight_move);
    printf("Test 3/13: test_pawn_capture\n"); fflush(stdout);
    RUN_TEST(test_pawn_capture);
    printf("Test 4/13: test_castling_kingside\n"); fflush(stdout);
    RUN_TEST(test_castling_kingside);
    printf("Test 5/13: test_castling_queenside\n"); fflush(stdout);
    RUN_TEST(test_castling_queenside);
    printf("Test 6/13: test_pawn_promotion\n"); fflush(stdout);
    RUN_TEST(test_pawn_promotion);
    printf("Test 7/13: test_en_passant\n"); fflush(stdout);
    RUN_TEST(test_en_passant);
    printf("Test 8/13: test_move_changes_turn\n"); fflush(stdout);
    RUN_TEST(test_move_changes_turn);
    printf("Test 9/13: test_initial_position\n"); fflush(stdout);
    RUN_TEST(test_initial_position);
    printf("Test 10/13: test_capture_list\n"); fflush(stdout);
    RUN_TEST(test_capture_list);
    printf("Test 11/13: test_piece_color_detection\n"); fflush(stdout);
    RUN_TEST(test_piece_color_detection);
    printf("Test 12/13: test_move_history\n"); fflush(stdout);
    RUN_TEST(test_move_history);
    printf("Test 13/13: test_checkers_init\n"); fflush(stdout);
    RUN_TEST(test_checkers_init);
}
