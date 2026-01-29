#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

// Platform-agnostic filesystem operations for Windows, macOS, and Linux

// === Directory operations ===

// Create a directory. Returns true on success or if directory already exists.
bool fs_mkdir(const char *path);

// === File listing ===

typedef struct FsFileEntry {
    char name[260];        // File name
    time_t modified_time;  // Last modification time
    bool is_directory;     // True if this is a directory
} FsFileEntry;

typedef struct FsFileList {
    FsFileEntry *entries;  // Array of file entries
    int count;             // Number of entries
    int capacity;          // Allocated capacity
} FsFileList;

// List files matching a glob pattern (e.g., "*.txt", "saves/*.save")
// Returns a file list that must be freed with fs_file_list_free()
bool fs_list_files(const char *pattern, FsFileList *out_list);

// Free a file list returned by fs_list_files()
void fs_file_list_free(FsFileList *list);

// === File operations ===

// Check if a file exists and is readable
bool fs_file_exists(const char *path);

// Get absolute path for a relative path
// Returns true on success, false on error
bool fs_get_absolute_path(const char *relative_path, char *out_absolute, size_t out_size);

// === Process execution ===

typedef struct FsProcess FsProcess;

// Execute a command and open a pipe to read its output
// mode: "r" for reading stdout, "w" for writing to stdin
FsProcess *fs_popen(const char *command, const char *mode);

// Read a line from a process pipe (similar to fgets)
// Returns NULL on EOF or error
char *fs_pgets(char *buffer, int size, FsProcess *proc);

// Close a process pipe and return its exit code
int fs_pclose(FsProcess *proc);
