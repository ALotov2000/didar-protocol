#pragma once

#include <arpa/inet.h>
#include <errno.h>
#include <poll.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include "didar.h"

#define CHECK(cond)                                                                   \
    do {                                                                              \
        if (!(cond)) {                                                                \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            exit(1);                                                                  \
        }                                                                             \
    } while (0)

namespace test {

constexpr int kQuietMs = 300;

struct Process {
    pid_t pid;
    int stderr_fd;
    char log[8192];
    size_t log_len;
};

inline const char* num(unsigned value) {
    static char ring[16][16];
    static int next;
    char* out = ring[next++ % 16];
    snprintf(out, sizeof ring[0], "%u", value);
    return out;
}

inline sockaddr_in loopback(uint16_t port) {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);
    return addr;
}

inline int udp_socket(uint16_t* port) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in addr = loopback(*port);
    socklen_t len = sizeof addr;
    CHECK(fd >= 0);
    CHECK(bind(fd, (sockaddr*)&addr, len) == 0);
    CHECK(getsockname(fd, (sockaddr*)&addr, &len) == 0);
    *port = ntohs(addr.sin_port);
    return fd;
}

inline uint16_t free_port() {
    uint16_t port = 0;
    close(udp_socket(&port));
    return port;
}

inline bool readable(int fd, int timeout_ms) {
    pollfd p{fd, POLLIN, 0};
    return poll(&p, 1, timeout_ms) == 1;
}

inline ssize_t recv_within(int fd, void* buf, size_t cap, sockaddr_in* from, int timeout_ms) {
    if (!readable(fd, timeout_ms)) return -1;
    socklen_t len = sizeof(sockaddr_in);
    return recvfrom(fd, buf, cap, 0, (sockaddr*)from, from ? &len : nullptr);
}

inline void send_to(int fd, uint16_t port, const void* buf, size_t len) {
    sockaddr_in to = loopback(port);
    CHECK(sendto(fd, buf, len, 0, (sockaddr*)&to, sizeof to) == ssize_t(len));
}

inline void expect_payload(int fd, const void* payload, size_t len) {
    static char buf[2048];
    CHECK(recv_within(fd, buf, sizeof buf, nullptr, 1000) == ssize_t(len));
    CHECK(memcmp(buf, payload, len) == 0);
}

inline void expect_quiet(int fd) {
    char buf[2048];
    CHECK(recv_within(fd, buf, sizeof buf, nullptr, kQuietMs) == -1);
}

inline void echo(int fd, sockaddr_in* from_out = nullptr) {
    char buf[2048];
    sockaddr_in from{};
    ssize_t n = recv_within(fd, buf, sizeof buf, &from, 1000);
    CHECK(n >= 0);
    CHECK(sendto(fd, buf, size_t(n), 0, (sockaddr*)&from, sizeof from) == n);
    if (from_out) *from_out = from;
}

inline void write_raw_header(uint8_t* out, uint8_t version_and_flags, uint8_t type, uint16_t port) {
    out[0] = version_and_flags;
    out[1] = type;
    out[2] = uint8_t(port >> 8);
    out[3] = uint8_t(port);
}

inline void expect_error_reply(uint16_t didar_port, const uint8_t* datagram, size_t len, uint8_t code) {
    uint16_t port = 0;
    int fd = udp_socket(&port);
    send_to(fd, didar_port, datagram, len);
    uint8_t reply[16];
    CHECK(recv_within(fd, reply, sizeof reply, nullptr, 1000) == 5);
    CHECK(reply[0] == 0x10 && reply[1] == 1 && reply[4] == code);
    close(fd);
}

inline Process spawn(const char* path, const char* const* args, bool one_cpu = false) {
    const char* argv[32] = {path};
    for (int i = 0; args[i]; i++) argv[i + 1] = args[i];
    int pipe_fds[2];
    CHECK(pipe(pipe_fds) == 0);
    Process p{};
    p.pid = fork();
    CHECK(p.pid >= 0);
    if (p.pid == 0) {
        if (one_cpu) {
            cpu_set_t cpus;
            CPU_ZERO(&cpus);
            CPU_SET(0, &cpus);
            sched_setaffinity(0, sizeof cpus, &cpus);
        }
        dup2(pipe_fds[1], STDERR_FILENO);
        close(pipe_fds[0]);
        execv(path, const_cast<char* const*>(argv));
        _exit(127);
    }
    close(pipe_fds[1]);
    p.stderr_fd = pipe_fds[0];
    return p;
}

inline bool log_contains(Process& p, const char* needle, int timeout_ms) {
    for (int waited = 0;; waited += 10) {
        p.log[p.log_len] = '\0';
        if (strstr(p.log, needle)) return true;
        if (waited >= timeout_ms) return false;
        size_t room = p.log_len < sizeof p.log - 1 ? sizeof p.log - 1 - p.log_len : 0;
        if (room > 0 && readable(p.stderr_fd, 10)) {
            ssize_t n = read(p.stderr_fd, p.log + p.log_len, room);
            if (n > 0) p.log_len += size_t(n);
        }
    }
}

inline int exit_code(Process& p) {
    int status;
    for (int i = 0; i < 200; i++) {
        if (waitpid(p.pid, &status, WNOHANG) == p.pid) return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        usleep(10000);
    }
    return -1;
}

inline void stop(Process& p) {
    kill(p.pid, SIGTERM);
    waitpid(p.pid, nullptr, 0);
    close(p.stderr_fd);
}

inline Process start_ready(const char* path, const char* const* args, bool one_cpu = false) {
    Process p = spawn(path, args, one_cpu);
    CHECK(log_contains(p, "ready", 2000));
    return p;
}

inline Process start_didard(uint16_t port, const char* allow, const char* extra_flag = nullptr,
                            const char* extra_value = nullptr, bool one_cpu = false) {
    const char* args[] = {"--port", num(port), "--allow", allow, extra_flag, extra_value, nullptr};
    return start_ready(DIDARD, args, one_cpu);
}

inline Process start_agent(uint16_t didar_port, uint16_t local_port, uint16_t target_port) {
    char server[32];
    char map[16];
    snprintf(server, sizeof server, "127.0.0.1:%u", didar_port);
    snprintf(map, sizeof map, "%u:%u", local_port, target_port);
    const char* args[] = {"--server", server, "--map", map, nullptr};
    return start_ready(DIDAR_AGENT, args);
}

inline didar_client* open_client(uint16_t didar_port) {
    didar_client* c = didar_open("127.0.0.1", didar_port);
    CHECK(c != nullptr);
    return c;
}

inline int client_recv_within(didar_client* c, uint16_t* source_port, void* buf, size_t cap, int timeout_ms) {
    if (!readable(didar_fd(c), timeout_ms)) return -ETIMEDOUT;
    return didar_recv(c, source_port, buf, cap);
}

}
