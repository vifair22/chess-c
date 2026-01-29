// Lightweight unit test framework for chess-c
// No dependencies, simple assertion macros

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

// Test statistics (non-static so other test files can access them)
int g_tests_run = 0;
int g_tests_passed = 0;
int g_tests_failed = 0;
const char *g_current_test = NULL;

// Color output (optional)
#define COLOR_GREEN "\033[0;32m"
#define COLOR_RED "\033[0;31m"
#define COLOR_RESET "\033[0m"

// Test result reporting
#define TEST_PASS() do { \
    g_tests_passed++; \
    printf(COLOR_GREEN "[PASS]" COLOR_RESET " %s\n", g_current_test); \
} while(0)

#define TEST_FAIL(msg) do { \
    g_tests_failed++; \
    printf(COLOR_RED "[FAIL]" COLOR_RESET " %s: %s\n", g_current_test, msg); \
} while(0)

// Assertion macros
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

// Test definition
#define TEST(name) static void name(void)

// Test runner
#define RUN_TEST(test_func) do { \
    g_current_test = #test_func; \
    g_tests_run++; \
    int failed_before = g_tests_failed; \
    test_func(); \
    if (g_tests_failed == failed_before) { \
        TEST_PASS(); \
    } \
} while(0)

// Forward declarations of test suites
void run_state_tests(void);
void run_chess_rules_tests(void);
void run_moves_tests(void);

int main(int argc, char **argv) {
    // Initialize SDL (required for game_init which creates mutexes)
    if (SDL_Init(SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    printf("=== Chess-C Unit Tests ===\n\n");

    // Run test suites - one at a time for debugging
    printf("About to run state tests\n");
    fflush(stdout);
    run_state_tests();

    printf("About to run chess rules tests\n");
    fflush(stdout);
    run_chess_rules_tests();

    printf("About to run moves tests\n");
    fflush(stdout);
    run_moves_tests();

    // Print summary
    printf("\n=== Test Summary ===\n");
    printf("Total:  %d\n", g_tests_run);
    printf("Passed: " COLOR_GREEN "%d" COLOR_RESET "\n", g_tests_passed);
    printf("Failed: " COLOR_RED "%d" COLOR_RESET "\n", g_tests_failed);

    SDL_Quit();

    if (g_tests_failed == 0) {
        printf("\n" COLOR_GREEN "All tests passed!" COLOR_RESET "\n");
        return 0;
    } else {
        printf("\n" COLOR_RED "%d test(s) failed!" COLOR_RESET "\n", g_tests_failed);
        return 1;
    }
}
