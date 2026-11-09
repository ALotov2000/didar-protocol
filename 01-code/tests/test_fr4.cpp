#include "harness.h"

namespace {

uint16_t didar_port;
uint16_t port_a;
int handler_a;

void fr4_1_no_handshake_when_disabled() {
    uint16_t wire_port = 0;
    int wire = test::udp_socket(&wire_port);
    didar_client* observed = test::open_client(wire_port);
    CHECK(didar_send(observed, port_a, "d", 1) == 1);
    uint8_t seen[8];
    CHECK(test::recv_within(wire, seen, sizeof seen, nullptr, 1000) == 5);
    CHECK(seen[0] == 0x10 && seen[1] == 0 && seen[4] == 'd');
    test::expect_quiet(wire);
    didar_close(observed);
    close(wire);

    didar_client* c = test::open_client(didar_port);
    CHECK(didar_send(c, port_a, "d", 1) == 1);
    test::expect_payload(handler_a, "d", 1);
    didar_close(c);
}

void fr4_2_encrypted_flag_rejected() {
    uint8_t datagram[5];
    test::write_raw_header(datagram, 0x11, 0, port_a);
    datagram[4] = 'e';
    test::expect_error_reply(didar_port, datagram, sizeof datagram, 4);
    test::expect_quiet(handler_a);
}

}

int main() {
    handler_a = test::udp_socket(&port_a);
    didar_port = test::free_port();
    test::Process server = test::start_didard(didar_port, test::num(port_a));
    fr4_1_no_handshake_when_disabled();
    fr4_2_encrypted_flag_rejected();
    test::stop(server);
    puts("FR-4: phase 1 scenarios passed");
}
