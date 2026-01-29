#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>

#include "core/filesystem.h"

static int run_game(const char *exe, const char *rep, int watch, char *reason, size_t reason_sz, double *duration_sec) {
    char cmd[1024];
#ifdef _WIN32
    snprintf(cmd, sizeof(cmd), "cmd /c \"\"%s\" --replay \"%s\" %s --test\"", exe, rep, watch ? "--no-headless" : "--headless");
#else
    snprintf(cmd, sizeof(cmd), "\"%s\" --replay \"%s\" %s --test", exe, rep, watch ? "--no-headless" : "--headless");
#endif
    clock_t start = clock();
    FsProcess *proc = fs_popen(cmd, "r");
    if (!proc) {
        if (reason && reason_sz > 0) snprintf(reason, reason_sz, "spawn failed");
        return 1;
    }
    char line[512];
    bool saw_result = false;
    bool pass = false;
    char last_info[256] = {0};
    while (fs_pgets(line, sizeof(line), proc)) {
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
        if (watch) {
            fputs(line, stdout);
        }
    }
    int rc = fs_pclose(proc);
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
    char dir_buf[1024];
    char exe_buf[1024];
    char file_buf[1024];
    const char *dir = "replays";
#ifdef _WIN32
    const char *exe = "..\\build\\Release\\chess-c.exe";
#else
    const char *exe = "../build/Release/chess-c";
#endif
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

    if (!fs_get_absolute_path(exe, exe_buf, sizeof(exe_buf))) {
        fprintf(stderr, "Bad exe path: %s\n", exe);
        return 1;
    }
    exe = exe_buf;

    int total = 0;
    int failed = 0;
    const int max_fail = 512;
    char (*failed_names)[260] = calloc((size_t)max_fail, sizeof(*failed_names));
    char (*failed_reasons)[128] = calloc((size_t)max_fail, sizeof(*failed_reasons));
    if (!failed_names || !failed_reasons) {
        fprintf(stderr, "Allocation failure\n");
        free(failed_names);
        free(failed_reasons);
        return 1;
    }
    int failed_count = 0;

    if (single_file) {
        if (!fs_get_absolute_path(single_file, file_buf, sizeof(file_buf))) {
            fprintf(stderr, "Bad file path: %s\n", single_file);
            return 1;
        }
        single_file = file_buf;
        if (!fs_file_exists(single_file)) {
            fprintf(stderr, "Replay file not found: %s\n", single_file);
            return 1;
        }
#ifdef _WIN32
        const char *name_only = strrchr(single_file, '\\');
#else
        const char *name_only = strrchr(single_file, '/');
#endif
        name_only = name_only ? name_only + 1 : single_file;
        if (watch) {
            printf("\n=== RUN %s ===\n", name_only);
        }
        char reason[128] = {0};
        double dur = 0.0;
        int res = run_game(exe, single_file, watch, reason, sizeof(reason), &dur);
        ++total;
        if (res != 0) {
            ++failed;
            if (failed_count < max_fail) {
                strncpy(failed_names[failed_count++], name_only, sizeof(failed_names[0]) - 1);
                failed_names[failed_count - 1][sizeof(failed_names[0]) - 1] = '\0';
                strncpy(failed_reasons[failed_count - 1], reason, sizeof(failed_reasons[0]) - 1);
                failed_reasons[failed_count - 1][sizeof(failed_reasons[0]) - 1] = '\0';
            }
            printf("-> FAIL %s (%s) [%.2fs]\n", name_only, reason, dur);
        } else {
            if (watch) {
                printf("-> PASS [%.2fs]\n", dur);
            }
        }
    } else {
        if (!fs_get_absolute_path(dir, dir_buf, sizeof(dir_buf))) {
            fprintf(stderr, "Bad dir path: %s\n", dir);
            return 1;
        }
        dir = dir_buf;

        char pattern[512];
        snprintf(pattern, sizeof(pattern), "%s/*.rep", dir);

        FsFileList file_list;
        if (!fs_list_files(pattern, &file_list)) {
            fprintf(stderr, "No replay files found in %s\n", dir);
            SDL_Quit();
            return 1;
        }

        for (int i = 0; i < file_list.count; ++i) {
            FsFileEntry *entry = &file_list.entries[i];
            if (entry->is_directory) continue;

            char path[512];
#ifdef _WIN32
            snprintf(path, sizeof(path), "%s\\%s", dir, entry->name);
#else
            snprintf(path, sizeof(path), "%s/%s", dir, entry->name);
#endif

            if (watch) {
                printf("\n=== RUN %s ===\n", entry->name);
            }
            char reason[128] = {0};
            double dur = 0.0;
            int res = run_game(exe, path, watch, reason, sizeof(reason), &dur);
            ++total;
            if (res != 0) {
                ++failed;
                if (failed_count < max_fail) {
                    strncpy(failed_names[failed_count++], entry->name, sizeof(failed_names[0]) - 1);
                    failed_names[failed_count - 1][sizeof(failed_names[0]) - 1] = '\0';
                    strncpy(failed_reasons[failed_count - 1], reason, sizeof(failed_reasons[0]) - 1);
                    failed_reasons[failed_count - 1][sizeof(failed_reasons[0]) - 1] = '\0';
                }
                printf("-> FAIL %s (%s) [%.2fs]\n", entry->name, reason, dur);
            } else {
                if (watch) {
                    printf("-> PASS [%.2fs]\n", dur);
                }
            }
        }

        fs_file_list_free(&file_list);
    }

    printf("\n=== SUMMARY ===\n");
    printf("TOTAL %d  PASSED %d  FAILED %d\n", total, total - failed, failed);
    if (failed_count > 0) {
        printf("FAILED TESTS:\n");
        for (int i = 0; i < failed_count; ++i) {
            printf(" - %s (%s)\n", failed_names[i], failed_reasons[i]);
        }
    }
    free(failed_names);
    free(failed_reasons);
    return failed == 0 ? 0 : 1;
}
