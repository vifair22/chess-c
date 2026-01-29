// State management tests - undo/redo, position hashing, repetition detection
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

#define TEST_ASSERT_NULL(ptr) TEST_ASSERT((ptr) == NULL, #ptr " should be NULL")
#define TEST_ASSERT_NOT_NULL(ptr) TEST_ASSERT((ptr) != NULL, #ptr " should not be NULL")

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

// Helper: create a minimal test state
static void setup_test_state(GameState *state) {
    game_init(state);
    game_reset(state, "chess");
}

static void teardown_test_state(GameState *state) {
    game_destroy(state);
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

// Test that undo/redo functionality works (tests snapshot indirectly)
TEST(test_snapshot_push) {
    GameState state = {0};
    bool initialized = false;

    setup_test_state(&state);
    initialized = true;

    // Verify initial pawn position is white pawn
    // Note: Rank 0 = chess rank 8 (black), Rank 7 = chess rank 1 (white)
    // e2 = file 4, rank 6
    char initial_piece = game_piece_at(&state, 4, 6);
    if (initial_piece != 'P') {
        char buf[256];
        snprintf(buf, sizeof(buf), "Expected 'P' at e2, got '%c' (ASCII %d)",
                 initial_piece, (int)initial_piece);
        TEST_FAIL(buf);
        if (initialized) teardown_test_state(&state);
        return;
    }

    // Make a move (implicitly creates a snapshot)
    // e2-e4 = {4,6} to {4,4}
    BoardPos from = {4, 6}; // e2
    BoardPos to = {4, 4};   // e4
    MoveHint hint = {MOVE_LEGAL, '\0'};
    bool move_success = game_apply_move(&state, from, to, hint);

    if (!move_success) {
        TEST_FAIL("Move e2-e4 should succeed");
        teardown_test_state(&state);
        return;
    }

    // Verify the move was made
    if (game_piece_at(&state, 4, 6) != '.') {
        TEST_FAIL("e2 should be empty after move");
        teardown_test_state(&state);
        return;
    }
    if (game_piece_at(&state, 4, 4) != 'P') {
        TEST_FAIL("e4 should have white pawn after move");
        teardown_test_state(&state);
        return;
    }

    // Undo should restore
    bool undo_success = game_undo(&state);
    if (!undo_success) {
        TEST_FAIL("Undo should succeed");
        teardown_test_state(&state);
        return;
    }

    if (game_piece_at(&state, 4, 6) != 'P') {
        TEST_FAIL("e2 should have white pawn after undo");
        teardown_test_state(&state);
        return;
    }
    if (game_piece_at(&state, 4, 4) != '.') {
        TEST_FAIL("e4 should be empty after undo");
        teardown_test_state(&state);
        return;
    }

    teardown_test_state(&state);
}

// Test that multiple undos work correctly (tests snapshot buffer indirectly)
TEST(test_snapshot_push_wrap) {
    GameState state = {0};
    setup_test_state(&state);

    // Make several moves with correct coordinates
    // e2-e4 (white)
    if (!game_apply_move(&state, (BoardPos){4, 6}, (BoardPos){4, 4}, (MoveHint){MOVE_LEGAL, '\0'})) {
        TEST_FAIL("e2-e4 should succeed");
        teardown_test_state(&state);
        return;
    }
    // e7-e5 (black)
    if (!game_apply_move(&state, (BoardPos){4, 1}, (BoardPos){4, 3}, (MoveHint){MOVE_LEGAL, '\0'})) {
        TEST_FAIL("e7-e5 should succeed");
        teardown_test_state(&state);
        return;
    }
    // Nf3 (white): g1-f3
    if (!game_apply_move(&state, (BoardPos){6, 7}, (BoardPos){5, 5}, (MoveHint){MOVE_LEGAL, '\0'})) {
        TEST_FAIL("Nf3 should succeed");
        teardown_test_state(&state);
        return;
    }
    // Nc6 (black): b8-c6
    if (!game_apply_move(&state, (BoardPos){1, 0}, (BoardPos){2, 2}, (MoveHint){MOVE_LEGAL, '\0'})) {
        TEST_FAIL("Nc6 should succeed");
        teardown_test_state(&state);
        return;
    }

    // Verify we can undo multiple times
    if (!game_undo(&state)) { // Undo Nc6
        TEST_FAIL("First undo should succeed");
        teardown_test_state(&state);
        return;
    }
    if (!game_undo(&state)) { // Undo Nf3
        TEST_FAIL("Second undo should succeed");
        teardown_test_state(&state);
        return;
    }
    if (!game_undo(&state)) { // Undo e5
        TEST_FAIL("Third undo should succeed");
        teardown_test_state(&state);
        return;
    }
    if (!game_undo(&state)) { // Undo e4
        TEST_FAIL("Fourth undo should succeed");
        teardown_test_state(&state);
        return;
    }

    // Should be back to initial position
    if (game_piece_at(&state, 4, 6) != 'P') {
        TEST_FAIL("e2 should have white pawn after all undos");
        teardown_test_state(&state);
        return;
    }
    if (game_piece_at(&state, 6, 7) != 'N') {
        TEST_FAIL("g1 should have white knight after all undos");
        teardown_test_state(&state);
        return;
    }

    teardown_test_state(&state);
}

// Test game_undo restores previous state
TEST(test_undo_restores_state) {
    GameState state = {0};
    setup_test_state(&state);

    // Remember initial turn
    int initial_turn = state.match.turn_number;
    bool initial_white_to_move = state.match.white_to_move;

    // Make a move (e2-e4)
    if (!game_apply_move(&state, (BoardPos){4, 6}, (BoardPos){4, 4}, (MoveHint){MOVE_LEGAL, '\0'})) {
        TEST_FAIL("Move should succeed");
        teardown_test_state(&state);
        return;
    }

    // Turn should have changed
    if (!(state.match.turn_number > initial_turn || state.match.white_to_move != initial_white_to_move)) {
        TEST_FAIL("Turn should change after move");
        teardown_test_state(&state);
        return;
    }

    // Undo should restore
    bool success = game_undo(&state);

    if (!success) {
        TEST_FAIL("Undo should succeed");
        teardown_test_state(&state);
        return;
    }
    if (state.match.turn_number != initial_turn) {
        TEST_FAIL("Turn number should be restored");
        teardown_test_state(&state);
        return;
    }
    if (state.match.white_to_move != initial_white_to_move) {
        TEST_FAIL("Side to move should be restored");
        teardown_test_state(&state);
        return;
    }

    teardown_test_state(&state);
}

// Test undo fails when no snapshots available
TEST(test_undo_fails_when_empty) {
    GameState state = {0};

    printf("  test_undo_fails_when_empty: calling setup\n");
    fflush(stdout);
    setup_test_state(&state);

    printf("  test_undo_fails_when_empty: calling game_undo\n");
    fflush(stdout);
    bool success = game_undo(&state);

    printf("  test_undo_fails_when_empty: game_undo returned %d\n", success);
    fflush(stdout);

    if (success) {
        TEST_FAIL("Undo should fail when no moves have been made");
        teardown_test_state(&state);
        return;
    }

    printf("  test_undo_fails_when_empty: calling teardown\n");
    fflush(stdout);
    teardown_test_state(&state);
    printf("  test_undo_fails_when_empty: teardown done\n");
    fflush(stdout);
}

// Test position_hash is consistent for same position
TEST(test_position_hash_consistent) {
    GameState state1 = {0}, state2 = {0};
    bool init1 = false, init2 = false;

    setup_test_state(&state1);
    init1 = true;
    setup_test_state(&state2);
    init2 = true;

    // Set same position
    state1.board_state.squares[0][0].piece = 'R';
    state1.board_state.squares[7][7].piece = 'k';
    state1.match.white_to_move = true;

    state2.board_state.squares[0][0].piece = 'R';
    state2.board_state.squares[7][7].piece = 'k';
    state2.match.white_to_move = true;

    uint64_t hash1 = position_hash(&state1);
    uint64_t hash2 = position_hash(&state2);

    if (hash1 != hash2) {
        TEST_FAIL("Hashes should be equal for same position");
        if (init1) teardown_test_state(&state1);
        if (init2) teardown_test_state(&state2);
        return;
    }

    teardown_test_state(&state1);
    teardown_test_state(&state2);
}

// Test position_hash differs for different positions
TEST(test_position_hash_differs) {
    GameState state1 = {0}, state2 = {0};
    bool init1 = false, init2 = false;
    setup_test_state(&state1);
    init1 = true;
    setup_test_state(&state2);
    init2 = true;

    // Different piece
    state1.board_state.squares[0][0].piece = 'R';
    state2.board_state.squares[0][0].piece = 'N';

    uint64_t hash1 = position_hash(&state1);
    uint64_t hash2 = position_hash(&state2);

    if (!(hash1 != hash2)) {
        TEST_FAIL("Hashes should be different");
        if (init1) teardown_test_state(&state1);
        if (init2) teardown_test_state(&state2);
        return;
    }

    teardown_test_state(&state1);
    teardown_test_state(&state2);
}

// Test position_hash differs for different side to move
TEST(test_position_hash_side_to_move) {
    GameState state1 = {0}, state2 = {0};
    bool init1 = false, init2 = false;
    setup_test_state(&state1);
    init1 = true;
    setup_test_state(&state2);
    init2 = true;

    // Same pieces, different side to move
    state1.board_state.squares[0][0].piece = 'R';
    state2.board_state.squares[0][0].piece = 'R';
    state1.match.white_to_move = true;
    state2.match.white_to_move = false;

    uint64_t hash1 = position_hash(&state1);
    uint64_t hash2 = position_hash(&state2);

    if (!(hash1 != hash2)) {
        TEST_FAIL("Hashes should be different");
        if (init1) teardown_test_state(&state1);
        if (init2) teardown_test_state(&state2);
        return;
    }

    teardown_test_state(&state1);
    teardown_test_state(&state2);
}

// Test position_hash includes castling rights
TEST(test_position_hash_castling) {
    GameState state1 = {0}, state2 = {0};
    bool init1 = false, init2 = false;
    setup_test_state(&state1);
    init1 = true;
    setup_test_state(&state2);
    init2 = true;

    // Same position, different castling rights
    state1.chess.rights.white_can_castle_kingside = true;
    state2.chess.rights.white_can_castle_kingside = false;

    uint64_t hash1 = position_hash(&state1);
    uint64_t hash2 = position_hash(&state2);

    if (!(hash1 != hash2)) {
        TEST_FAIL("Hashes should be different");
        if (init1) teardown_test_state(&state1);
        if (init2) teardown_test_state(&state2);
        return;
    }

    teardown_test_state(&state1);
    teardown_test_state(&state2);
}

// Test position_hash includes en passant
TEST(test_position_hash_en_passant) {
    GameState state1 = {0}, state2 = {0};
    bool init1 = false, init2 = false;
    setup_test_state(&state1);
    init1 = true;
    setup_test_state(&state2);
    init2 = true;

    // Same position, different en passant target
    state1.chess.rights.en_passant_target = (BoardPos){2, 2};
    state2.chess.rights.en_passant_target = (BoardPos){-1, -1};

    uint64_t hash1 = position_hash(&state1);
    uint64_t hash2 = position_hash(&state2);

    if (!(hash1 != hash2)) {
        TEST_FAIL("Hashes should be different");
        if (init1) teardown_test_state(&state1);
        if (init2) teardown_test_state(&state2);
        return;
    }

    teardown_test_state(&state1);
    teardown_test_state(&state2);
}

// Test repetition_count with initial position
TEST(test_repetition_count_empty) {
    GameState state = {0};
    setup_test_state(&state);

    // game_reset records the initial position, so count should be 1
    int count = repetition_count(&state);

    if (count != 1) {
        TEST_FAIL("Initial position should have count of 1");
        teardown_test_state(&state);
        return;
    }

    teardown_test_state(&state);
}

// Test repetition_count after recording same position again
TEST(test_repetition_count_single) {
    GameState state = {0};
    setup_test_state(&state);

    // Record the same position again
    record_position(&state);
    int count = repetition_count(&state);

    // Should have 2 (initial + 1 duplicate)
    if (count != 2) {
        TEST_FAIL("Should have count of 2 after recording duplicate");
        teardown_test_state(&state);
        return;
    }

    teardown_test_state(&state);
}

// Test repetition_count detects triple repetition
TEST(test_repetition_count_triple) {
    GameState state = {0};
    setup_test_state(&state);

    // Record same position three more times (plus initial = 4 total)
    record_position(&state);
    record_position(&state);
    record_position(&state);

    int count = repetition_count(&state);

    // Should have 4 (initial + 3 more)
    if (count != 4) {
        TEST_FAIL("Should have count of 4 for triple repetition");
        teardown_test_state(&state);
        return;
    }

    teardown_test_state(&state);
}

// Test repetition_count with alternating positions
TEST(test_repetition_count_alternating) {
    GameState state = {0};
    setup_test_state(&state);

    // Position 1 (modify and record)
    state.board_state.squares[0][0].piece = 'R';
    record_position(&state);

    // Position 2 (different)
    state.board_state.squares[0][0].piece = 'N';
    record_position(&state);

    // Back to position 1
    state.board_state.squares[0][0].piece = 'R';
    record_position(&state);

    // Current position (R) should appear twice in history
    int count = repetition_count(&state);
    if (count != 2) {
        TEST_FAIL("Should have count of 2 for repeated position");
        teardown_test_state(&state);
        return;
    }

    teardown_test_state(&state);
}

// Test insufficient_material: K vs K
TEST(test_insufficient_material_k_vs_k) {
    GameState state = {0};
    setup_test_state(&state);
    clear_board(&state);

    // Only kings
    state.board_state.squares[0][0].piece = 'K';
    state.board_state.squares[7][7].piece = 'k';

    bool insufficient = is_insufficient_material(&state);

    if (!insufficient) {
        TEST_FAIL("K vs K should be insufficient material");
        teardown_test_state(&state);
        return;
    }

    teardown_test_state(&state);
}

// Test insufficient_material: K+N vs K
TEST(test_insufficient_material_kn_vs_k) {
    GameState state = {0};
    setup_test_state(&state);
    clear_board(&state);

    state.board_state.squares[0][0].piece = 'K';
    state.board_state.squares[1][0].piece = 'N';
    state.board_state.squares[7][7].piece = 'k';

    bool insufficient = is_insufficient_material(&state);

    if (!insufficient) {
        TEST_FAIL("K+N vs K should be insufficient material");
        teardown_test_state(&state);
        return;
    }

    teardown_test_state(&state);
}

// Test insufficient_material: K+B vs K
TEST(test_insufficient_material_kb_vs_k) {
    GameState state = {0};
    setup_test_state(&state);
    clear_board(&state);

    state.board_state.squares[0][0].piece = 'K';
    state.board_state.squares[2][0].piece = 'B';
    state.board_state.squares[7][7].piece = 'k';

    bool insufficient = is_insufficient_material(&state);

    if (!insufficient) {
        TEST_FAIL("K+B vs K should be insufficient material");
        teardown_test_state(&state);
        return;
    }

    teardown_test_state(&state);
}

// Test insufficient_material: K+B vs K+B (same color bishops)
TEST(test_insufficient_material_kb_vs_kb_same_color) {
    GameState state = {0};
    setup_test_state(&state);
    clear_board(&state);

    // Both bishops on dark squares (file+rank sum is odd)
    state.board_state.squares[0][0].piece = 'K';
    state.board_state.squares[0][1].piece = 'B'; // dark square (0+1=1, odd)
    state.board_state.squares[7][7].piece = 'k';
    state.board_state.squares[7][6].piece = 'b'; // dark square (7+6=13, odd)

    bool insufficient = is_insufficient_material(&state);

    if (!insufficient) {
        TEST_FAIL("K+B vs K+B (same color) should be insufficient material");
        teardown_test_state(&state);
        return;
    }

    teardown_test_state(&state);
}

// Test insufficient_material: K+B vs K+B (opposite color bishops)
TEST(test_insufficient_material_kb_vs_kb_opposite_color) {
    GameState state = {0};
    setup_test_state(&state);
    clear_board(&state);

    // Bishops on opposite colors
    state.board_state.squares[0][0].piece = 'K';
    state.board_state.squares[0][1].piece = 'B'; // dark square (0+1=1, odd)
    state.board_state.squares[7][7].piece = 'k';
    state.board_state.squares[6][6].piece = 'b'; // light square (6+6=12, even)

    bool insufficient = is_insufficient_material(&state);

    // Opposite color bishops is NOT insufficient
    if (insufficient) {
        TEST_FAIL("K+B vs K+B (opposite colors) should NOT be insufficient material");
        teardown_test_state(&state);
        return;
    }

    teardown_test_state(&state);
}

// Test insufficient_material: K+Q vs K (not insufficient)
TEST(test_insufficient_material_kq_vs_k) {
    GameState state = {0};
    setup_test_state(&state);
    clear_board(&state);

    state.board_state.squares[0][0].piece = 'K';
    state.board_state.squares[3][0].piece = 'Q';
    state.board_state.squares[7][7].piece = 'k';

    bool insufficient = is_insufficient_material(&state);

    if (insufficient) {
        TEST_FAIL("K+Q vs K should NOT be insufficient material");
        teardown_test_state(&state);
        return;
    }

    teardown_test_state(&state);
}

// Test insufficient_material: K+P vs K (not insufficient)
TEST(test_insufficient_material_kp_vs_k) {
    GameState state = {0};
    setup_test_state(&state);
    clear_board(&state);

    state.board_state.squares[0][0].piece = 'K';
    state.board_state.squares[4][1].piece = 'P';
    state.board_state.squares[7][7].piece = 'k';

    bool insufficient = is_insufficient_material(&state);

    if (insufficient) {
        TEST_FAIL("K+P vs K should NOT be insufficient material");
        teardown_test_state(&state);
        return;
    }

    teardown_test_state(&state);
}

// Test game_peek_undo_white_to_move
TEST(test_peek_undo_white_to_move) {
    GameState state = {0};
    setup_test_state(&state);

    // Make a move to create a snapshot (e2-e4)
    if (!game_apply_move(&state, (BoardPos){4, 6}, (BoardPos){4, 4}, (MoveHint){MOVE_LEGAL, '\0'})) {
        TEST_FAIL("Move should succeed");
        teardown_test_state(&state);
        return;
    }

    // Peek should show the previous state had white to move
    bool white_to_move;
    bool success = game_peek_undo_white_to_move(&state, &white_to_move);

    if (!success) {
        TEST_FAIL("Peek undo should succeed");
        teardown_test_state(&state);
        return;
    }
    if (!white_to_move) {
        TEST_FAIL("Previous state should have white to move");
        teardown_test_state(&state);
        return;
    }

    teardown_test_state(&state);
}

void run_state_tests(void) {
    printf("=== State Tests ===\n");
    fflush(stdout);

    printf("Running test 1/21: test_snapshot_push\n");
    fflush(stdout);
    RUN_TEST(test_snapshot_push);

    printf("Running test 2/21: test_snapshot_push_wrap\n");
    fflush(stdout);
    RUN_TEST(test_snapshot_push_wrap);

    printf("Running test 3/21: test_undo_restores_state\n");
    fflush(stdout);
    RUN_TEST(test_undo_restores_state);

    printf("Running test 4/21: test_undo_fails_when_empty\n");
    fflush(stdout);
    RUN_TEST(test_undo_fails_when_empty);

    printf("Running test 5/21: test_position_hash_consistent\n");
    fflush(stdout);
    RUN_TEST(test_position_hash_consistent);

    printf("Running test 6/21: test_position_hash_differs\n");
    fflush(stdout);
    RUN_TEST(test_position_hash_differs);

    printf("Running test 7/21: test_position_hash_side_to_move\n");
    fflush(stdout);
    RUN_TEST(test_position_hash_side_to_move);

    printf("Running test 8/21: test_position_hash_castling\n");
    fflush(stdout);
    RUN_TEST(test_position_hash_castling);

    printf("Running test 9/21: test_position_hash_en_passant\n");
    fflush(stdout);
    RUN_TEST(test_position_hash_en_passant);

    printf("Running test 10/21: test_repetition_count_empty\n");
    fflush(stdout);
    RUN_TEST(test_repetition_count_empty);

    printf("Running test 11/21: test_repetition_count_single\n");
    fflush(stdout);
    RUN_TEST(test_repetition_count_single);

    printf("Running test 12/21: test_repetition_count_triple\n");
    fflush(stdout);
    RUN_TEST(test_repetition_count_triple);

    printf("Running test 13/21: test_repetition_count_alternating\n");
    fflush(stdout);
    RUN_TEST(test_repetition_count_alternating);

    printf("Running test 14/21: test_insufficient_material_k_vs_k\n");
    fflush(stdout);
    RUN_TEST(test_insufficient_material_k_vs_k);

    printf("Running test 15/21: test_insufficient_material_kn_vs_k\n");
    fflush(stdout);
    RUN_TEST(test_insufficient_material_kn_vs_k);

    printf("Running test 16/21: test_insufficient_material_kb_vs_k\n");
    fflush(stdout);
    RUN_TEST(test_insufficient_material_kb_vs_k);

    printf("Running test 17/21: test_insufficient_material_kb_vs_kb_same_color\n");
    fflush(stdout);
    RUN_TEST(test_insufficient_material_kb_vs_kb_same_color);

    printf("Running test 18/21: test_insufficient_material_kb_vs_kb_opposite_color\n");
    fflush(stdout);
    RUN_TEST(test_insufficient_material_kb_vs_kb_opposite_color);

    printf("Running test 19/21: test_insufficient_material_kq_vs_k\n");
    fflush(stdout);
    RUN_TEST(test_insufficient_material_kq_vs_k);

    printf("Running test 20/21: test_insufficient_material_kp_vs_k\n");
    fflush(stdout);
    RUN_TEST(test_insufficient_material_kp_vs_k);

    printf("Running test 21/21: test_peek_undo_white_to_move\n");
    fflush(stdout);
    RUN_TEST(test_peek_undo_white_to_move);
}
