#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <io.h>
#include <stdbool.h>
#include <time.h>

static int run_game(const char *exe, const char *rep, int watch, char *reason, size_t reason_sz, double *duration_sec) {
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "cmd /c \"\"%s\" --replay \"%s\" %s --test\"", exe, rep, watch ? "--no-headless" : "--headless");
    clock_t start = clock();
    FILE *fp = _popen(cmd, "rt");
    if (!fp) {
        if (reason && reason_sz > 0) snprintf(reason, reason_sz, "spawn failed");
        return 1;
    }
    char line[512];
    bool saw_result = false;
    bool pass = false;
    char last_info[256] = {0};
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "INFO:", 5) == 0 || strncmp(line, "WARN:", 5) == 0 || strncmp(line, "ERROR:", 6) == 0) {
            // Capture last log line for better failure context.
            strncpy(last_info, line, sizeof(last_info) - 1);
            last_info[sizeof(last_info) - 1] = '\0';
            size_t len = strlen(last_info);
            if (len > 0 && (last_info[len - 1] == '\n' || last_info[len - 1] == '\r')) last_info[len - 1] = '\0';
        }
        if (strncmp(line, "RESULT ", 7) == 0) {
            saw_result = true;
            pass = strstr(line, "PASS") != NULL;
        }
        fputs(line, stdout);
    }
    int rc = _pclose(fp);
    double dur = 0.0;
    if (duration_sec) {
        dur = (double)(clock() - start) / (double)CLOCKS_PER_SEC;
        *duration_sec = dur;
    }

    // Require both: process exited cleanly AND a RESULT line that says PASS.
    if (rc != 0) {
        if (reason && reason_sz > 0) {
            if (last_info[0]) {
                snprintf(reason, reason_sz, "%s", last_info);
            } else {
                snprintf(reason, reason_sz, "exit code %d", rc);
            }
        }
        return 1;
    }
    if (!saw_result) {
        if (reason && reason_sz > 0) snprintf(reason, reason_sz, "no RESULT line");
        return 1;
    }
    if (!pass) {
        if (reason && reason_sz > 0) {
            if (last_info[0]) {
                snprintf(reason, reason_sz, "%s", last_info);
            } else {
                snprintf(reason, reason_sz, "RESULT FAIL");
            }
        }
        return 1;
    }
    if (reason && reason_sz > 0) snprintf(reason, reason_sz, "PASS");
    return 0;
}

int main(int argc, char **argv) {
    char dir_buf[_MAX_PATH];
    char exe_buf[_MAX_PATH];
    char file_buf[_MAX_PATH];
    const char *dir = "replays";
    const char *exe = "..\\build\\Release\\chess-c.exe";
    const char *single_file = NULL;
    bool watch = false;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--dir") == 0 && i + 1 < argc) {
            dir = argv[++i];
        } else if (strcmp(argv[i], "--exe") == 0 && i + 1 < argc) {
            exe = argv[++i];
        } else if (strcmp(argv[i], "--file") == 0 && i + 1 < argc) {
            single_file = argv[++i];
        } else if (strcmp(argv[i], "--watch") == 0) {
            watch = true;
        }
    }

    if (!_fullpath(exe_buf, exe, sizeof(exe_buf))) {
        fprintf(stderr, "Bad exe path: %s\n", exe);
        return 1;
    }
    exe = exe_buf;

    int total = 0;
    int failed = 0;
    char failed_names[512][260];
    char failed_reasons[512][128];
    int failed_count = 0;

    if (single_file) {
        if (!_fullpath(file_buf, single_file, sizeof(file_buf))) {
            fprintf(stderr, "Bad file path: %s\n", single_file);
            return 1;
        }
        single_file = file_buf;
        if (_access(single_file, 4) != 0) {
            fprintf(stderr, "Replay file not found: %s\n", single_file);
            return 1;
        }
        const char *name_only = strrchr(single_file, '\\');
        name_only = name_only ? name_only + 1 : single_file;
        printf("\n=== RUN %s ===\n", name_only);
        char reason[128] = {0};
        double dur = 0.0;
        int res = run_game(exe, single_file, watch, reason, sizeof(reason), &dur);
        ++total;
        if (res != 0) {
            ++failed;
            if (failed_count < 512) {
                strncpy(failed_names[failed_count++], name_only, sizeof(failed_names[0]) - 1);
                failed_names[failed_count - 1][sizeof(failed_names[0]) - 1] = '\0';
                strncpy(failed_reasons[failed_count - 1], reason, sizeof(failed_reasons[0]) - 1);
                failed_reasons[failed_count - 1][sizeof(failed_reasons[0]) - 1] = '\0';
            }
            printf("-> FAIL (%s) [%.2fs]\n", reason, dur);
        } else {
            printf("-> PASS [%.2fs]\n", dur);
        }
    } else {
        if (!_fullpath(dir_buf, dir, sizeof(dir_buf))) {
            fprintf(stderr, "Bad dir path: %s\n", dir);
            return 1;
        }
        dir = dir_buf;

        char pattern[512];
        snprintf(pattern, sizeof(pattern), "%s\\*.rep", dir);
        intptr_t handle;
        struct _finddata_t find;
        handle = _findfirst(pattern, &find);
        if (handle == -1L) {
            fprintf(stderr, "No replay files found in %s\n", dir);
            SDL_Quit();
            return 1;
        }
        do {
            if (find.attrib & _A_SUBDIR) continue;
            char path[512];
            snprintf(path, sizeof(path), "%s\\%s", dir, find.name);
            printf("\n=== RUN %s ===\n", find.name);
            char reason[128] = {0};
            double dur = 0.0;
            int res = run_game(exe, path, watch, reason, sizeof(reason), &dur);
            ++total;
            if (res != 0) {
                ++failed;
                if (failed_count < 512) {
                    strncpy(failed_names[failed_count++], find.name, sizeof(failed_names[0]) - 1);
                    failed_names[failed_count - 1][sizeof(failed_names[0]) - 1] = '\0';
                    strncpy(failed_reasons[failed_count - 1], reason, sizeof(failed_reasons[0]) - 1);
                    failed_reasons[failed_count - 1][sizeof(failed_reasons[0]) - 1] = '\0';
                }
                printf("-> FAIL (%s) [%.2fs]\n", reason, dur);
            } else {
                printf("-> PASS [%.2fs]\n", dur);
            }
        } while (_findnext(handle, &find) == 0);
        _findclose(handle);
    }

    printf("\n=== SUMMARY ===\n");
    printf("TOTAL %d  PASSED %d  FAILED %d\n", total, total - failed, failed);
    if (failed_count > 0) {
        printf("FAILED TESTS:\n");
        for (int i = 0; i < failed_count; ++i) {
            printf(" - %s (%s)\n", failed_names[i], failed_reasons[i]);
        }
    }
    return failed == 0 ? 0 : 1;
}
