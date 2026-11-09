#include "harness.h"

namespace {

uint16_t didar_port;
uint16_t port_a;
uint16_t port_b;
int handler_a;
int handler_b;

void fr1_1_datagram_reaches_target() {
    didar_client* c = test::open_client(didar_port);
    CHECK(didar_send(c, port_a, "hi", 2) == 2);
    test::expect_payload(handler_a, "hi", 2);
    didar_close(c);
}

void fr1_2_two_targets_through_one_port() {
    didar_client* c = test::open_client(didar_port);
    CHECK(didar_send(c, port_a, "a", 1) == 1);
    CHECK(didar_send(c, port_b, "b", 1) == 1);
    test::expect_payload(handler_a, "a", 1);
    test::expect_payload(handler_b, "b", 1);
    test::expect_quiet(handler_a);
    test::expect_quiet(handler_b);
    didar_close(c);
}

void fr1_3_reply_returns_to_client() {
    didar_client* c = test::open_client(didar_port);
    CHECK(didar_send(c, port_a, "ping", 4) == 4);
    test::echo(handler_a);
    char buf[16];
    uint16_t source = 0;
    CHECK(test::client_recv_within(c, &source, buf, sizeof buf, 1000) == 4);
    CHECK(memcmp(buf, "ping", 4) == 0);
    CHECK(source == port_a);
    didar_close(c);
}

void fr1_4_two_clients_same_target() {
    didar_client* c1 = test::open_client(didar_port);
    didar_client* c2 = test::open_client(didar_port);
    CHECK(didar_send(c1, port_a, "1", 1) == 1);
    CHECK(didar_send(c2, port_a, "2", 1) == 1);
    sockaddr_in from[2];
    test::echo(handler_a, &from[0]);
    test::echo(handler_a, &from[1]);
    CHECK(from[0].sin_port != from[1].sin_port);
    char got;
    uint16_t source;
    CHECK(test::client_recv_within(c1, &source, &got, 1, 1000) == 1 && got == '1');
    CHECK(test::client_recv_within(c2, &source, &got, 1, 1000) == 1 && got == '2');
    didar_close(c1);
    didar_close(c2);
}

void fr1_5_datagram_boundaries_kept() {
    static char payload[1468];
    memset(payload, 'x', sizeof payload);
    didar_client* c = test::open_client(didar_port);
    const size_t sizes[] = {0, 1, 1468};
    for (size_t size : sizes) CHECK(didar_send(c, port_a, payload, size) == int(size));
    for (size_t size : sizes) test::expect_payload(handler_a, payload, size);
    didar_close(c);
}

void fr1_6_idle_flow_closed() {
    uint16_t port = test::free_port();
    test::Process server = test::start_didard(port, test::num(port_a), "--idle-sec", "1");
    didar_client* c = test::open_client(port);
    sockaddr_in first{};
    sockaddr_in second{};
    CHECK(didar_send(c, port_a, "1", 1) == 1);
    test::echo(handler_a, &first);
    sleep(3);
    CHECK(didar_send(c, port_a, "2", 1) == 1);
    test::echo(handler_a, &second);
    CHECK(first.sin_port != second.sin_port);
    didar_close(c);
    test::stop(server);
}

void fr1_7_agent_forwards_unmodified_app() {
    uint16_t local = test::free_port();
    test::Process agent = test::start_agent(didar_port, local, port_a);
    uint16_t app_port = 0;
    int app = test::udp_socket(&app_port);
    test::send_to(app, local, "x", 1);
    test::echo(handler_a);
    char got;
    sockaddr_in from{};
    CHECK(test::recv_within(app, &got, 1, &from, 1000) == 1);
    CHECK(got == 'x');
    CHECK(ntohs(from.sin_port) == local && from.sin_addr.s_addr == htonl(INADDR_LOOPBACK));
    close(app);
    test::stop(agent);
}

void fr1_8_agent_separates_apps() {
    uint16_t local = test::free_port();
    test::Process agent = test::start_agent(didar_port, local, port_a);
    uint16_t port1 = 0;
    uint16_t port2 = 0;
    int app1 = test::udp_socket(&port1);
    int app2 = test::udp_socket(&port2);
    test::send_to(app1, local, "1", 1);
    test::send_to(app2, local, "2", 1);
    sockaddr_in from[2];
    test::echo(handler_a, &from[0]);
    test::echo(handler_a, &from[1]);
    CHECK(from[0].sin_port != from[1].sin_port);
    char got;
    CHECK(test::recv_within(app1, &got, 1, nullptr, 1000) == 1 && got == '1');
    CHECK(test::recv_within(app2, &got, 1, nullptr, 1000) == 1 && got == '2');
    close(app1);
    close(app2);
    test::stop(agent);
}

void fr1_9_flags_override_config_file() {
    uint16_t file_port = test::free_port();
    uint16_t flag_port = test::free_port();
    char path[] = "/tmp/didard-test-XXXXXX";
    int fd = mkstemp(path);
    CHECK(fd >= 0);
    dprintf(fd, "port = %u\nallow = %u\n", file_port, port_a);
    close(fd);
    const char* args[] = {"-c", path, "--port", test::num(flag_port), nullptr};
    test::Process server = test::start_ready(DIDARD, args);
    didar_client* c = test::open_client(flag_port);
    CHECK(didar_send(c, port_a, "f", 1) == 1);
    test::expect_payload(handler_a, "f", 1);
    uint16_t probe = file_port;
    close(test::udp_socket(&probe));
    didar_close(c);
    test::stop(server);
    unlink(path);
}

}

int main() {
    handler_a = test::udp_socket(&port_a);
    handler_b = test::udp_socket(&port_b);
    didar_port = test::free_port();
    char allow[16];
    snprintf(allow, sizeof allow, "%u,%u", port_a, port_b);
    test::Process server = test::start_didard(didar_port, allow);
    fr1_1_datagram_reaches_target();
    fr1_2_two_targets_through_one_port();
    fr1_3_reply_returns_to_client();
    fr1_4_two_clients_same_target();
    fr1_5_datagram_boundaries_kept();
    fr1_6_idle_flow_closed();
    fr1_7_agent_forwards_unmodified_app();
    fr1_8_agent_separates_apps();
    fr1_9_flags_override_config_file();
    test::stop(server);
    puts("FR-1: all scenarios passed");
}
