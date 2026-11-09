#pragma once

#include <stddef.h>
#include <stdint.h>

namespace didar {

using KvHandler = int (*)(const char* key, const char* value, void* ctx);

/** Feeds the -c/--config file, then every --flag value pair (dashes become underscores), to on_kv. Returns 0 or -1. */
int load_config(int argc, char** argv, KvHandler on_kv, void* ctx);

int parse_u32(const char* text, uint32_t min, uint32_t max, uint32_t* out);
int parse_port(const char* text, uint16_t* out);

/** Copies the text before the last sep into left and points right just after it. Returns 0 or -1. */
int split_at_last(const char* text, char sep, char* left, size_t left_cap, const char** right);

}
