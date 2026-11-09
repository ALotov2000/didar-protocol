#include "harness.h"

namespace {

constexpr uint16_t kTarget = 7000;

void pr1_1_plaintext_overhead_is_4_bytes() {
    uint16_t wire_port = 0;
    int wire = test::udp_socket(&wire_port);
    didar_client* c = test::open_client(wire_port);
    static uint8_t payload[1468];
    static uint8_t seen[1600];
    const size_t sizes[] = {0, 100, 1468};
    for (size_t size : sizes) {
        CHECK(didar_send(c, kTarget, payload, size) == int(size));
        CHECK(test::recv_within(wire, seen, sizeof seen, nullptr, 1000) == ssize_t(size + 4));
        CHECK(seen[0] == 0x10 && seen[1] == 0);
        CHECK(seen[2] == kTarget >> 8 && seen[3] == (kTarget & 0xff));
    }
    didar_close(c);
    close(wire);
}

void pr1_2_size_limit() {
    uint16_t wire_port = 0;
    int wire = test::udp_socket(&wire_port);
    didar_client* c = test::open_client(wire_port);
    static uint8_t payload[1469];
    static uint8_t seen[1600];
    CHECK(didar_send(c, kTarget, payload, 1468) == 1468);
    CHECK(test::recv_within(wire, seen, sizeof seen, nullptr, 1000) == 1472);
    CHECK(didar_send(c, kTarget, payload, 1469) == -EMSGSIZE);
    test::expect_quiet(wire);
    didar_close(c);
    close(wire);
}

}

int main() {
    pr1_1_plaintext_overhead_is_4_bytes();
    pr1_2_size_limit();
    puts("PR-1: plaintext scenarios passed");
}
