#include "harness.h"

namespace {

uint16_t didar_port;
uint16_t port_a;
int handler_a;

void sr3_1_blocked_port_not_reached() {
    uint16_t blocked = 0;
    int blocked_handler = test::udp_socket(&blocked);
    didar_client* c = test::open_client(didar_port);
    CHECK(didar_send(c, blocked, "s", 1) == 1);
    test::expect_quiet(blocked_handler);
    didar_close(c);
    close(blocked_handler);
}

void sr3_2_rejection_creates_no_state() {
    uint16_t port = test::free_port();
    test::Process server = test::start_didard(port, test::num(port_a), "--max-flows", "1", true);
    didar_client* c = test::open_client(port);
    uint16_t blocked = test::free_port();
    for (int i = 0; i < 100; i++) CHECK(didar_send(c, blocked, "s", 1) == 1);
    CHECK(didar_send(c, port_a, "ok", 2) == 2);
    test::expect_payload(handler_a, "ok", 2);
    didar_close(c);
    test::stop(server);
}

void sr3_3_allow_list_required() {
    const char* args[] = {"--port", test::num(test::free_port()), nullptr};
    test::Process server = test::spawn(DIDARD, args);
    CHECK(test::exit_code(server) == 2);
    CHECK(test::log_contains(server, "port and allow are required", 1000));
    close(server.stderr_fd);
}

void sr3_4_invalid_allow_values() {
    const char* invalid[] = {"0", "70000", "7000,x"};
    for (const char* allow : invalid) {
        const char* args[] = {"--port", test::num(test::free_port()), "--allow", allow, nullptr};
        test::Process server = test::spawn(DIDARD, args);
        CHECK(test::exit_code(server) == 2);
        close(server.stderr_fd);
    }
}

}

int main() {
    handler_a = test::udp_socket(&port_a);
    didar_port = test::free_port();
    test::Process server = test::start_didard(didar_port, test::num(port_a));
    sr3_1_blocked_port_not_reached();
    sr3_2_rejection_creates_no_state();
    sr3_3_allow_list_required();
    sr3_4_invalid_allow_values();
    test::stop(server);
    puts("SR-3: all scenarios passed");
}
