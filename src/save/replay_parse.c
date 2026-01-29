#include "save/replay_parse.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

int parse_square_list(const char *list, int8_t *files, int8_t *ranks, int max) {
    if (!list || !files || !ranks || max <= 0) return 0;
    if (strcmp(list, "-") == 0) return 0;
    int count = 0;
    const char *p = list;
    while (*p && count < max) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;
        char *end = NULL;
        long f = strtol(p, &end, 10);
        if (end == p || *end != ':') {
            while (*p && *p != ',') p++;
            if (*p == ',') p++;
            continue;
        }
        p = end + 1;
        long r = strtol(p, &end, 10);
        if (end == p) {
            while (*p && *p != ',') p++;
            if (*p == ',') p++;
            continue;
        }
        files[count] = (int8_t)f;
        ranks[count] = (int8_t)r;
        count++;
        p = end;
        while (*p && *p != ',') p++;
        if (*p == ',') p++;
    }
    return count;
}
