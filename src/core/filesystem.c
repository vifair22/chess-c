#include "core/filesystem.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef _WIN32
    #include <direct.h>
    #include <io.h>
    #include <sys/types.h>
    #include <sys/stat.h>
    #define PATH_MAX _MAX_PATH
#else
    #include <dirent.h>
    #include <unistd.h>
    #include <sys/stat.h>
    #include <sys/types.h>
    #include <fnmatch.h>
    #include <limits.h>
#endif

// === Directory operations ===

bool fs_mkdir(const char *path) {
    if (!path) return false;

#ifdef _WIN32
    int result = _mkdir(path);
#else
    int result = mkdir(path, 0755);
#endif

    if (result == 0) {
        return true;
    }
    // Success if directory already exists
    return errno == EEXIST;
}

// === File listing ===

void fs_file_list_free(FsFileList *list) {
    if (!list) return;
    free(list->entries);
    list->entries = NULL;
    list->count = 0;
    list->capacity = 0;
}

static bool fs_file_list_add(FsFileList *list, const char *name, time_t mtime, bool is_dir) {
    if (list->count >= list->capacity) {
        int new_cap = list->capacity ? list->capacity * 2 : 16;
        FsFileEntry *new_entries = (FsFileEntry *)realloc(list->entries, new_cap * sizeof(FsFileEntry));
        if (!new_entries) {
            return false;
        }
        list->entries = new_entries;
        list->capacity = new_cap;
    }
    FsFileEntry *entry = &list->entries[list->count++];
    strncpy(entry->name, name, sizeof(entry->name) - 1);
    entry->name[sizeof(entry->name) - 1] = '\0';
    entry->modified_time = mtime;
    entry->is_directory = is_dir;
    return true;
}

#ifdef _WIN32

bool fs_list_files(const char *pattern, FsFileList *out_list) {
    if (!pattern || !out_list) return false;
    memset(out_list, 0, sizeof(*out_list));

    struct _finddata_t file_info;
    intptr_t handle = _findfirst(pattern, &file_info);
    if (handle == -1) {
        return false;
    }

    do {
        bool is_dir = (file_info.attrib & _A_SUBDIR) != 0;
        if (!fs_file_list_add(out_list, file_info.name, file_info.time_write, is_dir)) {
            _findclose(handle);
            fs_file_list_free(out_list);
            return false;
        }
    } while (_findnext(handle, &file_info) == 0);

    _findclose(handle);
    return true;
}

#else // POSIX (Linux/macOS)

// Extract directory and pattern from a glob pattern like "saves/*.save"
static bool parse_glob_pattern(const char *pattern, char *dir_out, size_t dir_size,
                               char *pattern_out, size_t pattern_size) {
    const char *last_slash = strrchr(pattern, '/');
    if (last_slash) {
        size_t dir_len = last_slash - pattern;
        if (dir_len >= dir_size) return false;
        strncpy(dir_out, pattern, dir_len);
        dir_out[dir_len] = '\0';
        strncpy(pattern_out, last_slash + 1, pattern_size - 1);
        pattern_out[pattern_size - 1] = '\0';
    } else {
        strncpy(dir_out, ".", dir_size - 1);
        dir_out[dir_size - 1] = '\0';
        strncpy(pattern_out, pattern, pattern_size - 1);
        pattern_out[pattern_size - 1] = '\0';
    }
    return true;
}

bool fs_list_files(const char *pattern, FsFileList *out_list) {
    if (!pattern || !out_list) return false;
    memset(out_list, 0, sizeof(*out_list));

    char dir_path[PATH_MAX];
    char file_pattern[PATH_MAX];
    if (!parse_glob_pattern(pattern, dir_path, sizeof(dir_path),
                           file_pattern, sizeof(file_pattern))) {
        return false;
    }

    DIR *dir = opendir(dir_path);
    if (!dir) {
        return false;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        // Skip . and ..
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        // Match against pattern
        if (fnmatch(file_pattern, entry->d_name, 0) != 0) {
            continue;
        }

        // Get file stats for modification time
        char full_path[PATH_MAX];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);

        struct stat st;
        bool is_dir = false;
        time_t mtime = 0;
        if (stat(full_path, &st) == 0) {
            mtime = st.st_mtime;
            is_dir = S_ISDIR(st.st_mode);
        }

        if (!fs_file_list_add(out_list, entry->d_name, mtime, is_dir)) {
            closedir(dir);
            fs_file_list_free(out_list);
            return false;
        }
    }

    closedir(dir);
    return true;
}

#endif

// === File operations ===

bool fs_file_exists(const char *path) {
    if (!path) return false;

#ifdef _WIN32
    return _access(path, 0) == 0;
#else
    return access(path, F_OK) == 0;
#endif
}

bool fs_get_absolute_path(const char *relative_path, char *out_absolute, size_t out_size) {
    if (!relative_path || !out_absolute || out_size == 0) return false;

#ifdef _WIN32
    char *result = _fullpath(out_absolute, relative_path, out_size);
    return result != NULL;
#else
    char *result = realpath(relative_path, NULL);
    if (!result) return false;

    size_t len = strlen(result);
    if (len >= out_size) {
        free(result);
        return false;
    }

    strncpy(out_absolute, result, out_size - 1);
    out_absolute[out_size - 1] = '\0';
    free(result);
    return true;
#endif
}

// === Process execution ===

struct FsProcess {
    FILE *pipe;
};

FsProcess *fs_popen(const char *command, const char *mode) {
    if (!command || !mode) return NULL;

    FILE *pipe;
#ifdef _WIN32
    pipe = _popen(command, mode);
#else
    pipe = popen(command, mode);
#endif

    if (!pipe) return NULL;

    FsProcess *proc = (FsProcess *)malloc(sizeof(FsProcess));
    if (!proc) {
#ifdef _WIN32
        _pclose(pipe);
#else
        pclose(pipe);
#endif
        return NULL;
    }

    proc->pipe = pipe;
    return proc;
}

char *fs_pgets(char *buffer, int size, FsProcess *proc) {
    if (!buffer || !proc || !proc->pipe) return NULL;
    return fgets(buffer, size, proc->pipe);
}

int fs_pclose(FsProcess *proc) {
    if (!proc) return -1;

    int result;
#ifdef _WIN32
    result = _pclose(proc->pipe);
#else
    result = pclose(proc->pipe);
#endif

    free(proc);
    return result;
}
