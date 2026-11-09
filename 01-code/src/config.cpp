#include "config.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace didar {
namespace {

char* trim(char* text) {
    while (isspace(static_cast<unsigned char>(*text))) text++;
    char* end = text + strlen(text);
    while (end > text && isspace(static_cast<unsigned char>(end[-1]))) *--end = '\0';
    return text;
}

bool is_config_flag(const char* arg) {
    return !strcmp(arg, "-c") || !strcmp(arg, "--config");
}

int load_file(const char* path, KvHandler on_kv, void* ctx) {
    FILE* file = fopen(path, "r");
    if (!file) {
        fprintf(stderr, "%s: %s\n", path, strerror(errno));
        return -1;
    }
    char line[512];
    int line_no = 0;
    int rc = 0;
    while (rc == 0 && fgets(line, sizeof line, file)) {
        line_no++;
        if (char* comment = strchr(line, '#')) *comment = '\0';
        char* text = trim(line);
        if (!*text) continue;
        char* eq = strchr(text, '=');
        if (!eq) {
            rc = -1;
        } else {
            *eq = '\0';
            rc = on_kv(trim(text), trim(eq + 1), ctx);
        }
        if (rc) fprintf(stderr, "%s:%d: invalid line\n", path, line_no);
    }
    fclose(file);
    return rc ? -1 : 0;
}

}

int load_config(int argc, char** argv, KvHandler on_kv, void* ctx) {
    for (int i = 1; i + 1 < argc; i += 2)
        if (is_config_flag(argv[i]) && load_file(argv[i + 1], on_kv, ctx)) return -1;
    for (int i = 1; i < argc; i += 2) {
        const char* flag = argv[i];
        if (i + 1 == argc) {
            fprintf(stderr, "%s: missing value\n", flag);
            return -1;
        }
        if (is_config_flag(flag)) continue;
        char key[64];
        if (strncmp(flag, "--", 2) || strlen(flag + 2) >= sizeof key) {
            fprintf(stderr, "%s: unknown flag\n", flag);
            return -1;
        }
        size_t n = 0;
        for (const char* p = flag + 2; *p; p++) key[n++] = *p == '-' ? '_' : *p;
        key[n] = '\0';
        if (on_kv(key, argv[i + 1], ctx)) {
            fprintf(stderr, "%s %s: invalid value\n", flag, argv[i + 1]);
            return -1;
        }
    }
    return 0;
}

int parse_u32(const char* text, uint32_t min, uint32_t max, uint32_t* out) {
    char* end;
    errno = 0;
    unsigned long value = strtoul(text, &end, 10);
    if (errno || end == text || *end || *text == '-' || value < min || value > max) return -1;
    *out = uint32_t(value);
    return 0;
}

int parse_port(const char* text, uint16_t* out) {
    uint32_t value;
    if (parse_u32(text, 1, 65535, &value)) return -1;
    *out = uint16_t(value);
    return 0;
}

int split_at_last(const char* text, char sep, char* left, size_t left_cap, const char** right) {
    const char* at = strrchr(text, sep);
    if (!at || size_t(at - text) >= left_cap) return -1;
    memcpy(left, text, size_t(at - text));
    left[at - text] = '\0';
    *right = at + 1;
    return 0;
}

}
