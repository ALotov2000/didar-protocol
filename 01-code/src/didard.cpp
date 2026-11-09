#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "config.h"
#include "flow_table.h"
#include "wire.h"

namespace {

constexpr int kBatch = 64;
constexpr uint64_t kListenKey = 0;

struct ServerConfig {
    uint16_t port = 0;
    uint32_t max_flows = 65536;
    uint32_t idle_sec = 60;
    bool has_allow = false;
    bool allowed[65536] = {};
};

struct ServerFlow {
    int proxy_fd;
};

using Flow = didar::FlowTable<ServerFlow>::Slot;

struct Worker {
    const ServerConfig* config;
    int listen_fd;
    int epoll_fd;
    didar::FlowTable<ServerFlow> flows;
    mmsghdr msgs[kBatch];
    iovec iovs[kBatch];
    sockaddr_in senders[kBatch];
    uint8_t inbox[kBatch][didar::kMaxDatagram];
    uint8_t reply[didar::kMaxUdpPayload];
};

ServerConfig config;

void send_error(Worker* w, const sockaddr_in& client, uint16_t port, didar::ErrorCode code) {
    uint8_t out[didar::kErrorSize];
    didar::write_header(out, didar::Type::Error, port);
    out[didar::kHeaderSize] = uint8_t(code);
    sendto(w->listen_fd, out, sizeof out, 0, reinterpret_cast<const sockaddr*>(&client), sizeof client);
}

int open_proxy(Worker* w, uint64_t key) {
    int fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    sockaddr_in target{};
    target.sin_family = AF_INET;
    target.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    target.sin_port = htons(didar::flow_tag(key));
    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.u64 = key;
    if (connect(fd, reinterpret_cast<sockaddr*>(&target), sizeof target) < 0 ||
        epoll_ctl(w->epoll_fd, EPOLL_CTL_ADD, fd, &ev) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

Flow* find_or_open_flow(Worker* w, uint64_t key) {
    Flow* flow = didar::flow_find(&w->flows, key);
    if (flow) return flow;
    flow = didar::flow_insert(&w->flows, key);
    if (!flow) return nullptr;
    flow->value.proxy_fd = open_proxy(w, key);
    if (flow->value.proxy_fd >= 0) return flow;
    didar::flow_erase(&w->flows, flow);
    return nullptr;
}

void close_unreachable_flow(Worker* w, Flow* flow) {
    uint64_t key = flow->key;
    close(flow->value.proxy_fd);
    didar::flow_erase(&w->flows, flow);
    send_error(w, didar::flow_peer(key), didar::flow_tag(key), didar::ErrorCode::TargetUnreachable);
}

void forward_to_target(Worker* w, const sockaddr_in& client, const uint8_t* data, size_t len, uint64_t now) {
    didar::Header h;
    didar::ErrorCode bad = didar::read_header(data, len, &h);
    if (bad == didar::ErrorCode::None && h.type != didar::Type::Data) bad = didar::ErrorCode::Malformed;
    if (bad == didar::ErrorCode::None && !w->config->allowed[h.port]) bad = didar::ErrorCode::PortNotAllowed;
    if (bad != didar::ErrorCode::None) {
        send_error(w, client, h.port, bad);
        return;
    }
    Flow* flow = find_or_open_flow(w, didar::flow_key(client, h.port));
    if (!flow) {
        send_error(w, client, h.port, didar::ErrorCode::FlowTableFull);
        return;
    }
    flow->last_seen = now;
    ssize_t sent = send(flow->value.proxy_fd, data + didar::kHeaderSize, len - didar::kHeaderSize, 0);
    if (sent < 0 && errno == ECONNREFUSED) close_unreachable_flow(w, flow);
}

void receive_from_clients(Worker* w, uint64_t now) {
    for (;;) {
        int count = recvmmsg(w->listen_fd, w->msgs, kBatch, MSG_DONTWAIT, nullptr);
        if (count <= 0) return;
        for (int i = 0; i < count; i++) {
            msghdr& hdr = w->msgs[i].msg_hdr;
            if (hdr.msg_flags & MSG_TRUNC)
                send_error(w, w->senders[i], 0, didar::ErrorCode::Malformed);
            else
                forward_to_target(w, w->senders[i], w->inbox[i], w->msgs[i].msg_len, now);
            hdr.msg_namelen = sizeof(sockaddr_in);
        }
    }
}

// ponytail: one sendto per reply; batch with sendmmsg if the PR-2 benchmark misses its target.
void return_to_client(Worker* w, uint64_t key, uint64_t now) {
    Flow* flow = didar::flow_find(&w->flows, key);
    if (!flow) return;
    sockaddr_in client = didar::flow_peer(key);
    size_t capacity = sizeof w->reply - didar::kHeaderSize;
    for (;;) {
        ssize_t n = recv(flow->value.proxy_fd, w->reply + didar::kHeaderSize, capacity, MSG_TRUNC);
        if (n < 0) {
            if (errno == ECONNREFUSED) close_unreachable_flow(w, flow);
            return;
        }
        flow->last_seen = now;
        bool too_big_for_udp = size_t(n) > capacity;
        if (too_big_for_udp) continue;
        didar::write_header(w->reply, didar::Type::Data, didar::flow_tag(key));
        sendto(w->listen_fd, w->reply, didar::kHeaderSize + size_t(n), 0,
               reinterpret_cast<const sockaddr*>(&client), sizeof client);
    }
}

void* run_worker(void* arg) {
    auto* w = static_cast<Worker*>(arg);
    epoll_event events[kBatch];
    uint64_t last_sweep = didar::monotonic_sec();
    for (;;) {
        int ready = epoll_wait(w->epoll_fd, events, kBatch, 1000);
        uint64_t now = didar::monotonic_sec();
        for (int i = 0; i < ready; i++) {
            if (events[i].data.u64 == kListenKey)
                receive_from_clients(w, now);
            else
                return_to_client(w, events[i].data.u64, now);
        }
        if (now != last_sweep) {
            didar::flow_close_idle(&w->flows, now, w->config->idle_sec, [](ServerFlow& f) { close(f.proxy_fd); });
            last_sweep = now;
        }
    }
}

Worker* start_worker(const ServerConfig* cfg) {
    auto* w = static_cast<Worker*>(calloc(1, sizeof(Worker)));
    if (!w) return nullptr;
    w->config = cfg;
    w->listen_fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    w->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    int one = 1;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(cfg->port);
    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.u64 = kListenKey;
    if (w->listen_fd < 0 || w->epoll_fd < 0 ||
        setsockopt(w->listen_fd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof one) < 0 ||
        bind(w->listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) < 0 ||
        epoll_ctl(w->epoll_fd, EPOLL_CTL_ADD, w->listen_fd, &ev) < 0 ||
        !didar::flow_table_init(&w->flows, cfg->max_flows))
        return nullptr;
    for (int i = 0; i < kBatch; i++) {
        w->iovs[i] = iovec{w->inbox[i], sizeof w->inbox[i]};
        w->msgs[i].msg_hdr.msg_iov = &w->iovs[i];
        w->msgs[i].msg_hdr.msg_iovlen = 1;
        w->msgs[i].msg_hdr.msg_name = &w->senders[i];
        w->msgs[i].msg_hdr.msg_namelen = sizeof(sockaddr_in);
    }
    return w;
}

int parse_allow(const char* text, bool* allowed) {
    for (;;) {
        char* end;
        errno = 0;
        unsigned long port = strtoul(text, &end, 10);
        if (errno || end == text || port == 0 || port > 65535) return -1;
        allowed[port] = true;
        while (*end == ' ') end++;
        if (*end == '\0') return 0;
        if (*end != ',') return -1;
        text = end + 1;
    }
}

int apply_setting(const char* key, const char* value, void*) {
    if (!strcmp(key, "port")) return didar::parse_port(value, &config.port);
    if (!strcmp(key, "max_flows")) return didar::parse_u32(value, 1, 1u << 30, &config.max_flows);
    if (!strcmp(key, "idle_sec")) return didar::parse_u32(value, 1, 86400, &config.idle_sec);
    if (!strcmp(key, "allow")) {
        config.has_allow = true;
        return parse_allow(value, config.allowed);
    }
    return -1;
}

}

int main(int argc, char** argv) {
    if (didar::load_config(argc, argv, apply_setting, nullptr) < 0) return 2;
    if (config.port == 0 || !config.has_allow) {
        fprintf(stderr, "didard: port and allow are required\n");
        return 2;
    }
    cpu_set_t cpus;
    sched_getaffinity(0, sizeof cpus, &cpus);
    int cores = CPU_COUNT(&cpus);
    auto* threads = static_cast<pthread_t*>(calloc(size_t(cores), sizeof(pthread_t)));
    for (int i = 0; i < cores; i++) {
        Worker* w = start_worker(&config);
        if (!w || pthread_create(&threads[i], nullptr, run_worker, w) != 0) {
            fprintf(stderr, "didard: port %u: %s\n", config.port, strerror(errno));
            return 1;
        }
    }
    fprintf(stderr, "didard: ready on port %u with %d threads\n", config.port, cores);
    pthread_join(threads[0], nullptr);
}
