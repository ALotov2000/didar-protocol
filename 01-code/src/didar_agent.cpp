#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "config.h"
#include "didar.h"
#include "flow_table.h"
#include "wire.h"

namespace {

constexpr int kMaxMaps = 256;
constexpr int kBatch = 64;

struct PortMap {
    uint16_t local_port;
    uint16_t target_port;
    int listen_fd;
};

struct AgentConfig {
    char server_ip[INET_ADDRSTRLEN] = "";
    uint16_t server_port = 0;
    PortMap maps[kMaxMaps] = {};
    int map_count = 0;
    uint32_t max_flows = 4096;
    uint32_t idle_sec = 60;
};

struct AgentFlow {
    didar_client* handle;
};

using Flow = didar::FlowTable<AgentFlow>::Slot;

AgentConfig config;
didar::FlowTable<AgentFlow> flows;
int epoll_fd;
uint8_t buffer[didar::kMaxUdpPayload];

void log_error(int rc) {
    fprintf(stderr, "didar-agent: %s\n", didar_strerror(rc));
}

Flow* find_or_open_flow(uint64_t key) {
    Flow* flow = didar::flow_find(&flows, key);
    if (flow) return flow;
    flow = didar::flow_insert(&flows, key);
    if (!flow) {
        errno = ENOBUFS;
        return nullptr;
    }
    flow->value.handle = didar_open(config.server_ip, config.server_port);
    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.u64 = key;
    if (flow->value.handle && epoll_ctl(epoll_fd, EPOLL_CTL_ADD, didar_fd(flow->value.handle), &ev) == 0)
        return flow;
    if (flow->value.handle) didar_close(flow->value.handle);
    didar::flow_erase(&flows, flow);
    return nullptr;
}

void forward_from_app(int map_index, uint64_t now) {
    const PortMap& map = config.maps[map_index];
    for (;;) {
        sockaddr_in app{};
        socklen_t app_len = sizeof app;
        ssize_t n = recvfrom(map.listen_fd, buffer, sizeof buffer, 0, reinterpret_cast<sockaddr*>(&app), &app_len);
        if (n < 0) return;
        Flow* flow = find_or_open_flow(didar::flow_key(app, uint16_t(map_index)));
        if (!flow) {
            fprintf(stderr, "didar-agent: cannot open flow: %s\n", strerror(errno));
            continue;
        }
        flow->last_seen = now;
        int rc = didar_send(flow->value.handle, map.target_port, buffer, size_t(n));
        if (rc < 0) log_error(rc);
    }
}

void return_to_app(uint64_t key, uint64_t now) {
    Flow* flow = didar::flow_find(&flows, key);
    if (!flow) return;
    const PortMap& map = config.maps[didar::flow_tag(key)];
    sockaddr_in app = didar::flow_peer(key);
    for (;;) {
        uint16_t source_port;
        int rc = didar_recv(flow->value.handle, &source_port, buffer, sizeof buffer);
        if (rc == -EAGAIN) return;
        flow->last_seen = now;
        if (rc < 0) {
            log_error(rc);
            return;
        }
        sendto(map.listen_fd, buffer, size_t(rc), 0, reinterpret_cast<const sockaddr*>(&app), sizeof app);
    }
}

int open_listener(PortMap& map, int map_index) {
    map.listen_fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    sockaddr_in loopback{};
    loopback.sin_family = AF_INET;
    loopback.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    loopback.sin_port = htons(map.local_port);
    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.u64 = uint64_t(map_index);
    if (map.listen_fd < 0 || bind(map.listen_fd, reinterpret_cast<sockaddr*>(&loopback), sizeof loopback) < 0 ||
        epoll_ctl(epoll_fd, EPOLL_CTL_ADD, map.listen_fd, &ev) < 0)
        return -1;
    return 0;
}

int parse_server(const char* value) {
    char ip[INET_ADDRSTRLEN];
    const char* port;
    in_addr parsed;
    if (didar::split_at_last(value, ':', ip, sizeof ip, &port) || inet_pton(AF_INET, ip, &parsed) != 1) return -1;
    memcpy(config.server_ip, ip, sizeof ip);
    return didar::parse_port(port, &config.server_port);
}

int parse_map(const char* value) {
    char local[8];
    const char* target;
    if (config.map_count == kMaxMaps || didar::split_at_last(value, ':', local, sizeof local, &target)) return -1;
    PortMap& map = config.maps[config.map_count];
    if (didar::parse_port(local, &map.local_port) || didar::parse_port(target, &map.target_port)) return -1;
    config.map_count++;
    return 0;
}

int apply_setting(const char* key, const char* value, void*) {
    if (!strcmp(key, "server")) return parse_server(value);
    if (!strcmp(key, "map")) return parse_map(value);
    if (!strcmp(key, "max_flows")) return didar::parse_u32(value, 1, 1u << 30, &config.max_flows);
    if (!strcmp(key, "idle_sec")) return didar::parse_u32(value, 1, 86400, &config.idle_sec);
    return -1;
}

}

int main(int argc, char** argv) {
    if (didar::load_config(argc, argv, apply_setting, nullptr) < 0) return 2;
    if (config.server_port == 0 || config.map_count == 0) {
        fprintf(stderr, "didar-agent: server and map are required\n");
        return 2;
    }
    epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd < 0 || !didar::flow_table_init(&flows, config.max_flows)) {
        perror("didar-agent");
        return 1;
    }
    for (int i = 0; i < config.map_count; i++) {
        if (open_listener(config.maps[i], i) < 0) {
            fprintf(stderr, "didar-agent: 127.0.0.1:%u: %s\n", config.maps[i].local_port, strerror(errno));
            return 1;
        }
    }
    fprintf(stderr, "didar-agent: ready with %d maps\n", config.map_count);
    epoll_event events[kBatch];
    uint64_t last_sweep = didar::monotonic_sec();
    for (;;) {
        int ready = epoll_wait(epoll_fd, events, kBatch, 1000);
        uint64_t now = didar::monotonic_sec();
        for (int i = 0; i < ready; i++) {
            uint64_t key = events[i].data.u64;
            if (didar::is_listener_key(key))
                forward_from_app(int(key), now);
            else
                return_to_app(key, now);
        }
        if (now != last_sweep) {
            didar::flow_close_idle(&flows, now, config.idle_sec, [](AgentFlow& f) { didar_close(f.handle); });
            last_sweep = now;
        }
    }
}
