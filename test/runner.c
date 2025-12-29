#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <io.h>
#include <stdbool.h>

static int run_game(const char *exe, const char *rep, int watch) {
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "cmd /c \"\"%s\" --replay \"%s\" %s --test\"", exe, rep, watch ? "--no-headless" : "--headless");
    FILE *fp = _popen(cmd, "rt");
    if (!fp) {
        fprintf(stderr, "FAILED SPAWN %s\n", cmd);
        return 1;
    }
    char line[512];
    int pass = -1;
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "RESULT ", 7) == 0) {
            if (strstr(line, "PASS")) pass = 0;
            else pass = 1;
        }
        fputs(line, stdout);
    }
    int rc = _pclose(fp);
    if (pass == -1) pass = rc;
    return pass;
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
        int res = run_game(exe, single_file, watch);
        ++total;
        if (res != 0) ++failed;
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
            int res = run_game(exe, path, watch);
            ++total;
            if (res != 0) {
                ++failed;
            }
        } while (_findnext(handle, &find) == 0);
        _findclose(handle);
    }

    printf("TESTS %d FAILED %d PASSED %d\n", total, failed, total - failed);
    return failed == 0 ? 0 : 1;
}
