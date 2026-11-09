#include <time.h>

#include "harness.h"

namespace {

constexpr int kWarmup = 1000;
constexpr int kSamples = 100000;
constexpr size_t kPayload = 1200;
constexpr uint64_t kLimitNs = 50000;

uint64_t samples[kSamples];

uint64_t now_ns() {
    timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return uint64_t(t.tv_sec) * 1000000000ull + uint64_t(t.tv_nsec);
}

int ascending(const void* a, const void* b) {
    uint64_t x = *static_cast<const uint64_t*>(a);
    uint64_t y = *static_cast<const uint64_t*>(b);
    return x < y ? -1 : x > y;
}

template <typename Send>
uint64_t p99_one_way_ns(int handler, Send send) {
    static uint8_t payload[kPayload];
    static uint8_t received[2048];
    for (int i = 0; i < kWarmup + kSamples; i++) {
        uint64_t sent_at = now_ns();
        memcpy(payload, &sent_at, sizeof sent_at);
        send(payload);
        CHECK(test::recv_within(handler, received, sizeof received, nullptr, 1000) == ssize_t(kPayload));
        uint64_t stamp;
        memcpy(&stamp, received, sizeof stamp);
        if (i >= kWarmup) samples[i - kWarmup] = now_ns() - stamp;
    }
    qsort(samples, kSamples, sizeof samples[0], ascending);
    return samples[kSamples * 99 / 100];
}

}

int main() {
    uint16_t port_a = 0;
    int handler = test::udp_socket(&port_a);
    uint16_t didar_port = test::free_port();
    test::Process server = test::start_didard(didar_port, test::num(port_a));
    uint16_t direct_port = 0;
    int direct = test::udp_socket(&direct_port);
    didar_client* c = test::open_client(didar_port);

    uint64_t direct_p99 = p99_one_way_ns(handler, [&](const uint8_t* p) { test::send_to(direct, port_a, p, kPayload); });
    uint64_t didar_p99 = p99_one_way_ns(handler, [&](const uint8_t* p) {
        CHECK(didar_send(c, port_a, p, kPayload) == int(kPayload));
    });
    printf("PR-2: p99 direct %llu ns, through didard %llu ns\n", (unsigned long long)direct_p99,
           (unsigned long long)didar_p99);
    CHECK(didar_p99 <= direct_p99 + kLimitNs);

    didar_close(c);
    close(direct);
    test::stop(server);
}
