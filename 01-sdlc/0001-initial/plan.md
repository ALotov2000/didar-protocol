# 0001 — Initial implementation (phase 1)

## Context

The design of didar is complete in `00-docs/` (architecture.md, api.md, requirements.md). This plan turns it into code: `libdidar`, `didard`, and `didar-agent` in C-style C++ on Linux, built with CMake, with test cases for every requirement scenario. Phase 2 (encryption) is out of scope because the crypto library is still open.

The plan is stored at `01-sdlc/0001-initial/plan.md`. A new `plan-format` skill makes every future plan include diffs of the main code, the library header that forms our interface, and the requirement test code, but no unit tests.

## Scope

| In scope (phase 1) | Deferred (phase 2) |
| ------------------ | ------------------ |
| FR-1, FR-4 (encryption-disabled path only), FR-5, SR-3, SR-4, PR-1 (plaintext), PR-2 (plaintext) | FR-2, FR-3, FR-4 (encrypted path), SR-1, SR-2, PR-1 and PR-2 encrypted limits |

## Execution order

1. Create `.claude/skills/plan-format/SKILL.md`, copy this plan to `01-sdlc/0001-initial/plan.md`, and add the build folder to `.gitignore`. Ask before committing.
2. Apply `01-code/CMakeLists.txt`, `include/didar.h`, and the test code diffs. Create empty stubs so the build links, and confirm every requirement test fails. Then **stop for approval** (test-driven skill).
3. Apply the remaining code diffs, build, and run `ctest` until it passes.
4. Docs follow-up through the `docs-change-approval` skill, covering two behaviours this plan pins down that the docs don't state yet:
   - `didard` answers datagrams larger than 1472 bytes with `MALFORMED`, and drops replies larger than the UDP limit.
   - List keys (`allow`, `map`) add up across the config file and flags; scalar keys are overridden by flags.
5. Ask before committing.

## Layout

```text
01-code/
├── CMakeLists.txt
├── include/didar.h        public C API (libdidar)
├── src/wire.h, wire.cpp   header encode/decode, error codes
├── src/client.cpp         libdidar client API
├── src/config.h, .cpp     key = value file + --flags, shared by both programs
├── src/flow_table.h       fixed-size open-addressing flow table, shared by both programs
├── src/didard.cpp         server
├── src/didar_agent.cpp    client agent
└── tests/                 harness.h + one test_<requirement>.cpp per requirement (public interface only)
```

## Code diffs

### Skill and repo files

```diff
diff --git a/.claude/skills/plan-format/SKILL.md b/.claude/skills/plan-format/SKILL.md
new file mode 100644
--- /dev/null
+++ b/.claude/skills/plan-format/SKILL.md
@@ -0,0 +1,15 @@
+---
+name: plan-format
+description: Required format for implementation plans Claude writes in this repo, stored as 01-sdlc/NNNN-<slug>/plan.md. Use whenever creating or updating a plan, including in plan mode.
+---
+
+# Plan format
+
+- **Location:** `01-sdlc/NNNN-<slug>/plan.md`, where `NNNN` is the next 4-digit number.
+- **Sections:** Context, Scope, Execution order, Layout (if files are added), Code diffs, Test cases, Verification.
+- **Code diffs:** every change to main code (sources, headers, build files, configs, skills) as complete unified diffs (`diff --git`, new files from `/dev/null`). No elisions such as `...`.
+- **Interface first:** the library's public header (`01-code/include/didar.h`) is always part of the diffs when it changes; it is our code's interface.
+- **Requirement test cases:** one table per requirement in `00-docs/requirements.md` that the plan touches, with one case per scenario: every path and error branch in its sequence diagram. Columns: `ID` (`<requirement>.<n>`, e.g. `FR-1.2`), `Scenario`, `Steps`, `Expected`. Requirements the plan does not implement are listed as deferred with the reason.
+- **Requirement test code:** diffs for the requirement tests (`01-code/tests/`), one function per test case ID. They use only the interface: `didar.h`, the documented wire format, and the built programs. Never internal `src/` headers.
+- **No unit tests:** never include unit tests of internal functions in a plan.
+- **Order:** tests are written first and must fail before code is written (test-driven skill).
diff --git a/.gitignore b/.gitignore
--- a/.gitignore
+++ b/.gitignore
@@ -1 +1,2 @@
 node_modules/
+01-code/build/
```

### Build

```diff
diff --git a/01-code/CMakeLists.txt b/01-code/CMakeLists.txt
new file mode 100644
--- /dev/null
+++ b/01-code/CMakeLists.txt
@@ -0,0 +1,26 @@
+cmake_minimum_required(VERSION 3.20)
+project(didar LANGUAGES CXX)
+
+set(CMAKE_CXX_STANDARD 20)
+set(CMAKE_CXX_STANDARD_REQUIRED ON)
+set(CMAKE_CXX_EXTENSIONS OFF)
+if(NOT CMAKE_BUILD_TYPE)
+  set(CMAKE_BUILD_TYPE Release)
+endif()
+add_compile_options(-Wall -Wextra -Werror -fno-exceptions -fno-rtti)
+
+find_package(Threads REQUIRED)
+
+add_library(didar src/wire.cpp src/client.cpp)
+target_include_directories(didar PUBLIC include PRIVATE src)
+
+add_executable(didard src/didard.cpp src/config.cpp)
+target_include_directories(didard PRIVATE src)
+target_link_libraries(didard PRIVATE didar Threads::Threads)
+
+add_executable(didar-agent src/didar_agent.cpp src/config.cpp)
+target_include_directories(didar-agent PRIVATE src)
+target_link_libraries(didar-agent PRIVATE didar)
+
+enable_testing()
+add_subdirectory(tests)
```

### Public API

```diff
diff --git a/01-code/include/didar.h b/01-code/include/didar.h
new file mode 100644
--- /dev/null
+++ b/01-code/include/didar.h
@@ -0,0 +1,41 @@
+#pragma once
+
+#include <stddef.h>
+#include <stdint.h>
+
+#ifdef __cplusplus
+extern "C" {
+#endif
+
+typedef struct didar_client didar_client;
+
+enum {
+    DIDAR_E_BASE = 1000,
+    DIDAR_E_TARGET_UNREACHABLE = 1001,
+    DIDAR_E_FLOW_TABLE_FULL = 1002,
+    DIDAR_E_BAD_VERSION = 1003,
+    DIDAR_E_MALFORMED = 1004,
+    DIDAR_E_PORT_NOT_ALLOWED = 1005,
+};
+
+/** Opens a non-blocking datagram socket to a didard server. Returns NULL with errno set on failure. */
+didar_client* didar_open(const char* server_ip, uint16_t didar_port);
+
+/** Sends one datagram to target_port. Returns len, -EMSGSIZE above 1468 bytes, or -errno. */
+int didar_send(didar_client* c, uint16_t target_port, const void* buf, size_t len);
+
+/** Receives one datagram and sets source_port. Returns the payload length, -EAGAIN, -errno, or -DIDAR_E_*. */
+int didar_recv(didar_client* c, uint16_t* source_port, void* buf, size_t cap);
+
+/** Returns the socket for the caller's epoll or poll loop. */
+int didar_fd(didar_client* c);
+
+/** Closes the socket and frees the handle. */
+void didar_close(didar_client* c);
+
+/** Describes a negative return value: -errno or -DIDAR_E_*. */
+const char* didar_strerror(int rc);
+
+#ifdef __cplusplus
+}
+#endif
```

### Wire format

```diff
diff --git a/01-code/src/wire.h b/01-code/src/wire.h
new file mode 100644
--- /dev/null
+++ b/01-code/src/wire.h
@@ -0,0 +1,36 @@
+#pragma once
+
+#include <stddef.h>
+#include <stdint.h>
+
+namespace didar {
+
+constexpr uint8_t kVersion = 1;
+constexpr size_t kHeaderSize = 4;
+constexpr size_t kErrorSize = kHeaderSize + 1;
+constexpr size_t kMaxPayload = 1468;
+constexpr size_t kMaxDatagram = kHeaderSize + kMaxPayload;
+constexpr size_t kMaxUdpPayload = 65507;
+
+enum class Type : uint8_t { Data = 0, Error = 1 };
+
+enum class ErrorCode : uint8_t {
+    None = 0,
+    TargetUnreachable = 1,
+    FlowTableFull = 2,
+    BadVersion = 3,
+    Malformed = 4,
+    PortNotAllowed = 5,
+};
+
+struct Header {
+    uint8_t version;
+    uint8_t flags;
+    Type type;
+    uint16_t port;
+};
+
+void write_header(uint8_t* out, Type type, uint16_t port);
+ErrorCode read_header(const uint8_t* in, size_t len, Header* out);
+
+}
diff --git a/01-code/src/wire.cpp b/01-code/src/wire.cpp
new file mode 100644
--- /dev/null
+++ b/01-code/src/wire.cpp
@@ -0,0 +1,34 @@
+#include "wire.h"
+
+#include "didar.h"
+
+namespace didar {
+
+static_assert(DIDAR_E_BASE + int(ErrorCode::TargetUnreachable) == DIDAR_E_TARGET_UNREACHABLE);
+static_assert(DIDAR_E_BASE + int(ErrorCode::FlowTableFull) == DIDAR_E_FLOW_TABLE_FULL);
+static_assert(DIDAR_E_BASE + int(ErrorCode::BadVersion) == DIDAR_E_BAD_VERSION);
+static_assert(DIDAR_E_BASE + int(ErrorCode::Malformed) == DIDAR_E_MALFORMED);
+static_assert(DIDAR_E_BASE + int(ErrorCode::PortNotAllowed) == DIDAR_E_PORT_NOT_ALLOWED);
+
+void write_header(uint8_t* out, Type type, uint16_t port) {
+    out[0] = kVersion << 4;
+    out[1] = uint8_t(type);
+    out[2] = uint8_t(port >> 8);
+    out[3] = uint8_t(port);
+}
+
+ErrorCode read_header(const uint8_t* in, size_t len, Header* out) {
+    *out = {};
+    if (len < kHeaderSize) return ErrorCode::Malformed;
+    out->version = in[0] >> 4;
+    out->flags = in[0] & 0x0f;
+    out->type = Type(in[1]);
+    out->port = uint16_t(in[2] << 8 | in[3]);
+    if (out->version != kVersion) return ErrorCode::BadVersion;
+    if (out->flags != 0) return ErrorCode::Malformed;
+    if (out->type == Type::Data) return ErrorCode::None;
+    if (out->type == Type::Error && len == kErrorSize) return ErrorCode::None;
+    return ErrorCode::Malformed;
+}
+
+}
```

### Client library

```diff
diff --git a/01-code/src/client.cpp b/01-code/src/client.cpp
new file mode 100644
--- /dev/null
+++ b/01-code/src/client.cpp
@@ -0,0 +1,86 @@
+#include "didar.h"
+
+#include <arpa/inet.h>
+#include <errno.h>
+#include <stdlib.h>
+#include <string.h>
+#include <sys/socket.h>
+#include <unistd.h>
+
+#include "wire.h"
+
+struct didar_client {
+    int fd;
+};
+
+didar_client* didar_open(const char* server_ip, uint16_t didar_port) {
+    sockaddr_in server{};
+    server.sin_family = AF_INET;
+    server.sin_port = htons(didar_port);
+    if (inet_pton(AF_INET, server_ip, &server.sin_addr) != 1) {
+        errno = EINVAL;
+        return nullptr;
+    }
+    int fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
+    if (fd < 0) return nullptr;
+    auto* c = static_cast<didar_client*>(malloc(sizeof(didar_client)));
+    if (!c || connect(fd, reinterpret_cast<sockaddr*>(&server), sizeof server) < 0) {
+        int saved = c ? errno : ENOMEM;
+        free(c);
+        close(fd);
+        errno = saved;
+        return nullptr;
+    }
+    c->fd = fd;
+    return c;
+}
+
+int didar_send(didar_client* c, uint16_t target_port, const void* buf, size_t len) {
+    if (len > didar::kMaxPayload) return -EMSGSIZE;
+    uint8_t header[didar::kHeaderSize];
+    didar::write_header(header, didar::Type::Data, target_port);
+    iovec parts[2] = {{header, sizeof header}, {const_cast<void*>(buf), len}};
+    msghdr msg{};
+    msg.msg_iov = parts;
+    msg.msg_iovlen = 2;
+    if (sendmsg(c->fd, &msg, 0) < 0) return -errno;
+    return int(len);
+}
+
+int didar_recv(didar_client* c, uint16_t* source_port, void* buf, size_t cap) {
+    uint8_t header[didar::kHeaderSize];
+    uint8_t spare;
+    iovec parts[2] = {{header, sizeof header}, cap ? iovec{buf, cap} : iovec{&spare, 1}};
+    msghdr msg{};
+    msg.msg_iov = parts;
+    msg.msg_iovlen = 2;
+    ssize_t n = recvmsg(c->fd, &msg, 0);
+    if (n < 0) return -errno;
+    didar::Header h;
+    if (didar::read_header(header, size_t(n), &h) != didar::ErrorCode::None) return -EPROTO;
+    *source_port = h.port;
+    if (h.type == didar::Type::Error) return -(DIDAR_E_BASE + *static_cast<uint8_t*>(parts[1].iov_base));
+    if (msg.msg_flags & MSG_TRUNC) return -EMSGSIZE;
+    return int(n - ssize_t(didar::kHeaderSize));
+}
+
+int didar_fd(didar_client* c) {
+    return c->fd;
+}
+
+void didar_close(didar_client* c) {
+    close(c->fd);
+    free(c);
+}
+
+const char* didar_strerror(int rc) {
+    switch (-rc) {
+    case DIDAR_E_TARGET_UNREACHABLE: return "Nothing is listening on the target port on the server.";
+    case DIDAR_E_FLOW_TABLE_FULL: return "Server has reached its maximum number of flows; try again later.";
+    case DIDAR_E_BAD_VERSION: return "Server does not support this protocol version.";
+    case DIDAR_E_MALFORMED: return "Server could not parse the datagram header.";
+    case DIDAR_E_PORT_NOT_ALLOWED: return "The target port is not open for didar on this server.";
+    }
+    if (rc < 0 && -rc < DIDAR_E_BASE) return strerror(-rc);
+    return "Unknown didar error.";
+}
```

### Config (shared)

```diff
diff --git a/01-code/src/config.h b/01-code/src/config.h
new file mode 100644
--- /dev/null
+++ b/01-code/src/config.h
@@ -0,0 +1,19 @@
+#pragma once
+
+#include <stddef.h>
+#include <stdint.h>
+
+namespace didar {
+
+using KvHandler = int (*)(const char* key, const char* value, void* ctx);
+
+/** Feeds the -c/--config file, then every --flag value pair (dashes become underscores), to on_kv. Returns 0 or -1. */
+int load_config(int argc, char** argv, KvHandler on_kv, void* ctx);
+
+int parse_u32(const char* text, uint32_t min, uint32_t max, uint32_t* out);
+int parse_port(const char* text, uint16_t* out);
+
+/** Copies the text before the last sep into left and points right just after it. Returns 0 or -1. */
+int split_at_last(const char* text, char sep, char* left, size_t left_cap, const char** right);
+
+}
diff --git a/01-code/src/config.cpp b/01-code/src/config.cpp
new file mode 100644
--- /dev/null
+++ b/01-code/src/config.cpp
@@ -0,0 +1,103 @@
+#include "config.h"
+
+#include <ctype.h>
+#include <errno.h>
+#include <stdio.h>
+#include <stdlib.h>
+#include <string.h>
+
+namespace didar {
+namespace {
+
+char* trim(char* text) {
+    while (isspace(static_cast<unsigned char>(*text))) text++;
+    char* end = text + strlen(text);
+    while (end > text && isspace(static_cast<unsigned char>(end[-1]))) *--end = '\0';
+    return text;
+}
+
+bool is_config_flag(const char* arg) {
+    return !strcmp(arg, "-c") || !strcmp(arg, "--config");
+}
+
+int load_file(const char* path, KvHandler on_kv, void* ctx) {
+    FILE* file = fopen(path, "r");
+    if (!file) {
+        fprintf(stderr, "%s: %s\n", path, strerror(errno));
+        return -1;
+    }
+    char line[512];
+    int line_no = 0;
+    int rc = 0;
+    while (rc == 0 && fgets(line, sizeof line, file)) {
+        line_no++;
+        if (char* comment = strchr(line, '#')) *comment = '\0';
+        char* text = trim(line);
+        if (!*text) continue;
+        char* eq = strchr(text, '=');
+        if (!eq) {
+            rc = -1;
+        } else {
+            *eq = '\0';
+            rc = on_kv(trim(text), trim(eq + 1), ctx);
+        }
+        if (rc) fprintf(stderr, "%s:%d: invalid line\n", path, line_no);
+    }
+    fclose(file);
+    return rc ? -1 : 0;
+}
+
+}
+
+int load_config(int argc, char** argv, KvHandler on_kv, void* ctx) {
+    for (int i = 1; i + 1 < argc; i += 2)
+        if (is_config_flag(argv[i]) && load_file(argv[i + 1], on_kv, ctx)) return -1;
+    for (int i = 1; i < argc; i += 2) {
+        const char* flag = argv[i];
+        if (i + 1 == argc) {
+            fprintf(stderr, "%s: missing value\n", flag);
+            return -1;
+        }
+        if (is_config_flag(flag)) continue;
+        char key[64];
+        if (strncmp(flag, "--", 2) || strlen(flag + 2) >= sizeof key) {
+            fprintf(stderr, "%s: unknown flag\n", flag);
+            return -1;
+        }
+        size_t n = 0;
+        for (const char* p = flag + 2; *p; p++) key[n++] = *p == '-' ? '_' : *p;
+        key[n] = '\0';
+        if (on_kv(key, argv[i + 1], ctx)) {
+            fprintf(stderr, "%s %s: invalid value\n", flag, argv[i + 1]);
+            return -1;
+        }
+    }
+    return 0;
+}
+
+int parse_u32(const char* text, uint32_t min, uint32_t max, uint32_t* out) {
+    char* end;
+    errno = 0;
+    unsigned long value = strtoul(text, &end, 10);
+    if (errno || end == text || *end || *text == '-' || value < min || value > max) return -1;
+    *out = uint32_t(value);
+    return 0;
+}
+
+int parse_port(const char* text, uint16_t* out) {
+    uint32_t value;
+    if (parse_u32(text, 1, 65535, &value)) return -1;
+    *out = uint16_t(value);
+    return 0;
+}
+
+int split_at_last(const char* text, char sep, char* left, size_t left_cap, const char** right) {
+    const char* at = strrchr(text, sep);
+    if (!at || size_t(at - text) >= left_cap) return -1;
+    memcpy(left, text, size_t(at - text));
+    left[at - text] = '\0';
+    *right = at + 1;
+    return 0;
+}
+
+}
```

### Flow table (shared)

```diff
diff --git a/01-code/src/flow_table.h b/01-code/src/flow_table.h
new file mode 100644
--- /dev/null
+++ b/01-code/src/flow_table.h
@@ -0,0 +1,120 @@
+#pragma once
+
+#include <arpa/inet.h>
+#include <netinet/in.h>
+#include <stdint.h>
+#include <stdlib.h>
+#include <time.h>
+
+namespace didar {
+
+template <typename V>
+struct FlowTable {
+    struct Slot {
+        uint64_t key;
+        uint64_t last_seen;
+        V value;
+        bool used;
+    };
+    Slot* slots;
+    uint32_t mask;
+    uint32_t count;
+    uint32_t max_flows;
+};
+
+inline uint64_t monotonic_sec() {
+    timespec now;
+    clock_gettime(CLOCK_MONOTONIC, &now);
+    return uint64_t(now.tv_sec);
+}
+
+/** Packs a peer address and a 16-bit tag (target port or map index) into a key. Peer IP 0 marks a listening socket. */
+inline uint64_t flow_key(const sockaddr_in& peer, uint16_t tag) {
+    return uint64_t(ntohl(peer.sin_addr.s_addr)) << 32 | uint64_t(ntohs(peer.sin_port)) << 16 | tag;
+}
+
+inline bool is_listener_key(uint64_t key) {
+    return key >> 32 == 0;
+}
+
+inline sockaddr_in flow_peer(uint64_t key) {
+    sockaddr_in peer{};
+    peer.sin_family = AF_INET;
+    peer.sin_addr.s_addr = htonl(uint32_t(key >> 32));
+    peer.sin_port = htons(uint16_t(key >> 16));
+    return peer;
+}
+
+inline uint16_t flow_tag(uint64_t key) {
+    return uint16_t(key);
+}
+
+inline uint32_t flow_hash(uint64_t key) {
+    return uint32_t((key * 0x9E3779B97F4A7C15ull) >> 32);
+}
+
+/** Allocates room for max_flows at a load factor of at most 0.5. */
+template <typename V>
+bool flow_table_init(FlowTable<V>* t, uint32_t max_flows) {
+    uint32_t capacity = 2;
+    while (capacity < max_flows * 2) capacity <<= 1;
+    t->slots = static_cast<typename FlowTable<V>::Slot*>(calloc(capacity, sizeof *t->slots));
+    t->mask = capacity - 1;
+    t->count = 0;
+    t->max_flows = max_flows;
+    return t->slots != nullptr;
+}
+
+template <typename V>
+typename FlowTable<V>::Slot* flow_find(FlowTable<V>* t, uint64_t key) {
+    for (uint32_t i = flow_hash(key) & t->mask;; i = (i + 1) & t->mask) {
+        auto* slot = &t->slots[i];
+        if (!slot->used) return nullptr;
+        if (slot->key == key) return slot;
+    }
+}
+
+/** Claims a slot for a key that is not present. Returns nullptr when max_flows is reached. */
+template <typename V>
+typename FlowTable<V>::Slot* flow_insert(FlowTable<V>* t, uint64_t key) {
+    if (t->count == t->max_flows) return nullptr;
+    uint32_t i = flow_hash(key) & t->mask;
+    while (t->slots[i].used) i = (i + 1) & t->mask;
+    auto* slot = &t->slots[i];
+    slot->used = true;
+    slot->key = key;
+    t->count++;
+    return slot;
+}
+
+/** Removes a slot with backward-shift deletion, so probe chains stay intact without tombstones. */
+template <typename V>
+void flow_erase(FlowTable<V>* t, typename FlowTable<V>::Slot* slot) {
+    uint32_t hole = uint32_t(slot - t->slots);
+    for (uint32_t i = (hole + 1) & t->mask; t->slots[i].used; i = (i + 1) & t->mask) {
+        uint32_t home = flow_hash(t->slots[i].key) & t->mask;
+        if (((i - home) & t->mask) >= ((i - hole) & t->mask)) {
+            t->slots[hole] = t->slots[i];
+            hole = i;
+        }
+    }
+    t->slots[hole].used = false;
+    t->count--;
+}
+
+// ponytail: scans the whole table once per second; keep a timer wheel if max_flows gets into the millions.
+template <typename V, typename OnClose>
+void flow_close_idle(FlowTable<V>* t, uint64_t now, uint64_t idle_sec, OnClose on_close) {
+    uint32_t i = 0;
+    while (i <= t->mask) {
+        auto* slot = &t->slots[i];
+        if (slot->used && now - slot->last_seen >= idle_sec) {
+            on_close(slot->value);
+            flow_erase(t, slot);
+        } else {
+            i++;
+        }
+    }
+}
+
+}
```

### Server

```diff
diff --git a/01-code/src/didard.cpp b/01-code/src/didard.cpp
new file mode 100644
--- /dev/null
+++ b/01-code/src/didard.cpp
@@ -0,0 +1,242 @@
+#include <errno.h>
+#include <pthread.h>
+#include <sched.h>
+#include <stdio.h>
+#include <stdlib.h>
+#include <string.h>
+#include <sys/epoll.h>
+#include <sys/socket.h>
+#include <unistd.h>
+
+#include "config.h"
+#include "flow_table.h"
+#include "wire.h"
+
+namespace {
+
+constexpr int kBatch = 64;
+constexpr uint64_t kListenKey = 0;
+
+struct ServerConfig {
+    uint16_t port = 0;
+    uint32_t max_flows = 65536;
+    uint32_t idle_sec = 60;
+    bool has_allow = false;
+    bool allowed[65536] = {};
+};
+
+struct ServerFlow {
+    int proxy_fd;
+};
+
+using Flow = didar::FlowTable<ServerFlow>::Slot;
+
+struct Worker {
+    const ServerConfig* config;
+    int listen_fd;
+    int epoll_fd;
+    didar::FlowTable<ServerFlow> flows;
+    mmsghdr msgs[kBatch];
+    iovec iovs[kBatch];
+    sockaddr_in senders[kBatch];
+    uint8_t inbox[kBatch][didar::kMaxDatagram];
+    uint8_t reply[didar::kMaxUdpPayload];
+};
+
+ServerConfig config;
+
+void send_error(Worker* w, const sockaddr_in& client, uint16_t port, didar::ErrorCode code) {
+    uint8_t out[didar::kErrorSize];
+    didar::write_header(out, didar::Type::Error, port);
+    out[didar::kHeaderSize] = uint8_t(code);
+    sendto(w->listen_fd, out, sizeof out, 0, reinterpret_cast<const sockaddr*>(&client), sizeof client);
+}
+
+int open_proxy(Worker* w, uint64_t key) {
+    int fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
+    if (fd < 0) return -1;
+    sockaddr_in target{};
+    target.sin_family = AF_INET;
+    target.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
+    target.sin_port = htons(didar::flow_tag(key));
+    epoll_event ev{};
+    ev.events = EPOLLIN;
+    ev.data.u64 = key;
+    if (connect(fd, reinterpret_cast<sockaddr*>(&target), sizeof target) < 0 ||
+        epoll_ctl(w->epoll_fd, EPOLL_CTL_ADD, fd, &ev) < 0) {
+        close(fd);
+        return -1;
+    }
+    return fd;
+}
+
+Flow* find_or_open_flow(Worker* w, uint64_t key) {
+    Flow* flow = didar::flow_find(&w->flows, key);
+    if (flow) return flow;
+    flow = didar::flow_insert(&w->flows, key);
+    if (!flow) return nullptr;
+    flow->value.proxy_fd = open_proxy(w, key);
+    if (flow->value.proxy_fd >= 0) return flow;
+    didar::flow_erase(&w->flows, flow);
+    return nullptr;
+}
+
+void close_unreachable_flow(Worker* w, Flow* flow) {
+    uint64_t key = flow->key;
+    close(flow->value.proxy_fd);
+    didar::flow_erase(&w->flows, flow);
+    send_error(w, didar::flow_peer(key), didar::flow_tag(key), didar::ErrorCode::TargetUnreachable);
+}
+
+void forward_to_target(Worker* w, const sockaddr_in& client, const uint8_t* data, size_t len, uint64_t now) {
+    didar::Header h;
+    didar::ErrorCode bad = didar::read_header(data, len, &h);
+    if (bad == didar::ErrorCode::None && h.type != didar::Type::Data) bad = didar::ErrorCode::Malformed;
+    if (bad == didar::ErrorCode::None && !w->config->allowed[h.port]) bad = didar::ErrorCode::PortNotAllowed;
+    if (bad != didar::ErrorCode::None) {
+        send_error(w, client, h.port, bad);
+        return;
+    }
+    Flow* flow = find_or_open_flow(w, didar::flow_key(client, h.port));
+    if (!flow) {
+        send_error(w, client, h.port, didar::ErrorCode::FlowTableFull);
+        return;
+    }
+    flow->last_seen = now;
+    ssize_t sent = send(flow->value.proxy_fd, data + didar::kHeaderSize, len - didar::kHeaderSize, 0);
+    if (sent < 0 && errno == ECONNREFUSED) close_unreachable_flow(w, flow);
+}
+
+void receive_from_clients(Worker* w, uint64_t now) {
+    for (;;) {
+        int count = recvmmsg(w->listen_fd, w->msgs, kBatch, MSG_DONTWAIT, nullptr);
+        if (count <= 0) return;
+        for (int i = 0; i < count; i++) {
+            msghdr& hdr = w->msgs[i].msg_hdr;
+            if (hdr.msg_flags & MSG_TRUNC)
+                send_error(w, w->senders[i], 0, didar::ErrorCode::Malformed);
+            else
+                forward_to_target(w, w->senders[i], w->inbox[i], w->msgs[i].msg_len, now);
+            hdr.msg_namelen = sizeof(sockaddr_in);
+        }
+    }
+}
+
+// ponytail: one sendto per reply; batch with sendmmsg if the PR-2 benchmark misses its target.
+void return_to_client(Worker* w, uint64_t key, uint64_t now) {
+    Flow* flow = didar::flow_find(&w->flows, key);
+    if (!flow) return;
+    sockaddr_in client = didar::flow_peer(key);
+    size_t capacity = sizeof w->reply - didar::kHeaderSize;
+    for (;;) {
+        ssize_t n = recv(flow->value.proxy_fd, w->reply + didar::kHeaderSize, capacity, MSG_TRUNC);
+        if (n < 0) {
+            if (errno == ECONNREFUSED) close_unreachable_flow(w, flow);
+            return;
+        }
+        flow->last_seen = now;
+        bool too_big_for_udp = size_t(n) > capacity;
+        if (too_big_for_udp) continue;
+        didar::write_header(w->reply, didar::Type::Data, didar::flow_tag(key));
+        sendto(w->listen_fd, w->reply, didar::kHeaderSize + size_t(n), 0,
+               reinterpret_cast<const sockaddr*>(&client), sizeof client);
+    }
+}
+
+void* run_worker(void* arg) {
+    auto* w = static_cast<Worker*>(arg);
+    epoll_event events[kBatch];
+    uint64_t last_sweep = didar::monotonic_sec();
+    for (;;) {
+        int ready = epoll_wait(w->epoll_fd, events, kBatch, 1000);
+        uint64_t now = didar::monotonic_sec();
+        for (int i = 0; i < ready; i++) {
+            if (events[i].data.u64 == kListenKey)
+                receive_from_clients(w, now);
+            else
+                return_to_client(w, events[i].data.u64, now);
+        }
+        if (now != last_sweep) {
+            didar::flow_close_idle(&w->flows, now, w->config->idle_sec, [](ServerFlow& f) { close(f.proxy_fd); });
+            last_sweep = now;
+        }
+    }
+}
+
+Worker* start_worker(const ServerConfig* cfg) {
+    auto* w = static_cast<Worker*>(calloc(1, sizeof(Worker)));
+    if (!w) return nullptr;
+    w->config = cfg;
+    w->listen_fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
+    w->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
+    int one = 1;
+    sockaddr_in addr{};
+    addr.sin_family = AF_INET;
+    addr.sin_addr.s_addr = htonl(INADDR_ANY);
+    addr.sin_port = htons(cfg->port);
+    epoll_event ev{};
+    ev.events = EPOLLIN;
+    ev.data.u64 = kListenKey;
+    if (w->listen_fd < 0 || w->epoll_fd < 0 ||
+        setsockopt(w->listen_fd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof one) < 0 ||
+        bind(w->listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) < 0 ||
+        epoll_ctl(w->epoll_fd, EPOLL_CTL_ADD, w->listen_fd, &ev) < 0 ||
+        !didar::flow_table_init(&w->flows, cfg->max_flows))
+        return nullptr;
+    for (int i = 0; i < kBatch; i++) {
+        w->iovs[i] = iovec{w->inbox[i], sizeof w->inbox[i]};
+        w->msgs[i].msg_hdr.msg_iov = &w->iovs[i];
+        w->msgs[i].msg_hdr.msg_iovlen = 1;
+        w->msgs[i].msg_hdr.msg_name = &w->senders[i];
+        w->msgs[i].msg_hdr.msg_namelen = sizeof(sockaddr_in);
+    }
+    return w;
+}
+
+int parse_allow(const char* text, bool* allowed) {
+    for (;;) {
+        char* end;
+        errno = 0;
+        unsigned long port = strtoul(text, &end, 10);
+        if (errno || end == text || port == 0 || port > 65535) return -1;
+        allowed[port] = true;
+        while (*end == ' ') end++;
+        if (*end == '\0') return 0;
+        if (*end != ',') return -1;
+        text = end + 1;
+    }
+}
+
+int apply_setting(const char* key, const char* value, void*) {
+    if (!strcmp(key, "port")) return didar::parse_port(value, &config.port);
+    if (!strcmp(key, "max_flows")) return didar::parse_u32(value, 1, 1u << 30, &config.max_flows);
+    if (!strcmp(key, "idle_sec")) return didar::parse_u32(value, 1, 86400, &config.idle_sec);
+    if (!strcmp(key, "allow")) {
+        config.has_allow = true;
+        return parse_allow(value, config.allowed);
+    }
+    return -1;
+}
+
+}
+
+int main(int argc, char** argv) {
+    if (didar::load_config(argc, argv, apply_setting, nullptr) < 0) return 2;
+    if (config.port == 0 || !config.has_allow) {
+        fprintf(stderr, "didard: port and allow are required\n");
+        return 2;
+    }
+    cpu_set_t cpus;
+    sched_getaffinity(0, sizeof cpus, &cpus);
+    int cores = CPU_COUNT(&cpus);
+    auto* threads = static_cast<pthread_t*>(calloc(size_t(cores), sizeof(pthread_t)));
+    for (int i = 0; i < cores; i++) {
+        Worker* w = start_worker(&config);
+        if (!w || pthread_create(&threads[i], nullptr, run_worker, w) != 0) {
+            fprintf(stderr, "didard: port %u: %s\n", config.port, strerror(errno));
+            return 1;
+        }
+    }
+    fprintf(stderr, "didard: ready on port %u with %d threads\n", config.port, cores);
+    pthread_join(threads[0], nullptr);
+}
```

### Agent

```diff
diff --git a/01-code/src/didar_agent.cpp b/01-code/src/didar_agent.cpp
new file mode 100644
--- /dev/null
+++ b/01-code/src/didar_agent.cpp
@@ -0,0 +1,182 @@
+#include <errno.h>
+#include <stdio.h>
+#include <string.h>
+#include <sys/epoll.h>
+#include <sys/socket.h>
+#include <unistd.h>
+
+#include "config.h"
+#include "didar.h"
+#include "flow_table.h"
+#include "wire.h"
+
+namespace {
+
+constexpr int kMaxMaps = 256;
+constexpr int kBatch = 64;
+
+struct PortMap {
+    uint16_t local_port;
+    uint16_t target_port;
+    int listen_fd;
+};
+
+struct AgentConfig {
+    char server_ip[INET_ADDRSTRLEN] = "";
+    uint16_t server_port = 0;
+    PortMap maps[kMaxMaps] = {};
+    int map_count = 0;
+    uint32_t max_flows = 4096;
+    uint32_t idle_sec = 60;
+};
+
+struct AgentFlow {
+    didar_client* handle;
+};
+
+using Flow = didar::FlowTable<AgentFlow>::Slot;
+
+AgentConfig config;
+didar::FlowTable<AgentFlow> flows;
+int epoll_fd;
+uint8_t buffer[didar::kMaxUdpPayload];
+
+void log_error(int rc) {
+    fprintf(stderr, "didar-agent: %s\n", didar_strerror(rc));
+}
+
+Flow* find_or_open_flow(uint64_t key) {
+    Flow* flow = didar::flow_find(&flows, key);
+    if (flow) return flow;
+    flow = didar::flow_insert(&flows, key);
+    if (!flow) {
+        errno = ENOBUFS;
+        return nullptr;
+    }
+    flow->value.handle = didar_open(config.server_ip, config.server_port);
+    epoll_event ev{};
+    ev.events = EPOLLIN;
+    ev.data.u64 = key;
+    if (flow->value.handle && epoll_ctl(epoll_fd, EPOLL_CTL_ADD, didar_fd(flow->value.handle), &ev) == 0)
+        return flow;
+    if (flow->value.handle) didar_close(flow->value.handle);
+    didar::flow_erase(&flows, flow);
+    return nullptr;
+}
+
+void forward_from_app(int map_index, uint64_t now) {
+    const PortMap& map = config.maps[map_index];
+    for (;;) {
+        sockaddr_in app{};
+        socklen_t app_len = sizeof app;
+        ssize_t n = recvfrom(map.listen_fd, buffer, sizeof buffer, 0, reinterpret_cast<sockaddr*>(&app), &app_len);
+        if (n < 0) return;
+        Flow* flow = find_or_open_flow(didar::flow_key(app, uint16_t(map_index)));
+        if (!flow) {
+            fprintf(stderr, "didar-agent: cannot open flow: %s\n", strerror(errno));
+            continue;
+        }
+        flow->last_seen = now;
+        int rc = didar_send(flow->value.handle, map.target_port, buffer, size_t(n));
+        if (rc < 0) log_error(rc);
+    }
+}
+
+void return_to_app(uint64_t key, uint64_t now) {
+    Flow* flow = didar::flow_find(&flows, key);
+    if (!flow) return;
+    const PortMap& map = config.maps[didar::flow_tag(key)];
+    sockaddr_in app = didar::flow_peer(key);
+    for (;;) {
+        uint16_t source_port;
+        int rc = didar_recv(flow->value.handle, &source_port, buffer, sizeof buffer);
+        if (rc == -EAGAIN) return;
+        flow->last_seen = now;
+        if (rc < 0) {
+            log_error(rc);
+            return;
+        }
+        sendto(map.listen_fd, buffer, size_t(rc), 0, reinterpret_cast<const sockaddr*>(&app), sizeof app);
+    }
+}
+
+int open_listener(PortMap& map, int map_index) {
+    map.listen_fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
+    sockaddr_in loopback{};
+    loopback.sin_family = AF_INET;
+    loopback.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
+    loopback.sin_port = htons(map.local_port);
+    epoll_event ev{};
+    ev.events = EPOLLIN;
+    ev.data.u64 = uint64_t(map_index);
+    if (map.listen_fd < 0 || bind(map.listen_fd, reinterpret_cast<sockaddr*>(&loopback), sizeof loopback) < 0 ||
+        epoll_ctl(epoll_fd, EPOLL_CTL_ADD, map.listen_fd, &ev) < 0)
+        return -1;
+    return 0;
+}
+
+int parse_server(const char* value) {
+    char ip[INET_ADDRSTRLEN];
+    const char* port;
+    in_addr parsed;
+    if (didar::split_at_last(value, ':', ip, sizeof ip, &port) || inet_pton(AF_INET, ip, &parsed) != 1) return -1;
+    memcpy(config.server_ip, ip, sizeof ip);
+    return didar::parse_port(port, &config.server_port);
+}
+
+int parse_map(const char* value) {
+    char local[8];
+    const char* target;
+    if (config.map_count == kMaxMaps || didar::split_at_last(value, ':', local, sizeof local, &target)) return -1;
+    PortMap& map = config.maps[config.map_count];
+    if (didar::parse_port(local, &map.local_port) || didar::parse_port(target, &map.target_port)) return -1;
+    config.map_count++;
+    return 0;
+}
+
+int apply_setting(const char* key, const char* value, void*) {
+    if (!strcmp(key, "server")) return parse_server(value);
+    if (!strcmp(key, "map")) return parse_map(value);
+    if (!strcmp(key, "max_flows")) return didar::parse_u32(value, 1, 1u << 30, &config.max_flows);
+    if (!strcmp(key, "idle_sec")) return didar::parse_u32(value, 1, 86400, &config.idle_sec);
+    return -1;
+}
+
+}
+
+int main(int argc, char** argv) {
+    if (didar::load_config(argc, argv, apply_setting, nullptr) < 0) return 2;
+    if (config.server_port == 0 || config.map_count == 0) {
+        fprintf(stderr, "didar-agent: server and map are required\n");
+        return 2;
+    }
+    epoll_fd = epoll_create1(EPOLL_CLOEXEC);
+    if (epoll_fd < 0 || !didar::flow_table_init(&flows, config.max_flows)) {
+        perror("didar-agent");
+        return 1;
+    }
+    for (int i = 0; i < config.map_count; i++) {
+        if (open_listener(config.maps[i], i) < 0) {
+            fprintf(stderr, "didar-agent: 127.0.0.1:%u: %s\n", config.maps[i].local_port, strerror(errno));
+            return 1;
+        }
+    }
+    fprintf(stderr, "didar-agent: ready with %d maps\n", config.map_count);
+    epoll_event events[kBatch];
+    uint64_t last_sweep = didar::monotonic_sec();
+    for (;;) {
+        int ready = epoll_wait(epoll_fd, events, kBatch, 1000);
+        uint64_t now = didar::monotonic_sec();
+        for (int i = 0; i < ready; i++) {
+            uint64_t key = events[i].data.u64;
+            if (didar::is_listener_key(key))
+                forward_from_app(int(key), now);
+            else
+                return_to_app(key, now);
+        }
+        if (now != last_sweep) {
+            didar::flow_close_idle(&flows, now, config.idle_sec, [](AgentFlow& f) { didar_close(f.handle); });
+            last_sweep = now;
+        }
+    }
+}
```

## Test cases

Tests use only the interface: `didar.h`, the wire format documented in `architecture.md`, and the built `didard` and `didar-agent` programs. CMake keeps `src/` private, so including an internal header from a test fails to compile. There are no unit tests. Each table row below is one function in the test code that follows.

### FR-1: Single-port demultiplexing

| ID | Scenario | Steps | Expected |
| -- | -------- | ----- | -------- |
| FR-1.1 | Datagram reaches its target | didard allows A; client `didar_send(A, "hi")` | Handler A receives exactly `hi` (header stripped) |
| FR-1.2 | Two targets through one port | Allow A and B; send to A, then B | A gets only the A payload, B only the B payload |
| FR-1.3 | Reply returns to client | Handler A echoes | `didar_recv` returns the echo with `source_port == A` |
| FR-1.4 | Two clients, same target | Two handles send to A; handler echoes | Each client receives only its own echo; handler sees two different source ports |
| FR-1.5 | Datagram boundaries kept | Send 0, 1, and 1468-byte payloads | Handler receives three datagrams of exactly those sizes |
| FR-1.6 | Idle flow closed | `idle_sec = 1`; send, wait 3 s, send again | Handler sees a new source port for the second datagram |
| FR-1.7 | Agent forwards an unmodified app | Agent `map L:A`; plain UDP socket sends to `127.0.0.1:L` | Handler A receives the payload; app receives the echo from `127.0.0.1:L` |
| FR-1.8 | Agent separates apps | Two plain sockets send to `127.0.0.1:L` | Handler sees two source ports; each app gets only its own echo |
| FR-1.9 | Config file plus flag override | didard `-c` file with `port = X`, flag `--port Y` | Server answers on Y, not on X |

### FR-4: Optional encryption (phase 1 part)

| ID | Scenario | Steps | Expected |
| -- | -------- | ----- | -------- |
| FR-4.1 | Encryption disabled needs no handshake | First datagram on a fresh handle is DATA | Delivered with no prior exchange (no other datagrams seen on the wire) |
| FR-4.2 | Encrypted flag not supported yet | Raw socket sends a header with flags bit 0 set | Client receives `MALFORMED`; handler receives nothing |
| FR-4.3 | Encrypted path | — | Deferred to phase 2: needs the crypto library |

### FR-5: Error reporting

| ID | Scenario | Steps | Expected |
| -- | -------- | ----- | -------- |
| FR-5.1 | Port not allowed | Send to a port not in `allow` | `didar_recv` returns `-DIDAR_E_PORT_NOT_ALLOWED`, `source_port` is that port, message matches api.md |
| FR-5.2 | Target unreachable | Allowed port, no handler; send twice | `-DIDAR_E_TARGET_UNREACHABLE` for that port |
| FR-5.3 | Bad version | Raw header with version 2 | `-DIDAR_E_BAD_VERSION` |
| FR-5.4 | Malformed | Raw 3-byte datagram; raw unknown type; raw ERROR from the client; 1473-byte payload | `-DIDAR_E_MALFORMED` for each |
| FR-5.5 | Flow table full | `max_flows = 1`, didard pinned to one CPU; second client sends | Second client gets `-DIDAR_E_FLOW_TABLE_FULL`; first still works |
| FR-5.6 | Messages | `didar_strerror` for codes 1001–1005, `-EAGAIN`, and an unknown code | Exact api.md texts, the system message, and "Unknown didar error." |
| FR-5.7 | Agent logs errors | Agent maps to a port that is not allowed | Agent stderr contains the `PORT_NOT_ALLOWED` message; the app gets nothing |

### SR-3: Target allow-list

| ID | Scenario | Steps | Expected |
| -- | -------- | ----- | -------- |
| SR-3.1 | Not-allowed port is not reached | Handler on port N, N not in `allow`; send to N | Handler receives nothing within 500 ms |
| SR-3.2 | Rejection creates no state | `max_flows = 1`, pinned to one CPU; 100 sends to a not-allowed port, then one to an allowed port | The allowed send is delivered |
| SR-3.3 | Allow-list is required | Start didard without `allow` | Exit code 2 and the "port and allow are required" message |
| SR-3.4 | Invalid allow values | `--allow 0`, `--allow 70000`, `--allow 7000,x` | Exit code 2 |

### SR-4: Local-only agent

| ID | Scenario | Steps | Expected |
| -- | -------- | ----- | -------- |
| SR-4.1 | Agent listens on loopback only | Start agent with `map L:A`; read `/proc/net/udp` | Local port L is bound to `0100007F` (127.0.0.1) only |
| SR-4.2 | No way to change the bind address | Start agent with `--bind 0.0.0.0` | Exit code 2 |

### PR-1: Low size overhead

| ID | Scenario | Steps | Expected |
| -- | -------- | ----- | -------- |
| PR-1.1 | Plaintext overhead is 4 bytes | Point `didar_open` at a plain test socket; send 0, 100, and 1468 bytes | Received sizes are payload + 4 |
| PR-1.2 | Size limit | `didar_send` with 1468 and 1469 bytes | 1468 succeeds; 1469 returns `-EMSGSIZE` and sends nothing |
| PR-1.3 | Encrypted overhead ≤ 32 bytes | — | Deferred to phase 2 |

### PR-2: Low demultiplexing time

| ID | Scenario | Steps | Expected |
| -- | -------- | ----- | -------- |
| PR-2.1 | Plaintext p99 added latency | Send 100,000 datagrams with 1200-byte payloads, each stamped with `CLOCK_MONOTONIC`; measure the one-way latency at the handler, direct and through didard | p99(through didard) − p99(direct) ≤ 50 µs. Label `perf`; run in a Release build |
| PR-2.2 | Encrypted ≤ 100 µs | — | Deferred to phase 2 |

### Test code

```diff
diff --git a/01-code/tests/CMakeLists.txt b/01-code/tests/CMakeLists.txt
new file mode 100644
--- /dev/null
+++ b/01-code/tests/CMakeLists.txt
@@ -0,0 +1,14 @@
+function(didar_test name label)
+  add_executable(test_${name} test_${name}.cpp)
+  target_link_libraries(test_${name} PRIVATE didar)
+  target_compile_definitions(test_${name} PRIVATE
+    DIDARD="$<TARGET_FILE:didard>" DIDAR_AGENT="$<TARGET_FILE:didar-agent>")
+  add_dependencies(test_${name} didard didar-agent)
+  add_test(NAME ${name} COMMAND test_${name})
+  set_tests_properties(${name} PROPERTIES LABELS ${label})
+endfunction()
+
+foreach(name fr1 fr4 fr5 sr3 sr4 pr1)
+  didar_test(${name} requirement)
+endforeach()
+didar_test(pr2 perf)
diff --git a/01-code/tests/harness.h b/01-code/tests/harness.h
new file mode 100644
--- /dev/null
+++ b/01-code/tests/harness.h
@@ -0,0 +1,206 @@
+#pragma once
+
+#include <arpa/inet.h>
+#include <errno.h>
+#include <poll.h>
+#include <sched.h>
+#include <signal.h>
+#include <stdio.h>
+#include <stdlib.h>
+#include <string.h>
+#include <sys/socket.h>
+#include <sys/wait.h>
+#include <unistd.h>
+
+#include "didar.h"
+
+#define CHECK(cond)                                                                   \
+    do {                                                                              \
+        if (!(cond)) {                                                                \
+            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
+            exit(1);                                                                  \
+        }                                                                             \
+    } while (0)
+
+namespace test {
+
+constexpr int kQuietMs = 300;
+
+struct Process {
+    pid_t pid;
+    int stderr_fd;
+    char log[8192];
+    size_t log_len;
+};
+
+inline const char* num(unsigned value) {
+    static char ring[16][16];
+    static int next;
+    char* out = ring[next++ % 16];
+    snprintf(out, sizeof ring[0], "%u", value);
+    return out;
+}
+
+inline sockaddr_in loopback(uint16_t port) {
+    sockaddr_in addr{};
+    addr.sin_family = AF_INET;
+    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
+    addr.sin_port = htons(port);
+    return addr;
+}
+
+inline int udp_socket(uint16_t* port) {
+    int fd = socket(AF_INET, SOCK_DGRAM, 0);
+    sockaddr_in addr = loopback(*port);
+    socklen_t len = sizeof addr;
+    CHECK(fd >= 0);
+    CHECK(bind(fd, (sockaddr*)&addr, len) == 0);
+    CHECK(getsockname(fd, (sockaddr*)&addr, &len) == 0);
+    *port = ntohs(addr.sin_port);
+    return fd;
+}
+
+inline uint16_t free_port() {
+    uint16_t port = 0;
+    close(udp_socket(&port));
+    return port;
+}
+
+inline bool readable(int fd, int timeout_ms) {
+    pollfd p{fd, POLLIN, 0};
+    return poll(&p, 1, timeout_ms) == 1;
+}
+
+inline ssize_t recv_within(int fd, void* buf, size_t cap, sockaddr_in* from, int timeout_ms) {
+    if (!readable(fd, timeout_ms)) return -1;
+    socklen_t len = sizeof(sockaddr_in);
+    return recvfrom(fd, buf, cap, 0, (sockaddr*)from, from ? &len : nullptr);
+}
+
+inline void send_to(int fd, uint16_t port, const void* buf, size_t len) {
+    sockaddr_in to = loopback(port);
+    CHECK(sendto(fd, buf, len, 0, (sockaddr*)&to, sizeof to) == ssize_t(len));
+}
+
+inline void expect_payload(int fd, const void* payload, size_t len) {
+    static char buf[2048];
+    CHECK(recv_within(fd, buf, sizeof buf, nullptr, 1000) == ssize_t(len));
+    CHECK(memcmp(buf, payload, len) == 0);
+}
+
+inline void expect_quiet(int fd) {
+    char buf[2048];
+    CHECK(recv_within(fd, buf, sizeof buf, nullptr, kQuietMs) == -1);
+}
+
+inline void echo(int fd, sockaddr_in* from_out = nullptr) {
+    char buf[2048];
+    sockaddr_in from{};
+    ssize_t n = recv_within(fd, buf, sizeof buf, &from, 1000);
+    CHECK(n >= 0);
+    CHECK(sendto(fd, buf, size_t(n), 0, (sockaddr*)&from, sizeof from) == n);
+    if (from_out) *from_out = from;
+}
+
+inline void write_raw_header(uint8_t* out, uint8_t version_and_flags, uint8_t type, uint16_t port) {
+    out[0] = version_and_flags;
+    out[1] = type;
+    out[2] = uint8_t(port >> 8);
+    out[3] = uint8_t(port);
+}
+
+inline void expect_error_reply(uint16_t didar_port, const uint8_t* datagram, size_t len, uint8_t code) {
+    uint16_t port = 0;
+    int fd = udp_socket(&port);
+    send_to(fd, didar_port, datagram, len);
+    uint8_t reply[16];
+    CHECK(recv_within(fd, reply, sizeof reply, nullptr, 1000) == 5);
+    CHECK(reply[0] == 0x10 && reply[1] == 1 && reply[4] == code);
+    close(fd);
+}
+
+inline Process spawn(const char* path, const char* const* args, bool one_cpu = false) {
+    const char* argv[32] = {path};
+    for (int i = 0; args[i]; i++) argv[i + 1] = args[i];
+    int pipe_fds[2];
+    CHECK(pipe(pipe_fds) == 0);
+    Process p{};
+    p.pid = fork();
+    CHECK(p.pid >= 0);
+    if (p.pid == 0) {
+        if (one_cpu) {
+            cpu_set_t cpus;
+            CPU_ZERO(&cpus);
+            CPU_SET(0, &cpus);
+            sched_setaffinity(0, sizeof cpus, &cpus);
+        }
+        dup2(pipe_fds[1], STDERR_FILENO);
+        close(pipe_fds[0]);
+        execv(path, const_cast<char* const*>(argv));
+        _exit(127);
+    }
+    close(pipe_fds[1]);
+    p.stderr_fd = pipe_fds[0];
+    return p;
+}
+
+inline bool log_contains(Process& p, const char* needle, int timeout_ms) {
+    for (int waited = 0;; waited += 10) {
+        p.log[p.log_len] = '\0';
+        if (strstr(p.log, needle)) return true;
+        if (waited >= timeout_ms) return false;
+        if (readable(p.stderr_fd, 10)) {
+            ssize_t n = read(p.stderr_fd, p.log + p.log_len, sizeof p.log - 1 - p.log_len);
+            if (n > 0) p.log_len += size_t(n);
+        }
+    }
+}
+
+inline int exit_code(Process& p) {
+    int status;
+    for (int i = 0; i < 200; i++) {
+        if (waitpid(p.pid, &status, WNOHANG) == p.pid) return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
+        usleep(10000);
+    }
+    return -1;
+}
+
+inline void stop(Process& p) {
+    kill(p.pid, SIGTERM);
+    waitpid(p.pid, nullptr, 0);
+    close(p.stderr_fd);
+}
+
+inline Process start_ready(const char* path, const char* const* args, bool one_cpu = false) {
+    Process p = spawn(path, args, one_cpu);
+    CHECK(log_contains(p, "ready", 2000));
+    return p;
+}
+
+inline Process start_didard(uint16_t port, const char* allow, const char* extra_flag = nullptr,
+                            const char* extra_value = nullptr, bool one_cpu = false) {
+    const char* args[] = {"--port", num(port), "--allow", allow, extra_flag, extra_value, nullptr};
+    return start_ready(DIDARD, args, one_cpu);
+}
+
+inline Process start_agent(uint16_t didar_port, uint16_t local_port, uint16_t target_port) {
+    char server[32];
+    char map[16];
+    snprintf(server, sizeof server, "127.0.0.1:%u", didar_port);
+    snprintf(map, sizeof map, "%u:%u", local_port, target_port);
+    const char* args[] = {"--server", server, "--map", map, nullptr};
+    return start_ready(DIDAR_AGENT, args);
+}
+
+inline didar_client* open_client(uint16_t didar_port) {
+    didar_client* c = didar_open("127.0.0.1", didar_port);
+    CHECK(c != nullptr);
+    return c;
+}
+
+inline int client_recv_within(didar_client* c, uint16_t* source_port, void* buf, size_t cap, int timeout_ms) {
+    if (!readable(didar_fd(c), timeout_ms)) return -ETIMEDOUT;
+    return didar_recv(c, source_port, buf, cap);
+}
+
+}
diff --git a/01-code/tests/test_fr1.cpp b/01-code/tests/test_fr1.cpp
new file mode 100644
--- /dev/null
+++ b/01-code/tests/test_fr1.cpp
@@ -0,0 +1,161 @@
+#include "harness.h"
+
+namespace {
+
+uint16_t didar_port;
+uint16_t port_a;
+uint16_t port_b;
+int handler_a;
+int handler_b;
+
+void fr1_1_datagram_reaches_target() {
+    didar_client* c = test::open_client(didar_port);
+    CHECK(didar_send(c, port_a, "hi", 2) == 2);
+    test::expect_payload(handler_a, "hi", 2);
+    didar_close(c);
+}
+
+void fr1_2_two_targets_through_one_port() {
+    didar_client* c = test::open_client(didar_port);
+    CHECK(didar_send(c, port_a, "a", 1) == 1);
+    CHECK(didar_send(c, port_b, "b", 1) == 1);
+    test::expect_payload(handler_a, "a", 1);
+    test::expect_payload(handler_b, "b", 1);
+    test::expect_quiet(handler_a);
+    test::expect_quiet(handler_b);
+    didar_close(c);
+}
+
+void fr1_3_reply_returns_to_client() {
+    didar_client* c = test::open_client(didar_port);
+    CHECK(didar_send(c, port_a, "ping", 4) == 4);
+    test::echo(handler_a);
+    char buf[16];
+    uint16_t source = 0;
+    CHECK(test::client_recv_within(c, &source, buf, sizeof buf, 1000) == 4);
+    CHECK(memcmp(buf, "ping", 4) == 0);
+    CHECK(source == port_a);
+    didar_close(c);
+}
+
+void fr1_4_two_clients_same_target() {
+    didar_client* c1 = test::open_client(didar_port);
+    didar_client* c2 = test::open_client(didar_port);
+    CHECK(didar_send(c1, port_a, "1", 1) == 1);
+    CHECK(didar_send(c2, port_a, "2", 1) == 1);
+    sockaddr_in from[2];
+    test::echo(handler_a, &from[0]);
+    test::echo(handler_a, &from[1]);
+    CHECK(from[0].sin_port != from[1].sin_port);
+    char got;
+    uint16_t source;
+    CHECK(test::client_recv_within(c1, &source, &got, 1, 1000) == 1 && got == '1');
+    CHECK(test::client_recv_within(c2, &source, &got, 1, 1000) == 1 && got == '2');
+    didar_close(c1);
+    didar_close(c2);
+}
+
+void fr1_5_datagram_boundaries_kept() {
+    static char payload[1468];
+    memset(payload, 'x', sizeof payload);
+    didar_client* c = test::open_client(didar_port);
+    const size_t sizes[] = {0, 1, 1468};
+    for (size_t size : sizes) CHECK(didar_send(c, port_a, payload, size) == int(size));
+    for (size_t size : sizes) test::expect_payload(handler_a, payload, size);
+    didar_close(c);
+}
+
+void fr1_6_idle_flow_closed() {
+    uint16_t port = test::free_port();
+    test::Process server = test::start_didard(port, test::num(port_a), "--idle-sec", "1");
+    didar_client* c = test::open_client(port);
+    sockaddr_in first{};
+    sockaddr_in second{};
+    CHECK(didar_send(c, port_a, "1", 1) == 1);
+    test::echo(handler_a, &first);
+    sleep(3);
+    CHECK(didar_send(c, port_a, "2", 1) == 1);
+    test::echo(handler_a, &second);
+    CHECK(first.sin_port != second.sin_port);
+    didar_close(c);
+    test::stop(server);
+}
+
+void fr1_7_agent_forwards_unmodified_app() {
+    uint16_t local = test::free_port();
+    test::Process agent = test::start_agent(didar_port, local, port_a);
+    uint16_t app_port = 0;
+    int app = test::udp_socket(&app_port);
+    test::send_to(app, local, "x", 1);
+    test::echo(handler_a);
+    char got;
+    sockaddr_in from{};
+    CHECK(test::recv_within(app, &got, 1, &from, 1000) == 1);
+    CHECK(got == 'x');
+    CHECK(ntohs(from.sin_port) == local && from.sin_addr.s_addr == htonl(INADDR_LOOPBACK));
+    close(app);
+    test::stop(agent);
+}
+
+void fr1_8_agent_separates_apps() {
+    uint16_t local = test::free_port();
+    test::Process agent = test::start_agent(didar_port, local, port_a);
+    uint16_t port1 = 0;
+    uint16_t port2 = 0;
+    int app1 = test::udp_socket(&port1);
+    int app2 = test::udp_socket(&port2);
+    test::send_to(app1, local, "1", 1);
+    test::send_to(app2, local, "2", 1);
+    sockaddr_in from[2];
+    test::echo(handler_a, &from[0]);
+    test::echo(handler_a, &from[1]);
+    CHECK(from[0].sin_port != from[1].sin_port);
+    char got;
+    CHECK(test::recv_within(app1, &got, 1, nullptr, 1000) == 1 && got == '1');
+    CHECK(test::recv_within(app2, &got, 1, nullptr, 1000) == 1 && got == '2');
+    close(app1);
+    close(app2);
+    test::stop(agent);
+}
+
+void fr1_9_flags_override_config_file() {
+    uint16_t file_port = test::free_port();
+    uint16_t flag_port = test::free_port();
+    char path[] = "/tmp/didard-test-XXXXXX";
+    int fd = mkstemp(path);
+    CHECK(fd >= 0);
+    dprintf(fd, "port = %u\nallow = %u\n", file_port, port_a);
+    close(fd);
+    const char* args[] = {"-c", path, "--port", test::num(flag_port), nullptr};
+    test::Process server = test::start_ready(DIDARD, args);
+    didar_client* c = test::open_client(flag_port);
+    CHECK(didar_send(c, port_a, "f", 1) == 1);
+    test::expect_payload(handler_a, "f", 1);
+    uint16_t probe = file_port;
+    close(test::udp_socket(&probe));
+    didar_close(c);
+    test::stop(server);
+    unlink(path);
+}
+
+}
+
+int main() {
+    handler_a = test::udp_socket(&port_a);
+    handler_b = test::udp_socket(&port_b);
+    didar_port = test::free_port();
+    char allow[16];
+    snprintf(allow, sizeof allow, "%u,%u", port_a, port_b);
+    test::Process server = test::start_didard(didar_port, allow);
+    fr1_1_datagram_reaches_target();
+    fr1_2_two_targets_through_one_port();
+    fr1_3_reply_returns_to_client();
+    fr1_4_two_clients_same_target();
+    fr1_5_datagram_boundaries_kept();
+    fr1_6_idle_flow_closed();
+    fr1_7_agent_forwards_unmodified_app();
+    fr1_8_agent_separates_apps();
+    fr1_9_flags_override_config_file();
+    test::stop(server);
+    puts("FR-1: all scenarios passed");
+}
diff --git a/01-code/tests/test_fr4.cpp b/01-code/tests/test_fr4.cpp
new file mode 100644
--- /dev/null
+++ b/01-code/tests/test_fr4.cpp
@@ -0,0 +1,45 @@
+#include "harness.h"
+
+namespace {
+
+uint16_t didar_port;
+uint16_t port_a;
+int handler_a;
+
+void fr4_1_no_handshake_when_disabled() {
+    uint16_t wire_port = 0;
+    int wire = test::udp_socket(&wire_port);
+    didar_client* observed = test::open_client(wire_port);
+    CHECK(didar_send(observed, port_a, "d", 1) == 1);
+    uint8_t seen[8];
+    CHECK(test::recv_within(wire, seen, sizeof seen, nullptr, 1000) == 5);
+    CHECK(seen[0] == 0x10 && seen[1] == 0 && seen[4] == 'd');
+    test::expect_quiet(wire);
+    didar_close(observed);
+    close(wire);
+
+    didar_client* c = test::open_client(didar_port);
+    CHECK(didar_send(c, port_a, "d", 1) == 1);
+    test::expect_payload(handler_a, "d", 1);
+    didar_close(c);
+}
+
+void fr4_2_encrypted_flag_rejected() {
+    uint8_t datagram[5];
+    test::write_raw_header(datagram, 0x11, 0, port_a);
+    datagram[4] = 'e';
+    test::expect_error_reply(didar_port, datagram, sizeof datagram, 4);
+    test::expect_quiet(handler_a);
+}
+
+}
+
+int main() {
+    handler_a = test::udp_socket(&port_a);
+    didar_port = test::free_port();
+    test::Process server = test::start_didard(didar_port, test::num(port_a));
+    fr4_1_no_handshake_when_disabled();
+    fr4_2_encrypted_flag_rejected();
+    test::stop(server);
+    puts("FR-4: phase 1 scenarios passed");
+}
diff --git a/01-code/tests/test_fr5.cpp b/01-code/tests/test_fr5.cpp
new file mode 100644
--- /dev/null
+++ b/01-code/tests/test_fr5.cpp
@@ -0,0 +1,118 @@
+#include "harness.h"
+
+namespace {
+
+uint16_t didar_port;
+uint16_t port_a;
+uint16_t unreachable_port;
+uint16_t blocked_port;
+int handler_a;
+
+void fr5_1_port_not_allowed() {
+    didar_client* c = test::open_client(didar_port);
+    CHECK(didar_send(c, blocked_port, "n", 1) == 1);
+    char buf[8];
+    uint16_t source = 0;
+    int rc = test::client_recv_within(c, &source, buf, sizeof buf, 1000);
+    CHECK(rc == -DIDAR_E_PORT_NOT_ALLOWED);
+    CHECK(source == blocked_port);
+    CHECK(!strcmp(didar_strerror(rc), "The target port is not open for didar on this server."));
+    didar_close(c);
+}
+
+void fr5_2_target_unreachable() {
+    didar_client* c = test::open_client(didar_port);
+    char buf[8];
+    uint16_t source = 0;
+    int rc = 0;
+    for (int attempt = 0; attempt < 3 && rc != -DIDAR_E_TARGET_UNREACHABLE; attempt++) {
+        CHECK(didar_send(c, unreachable_port, "u", 1) == 1);
+        rc = test::client_recv_within(c, &source, buf, sizeof buf, 500);
+    }
+    CHECK(rc == -DIDAR_E_TARGET_UNREACHABLE);
+    CHECK(source == unreachable_port);
+    didar_close(c);
+}
+
+void fr5_3_bad_version() {
+    uint8_t datagram[4];
+    test::write_raw_header(datagram, 0x20, 0, port_a);
+    test::expect_error_reply(didar_port, datagram, sizeof datagram, 3);
+}
+
+void fr5_4_malformed() {
+    uint8_t short_header[3] = {0x10, 0, 0};
+    test::expect_error_reply(didar_port, short_header, sizeof short_header, 4);
+    uint8_t unknown_type[4];
+    test::write_raw_header(unknown_type, 0x10, 7, port_a);
+    test::expect_error_reply(didar_port, unknown_type, sizeof unknown_type, 4);
+    uint8_t error_from_client[5];
+    test::write_raw_header(error_from_client, 0x10, 1, port_a);
+    error_from_client[4] = 1;
+    test::expect_error_reply(didar_port, error_from_client, sizeof error_from_client, 4);
+    static uint8_t oversized[4 + 1473];
+    test::write_raw_header(oversized, 0x10, 0, port_a);
+    test::expect_error_reply(didar_port, oversized, sizeof oversized, 4);
+    test::expect_quiet(handler_a);
+}
+
+void fr5_5_flow_table_full() {
+    uint16_t port = test::free_port();
+    test::Process server = test::start_didard(port, test::num(port_a), "--max-flows", "1", true);
+    didar_client* first = test::open_client(port);
+    didar_client* second = test::open_client(port);
+    CHECK(didar_send(first, port_a, "1", 1) == 1);
+    test::expect_payload(handler_a, "1", 1);
+    CHECK(didar_send(second, port_a, "2", 1) == 1);
+    char buf[8];
+    uint16_t source = 0;
+    CHECK(test::client_recv_within(second, &source, buf, sizeof buf, 1000) == -DIDAR_E_FLOW_TABLE_FULL);
+    CHECK(didar_send(first, port_a, "3", 1) == 1);
+    test::expect_payload(handler_a, "3", 1);
+    didar_close(first);
+    didar_close(second);
+    test::stop(server);
+}
+
+void fr5_6_messages() {
+    CHECK(!strcmp(didar_strerror(-DIDAR_E_TARGET_UNREACHABLE), "Nothing is listening on the target port on the server."));
+    CHECK(!strcmp(didar_strerror(-DIDAR_E_FLOW_TABLE_FULL), "Server has reached its maximum number of flows; try again later."));
+    CHECK(!strcmp(didar_strerror(-DIDAR_E_BAD_VERSION), "Server does not support this protocol version."));
+    CHECK(!strcmp(didar_strerror(-DIDAR_E_MALFORMED), "Server could not parse the datagram header."));
+    CHECK(!strcmp(didar_strerror(-DIDAR_E_PORT_NOT_ALLOWED), "The target port is not open for didar on this server."));
+    CHECK(!strcmp(didar_strerror(-EAGAIN), strerror(EAGAIN)));
+    CHECK(!strcmp(didar_strerror(-9999), "Unknown didar error."));
+}
+
+void fr5_7_agent_logs_errors() {
+    uint16_t local = test::free_port();
+    test::Process agent = test::start_agent(didar_port, local, blocked_port);
+    uint16_t app_port = 0;
+    int app = test::udp_socket(&app_port);
+    test::send_to(app, local, "n", 1);
+    CHECK(test::log_contains(agent, "The target port is not open for didar on this server.", 1000));
+    test::expect_quiet(app);
+    close(app);
+    test::stop(agent);
+}
+
+}
+
+int main() {
+    handler_a = test::udp_socket(&port_a);
+    unreachable_port = test::free_port();
+    blocked_port = test::free_port();
+    didar_port = test::free_port();
+    char allow[16];
+    snprintf(allow, sizeof allow, "%u,%u", port_a, unreachable_port);
+    test::Process server = test::start_didard(didar_port, allow);
+    fr5_1_port_not_allowed();
+    fr5_2_target_unreachable();
+    fr5_3_bad_version();
+    fr5_4_malformed();
+    fr5_5_flow_table_full();
+    fr5_6_messages();
+    fr5_7_agent_logs_errors();
+    test::stop(server);
+    puts("FR-5: all scenarios passed");
+}
diff --git a/01-code/tests/test_sr3.cpp b/01-code/tests/test_sr3.cpp
new file mode 100644
--- /dev/null
+++ b/01-code/tests/test_sr3.cpp
@@ -0,0 +1,61 @@
+#include "harness.h"
+
+namespace {
+
+uint16_t didar_port;
+uint16_t port_a;
+int handler_a;
+
+void sr3_1_blocked_port_not_reached() {
+    uint16_t blocked = 0;
+    int blocked_handler = test::udp_socket(&blocked);
+    didar_client* c = test::open_client(didar_port);
+    CHECK(didar_send(c, blocked, "s", 1) == 1);
+    test::expect_quiet(blocked_handler);
+    didar_close(c);
+    close(blocked_handler);
+}
+
+void sr3_2_rejection_creates_no_state() {
+    uint16_t port = test::free_port();
+    test::Process server = test::start_didard(port, test::num(port_a), "--max-flows", "1", true);
+    didar_client* c = test::open_client(port);
+    uint16_t blocked = test::free_port();
+    for (int i = 0; i < 100; i++) CHECK(didar_send(c, blocked, "s", 1) == 1);
+    CHECK(didar_send(c, port_a, "ok", 2) == 2);
+    test::expect_payload(handler_a, "ok", 2);
+    didar_close(c);
+    test::stop(server);
+}
+
+void sr3_3_allow_list_required() {
+    const char* args[] = {"--port", test::num(test::free_port()), nullptr};
+    test::Process server = test::spawn(DIDARD, args);
+    CHECK(test::exit_code(server) == 2);
+    CHECK(test::log_contains(server, "port and allow are required", 1000));
+    close(server.stderr_fd);
+}
+
+void sr3_4_invalid_allow_values() {
+    const char* invalid[] = {"0", "70000", "7000,x"};
+    for (const char* allow : invalid) {
+        const char* args[] = {"--port", test::num(test::free_port()), "--allow", allow, nullptr};
+        test::Process server = test::spawn(DIDARD, args);
+        CHECK(test::exit_code(server) == 2);
+        close(server.stderr_fd);
+    }
+}
+
+}
+
+int main() {
+    handler_a = test::udp_socket(&port_a);
+    didar_port = test::free_port();
+    test::Process server = test::start_didard(didar_port, test::num(port_a));
+    sr3_1_blocked_port_not_reached();
+    sr3_2_rejection_creates_no_state();
+    sr3_3_allow_list_required();
+    sr3_4_invalid_allow_values();
+    test::stop(server);
+    puts("SR-3: all scenarios passed");
+}
diff --git a/01-code/tests/test_sr4.cpp b/01-code/tests/test_sr4.cpp
new file mode 100644
--- /dev/null
+++ b/01-code/tests/test_sr4.cpp
@@ -0,0 +1,45 @@
+#include "harness.h"
+
+namespace {
+
+bool bound_only_to_loopback(uint16_t port) {
+    FILE* table = fopen("/proc/net/udp", "r");
+    CHECK(table != nullptr);
+    char line[256];
+    bool found = false;
+    bool only_loopback = true;
+    CHECK(fgets(line, sizeof line, table) != nullptr);
+    while (fgets(line, sizeof line, table)) {
+        unsigned address;
+        unsigned local_port;
+        if (sscanf(line, " %*d: %8X:%4X", &address, &local_port) != 2 || local_port != port) continue;
+        found = true;
+        only_loopback = only_loopback && address == 0x0100007F;
+    }
+    fclose(table);
+    return found && only_loopback;
+}
+
+void sr4_1_agent_listens_on_loopback_only() {
+    uint16_t local = test::free_port();
+    test::Process agent = test::start_agent(test::free_port(), local, 7000);
+    CHECK(bound_only_to_loopback(local));
+    test::stop(agent);
+}
+
+void sr4_2_bind_address_cannot_be_changed() {
+    char map[16];
+    snprintf(map, sizeof map, "%u:7000", test::free_port());
+    const char* args[] = {"--server", "127.0.0.1:9", "--map", map, "--bind", "0.0.0.0", nullptr};
+    test::Process agent = test::spawn(DIDAR_AGENT, args);
+    CHECK(test::exit_code(agent) == 2);
+    close(agent.stderr_fd);
+}
+
+}
+
+int main() {
+    sr4_1_agent_listens_on_loopback_only();
+    sr4_2_bind_address_cannot_be_changed();
+    puts("SR-4: all scenarios passed");
+}
diff --git a/01-code/tests/test_pr1.cpp b/01-code/tests/test_pr1.cpp
new file mode 100644
--- /dev/null
+++ b/01-code/tests/test_pr1.cpp
@@ -0,0 +1,44 @@
+#include "harness.h"
+
+namespace {
+
+constexpr uint16_t kTarget = 7000;
+
+void pr1_1_plaintext_overhead_is_4_bytes() {
+    uint16_t wire_port = 0;
+    int wire = test::udp_socket(&wire_port);
+    didar_client* c = test::open_client(wire_port);
+    static uint8_t payload[1468];
+    static uint8_t seen[1600];
+    const size_t sizes[] = {0, 100, 1468};
+    for (size_t size : sizes) {
+        CHECK(didar_send(c, kTarget, payload, size) == int(size));
+        CHECK(test::recv_within(wire, seen, sizeof seen, nullptr, 1000) == ssize_t(size + 4));
+        CHECK(seen[0] == 0x10 && seen[1] == 0);
+        CHECK(seen[2] == kTarget >> 8 && seen[3] == (kTarget & 0xff));
+    }
+    didar_close(c);
+    close(wire);
+}
+
+void pr1_2_size_limit() {
+    uint16_t wire_port = 0;
+    int wire = test::udp_socket(&wire_port);
+    didar_client* c = test::open_client(wire_port);
+    static uint8_t payload[1469];
+    static uint8_t seen[1600];
+    CHECK(didar_send(c, kTarget, payload, 1468) == 1468);
+    CHECK(test::recv_within(wire, seen, sizeof seen, nullptr, 1000) == 1472);
+    CHECK(didar_send(c, kTarget, payload, 1469) == -EMSGSIZE);
+    test::expect_quiet(wire);
+    didar_close(c);
+    close(wire);
+}
+
+}
+
+int main() {
+    pr1_1_plaintext_overhead_is_4_bytes();
+    pr1_2_size_limit();
+    puts("PR-1: plaintext scenarios passed");
+}
diff --git a/01-code/tests/test_pr2.cpp b/01-code/tests/test_pr2.cpp
new file mode 100644
--- /dev/null
+++ b/01-code/tests/test_pr2.cpp
@@ -0,0 +1,65 @@
+#include <time.h>
+
+#include "harness.h"
+
+namespace {
+
+constexpr int kWarmup = 1000;
+constexpr int kSamples = 100000;
+constexpr size_t kPayload = 1200;
+constexpr uint64_t kLimitNs = 50000;
+
+uint64_t samples[kSamples];
+
+uint64_t now_ns() {
+    timespec t;
+    clock_gettime(CLOCK_MONOTONIC, &t);
+    return uint64_t(t.tv_sec) * 1000000000ull + uint64_t(t.tv_nsec);
+}
+
+int ascending(const void* a, const void* b) {
+    uint64_t x = *static_cast<const uint64_t*>(a);
+    uint64_t y = *static_cast<const uint64_t*>(b);
+    return x < y ? -1 : x > y;
+}
+
+template <typename Send>
+uint64_t p99_one_way_ns(int handler, Send send) {
+    static uint8_t payload[kPayload];
+    static uint8_t received[2048];
+    for (int i = 0; i < kWarmup + kSamples; i++) {
+        uint64_t sent_at = now_ns();
+        memcpy(payload, &sent_at, sizeof sent_at);
+        send(payload);
+        CHECK(test::recv_within(handler, received, sizeof received, nullptr, 1000) == ssize_t(kPayload));
+        uint64_t stamp;
+        memcpy(&stamp, received, sizeof stamp);
+        if (i >= kWarmup) samples[i - kWarmup] = now_ns() - stamp;
+    }
+    qsort(samples, kSamples, sizeof samples[0], ascending);
+    return samples[kSamples * 99 / 100];
+}
+
+}
+
+int main() {
+    uint16_t port_a = 0;
+    int handler = test::udp_socket(&port_a);
+    uint16_t didar_port = test::free_port();
+    test::Process server = test::start_didard(didar_port, test::num(port_a));
+    uint16_t direct_port = 0;
+    int direct = test::udp_socket(&direct_port);
+    didar_client* c = test::open_client(didar_port);
+
+    uint64_t direct_p99 = p99_one_way_ns(handler, [&](const uint8_t* p) { test::send_to(direct, port_a, p, kPayload); });
+    uint64_t didar_p99 = p99_one_way_ns(handler, [&](const uint8_t* p) {
+        CHECK(didar_send(c, port_a, p, kPayload) == int(kPayload));
+    });
+    printf("PR-2: p99 direct %llu ns, through didard %llu ns\n", (unsigned long long)direct_p99,
+           (unsigned long long)didar_p99);
+    CHECK(didar_p99 <= direct_p99 + kLimitNs);
+
+    didar_close(c);
+    close(direct);
+    test::stop(server);
+}
```

## Verification

```bash
cmake -S 01-code -B 01-code/build -DCMAKE_BUILD_TYPE=Release
cmake --build 01-code/build
ctest --test-dir 01-code/build --output-on-failure -L "unit|requirement"
ctest --test-dir 01-code/build --output-on-failure -L perf
```

Manual end-to-end check:

```bash
nc -u -l 127.0.0.1 7000 &
01-code/build/didard --port 9000 --allow 7000 &
01-code/build/didar-agent --server 127.0.0.1:9000 --map 5000:7000 &
echo hello | nc -u -w1 127.0.0.1 5000
```

`nc` on 7000 should print `hello`.
