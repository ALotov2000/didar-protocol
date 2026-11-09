#include "harness.h"

namespace {

uint16_t didar_port;
uint16_t port_a;
uint16_t unreachable_port;
uint16_t blocked_port;
int handler_a;

void fr5_1_port_not_allowed() {
    didar_client* c = test::open_client(didar_port);
    CHECK(didar_send(c, blocked_port, "n", 1) == 1);
    char buf[8];
    uint16_t source = 0;
    int rc = test::client_recv_within(c, &source, buf, sizeof buf, 1000);
    CHECK(rc == -DIDAR_E_PORT_NOT_ALLOWED);
    CHECK(source == blocked_port);
    CHECK(!strcmp(didar_strerror(rc), "The target port is not open for didar on this server."));
    didar_close(c);
}

void fr5_2_target_unreachable() {
    didar_client* c = test::open_client(didar_port);
    char buf[8];
    uint16_t source = 0;
    int rc = 0;
    for (int attempt = 0; attempt < 3 && rc != -DIDAR_E_TARGET_UNREACHABLE; attempt++) {
        CHECK(didar_send(c, unreachable_port, "u", 1) == 1);
        rc = test::client_recv_within(c, &source, buf, sizeof buf, 500);
    }
    CHECK(rc == -DIDAR_E_TARGET_UNREACHABLE);
    CHECK(source == unreachable_port);
    didar_close(c);
}

void fr5_3_bad_version() {
    uint8_t datagram[4];
    test::write_raw_header(datagram, 0x20, 0, port_a);
    test::expect_error_reply(didar_port, datagram, sizeof datagram, 3);
}

void fr5_4_malformed() {
    uint8_t short_header[3] = {0x10, 0, 0};
    test::expect_error_reply(didar_port, short_header, sizeof short_header, 4);
    uint8_t unknown_type[4];
    test::write_raw_header(unknown_type, 0x10, 7, port_a);
    test::expect_error_reply(didar_port, unknown_type, sizeof unknown_type, 4);
    uint8_t error_from_client[5];
    test::write_raw_header(error_from_client, 0x10, 1, port_a);
    error_from_client[4] = 1;
    test::expect_error_reply(didar_port, error_from_client, sizeof error_from_client, 4);
    static uint8_t oversized[4 + 1473];
    test::write_raw_header(oversized, 0x10, 0, port_a);
    test::expect_error_reply(didar_port, oversized, sizeof oversized, 4);
    test::expect_quiet(handler_a);
}

void fr5_5_flow_table_full() {
    uint16_t port = test::free_port();
    test::Process server = test::start_didard(port, test::num(port_a), "--max-flows", "1", true);
    didar_client* first = test::open_client(port);
    didar_client* second = test::open_client(port);
    CHECK(didar_send(first, port_a, "1", 1) == 1);
    test::expect_payload(handler_a, "1", 1);
    CHECK(didar_send(second, port_a, "2", 1) == 1);
    char buf[8];
    uint16_t source = 0;
    CHECK(test::client_recv_within(second, &source, buf, sizeof buf, 1000) == -DIDAR_E_FLOW_TABLE_FULL);
    CHECK(didar_send(first, port_a, "3", 1) == 1);
    test::expect_payload(handler_a, "3", 1);
    didar_close(first);
    didar_close(second);
    test::stop(server);
}

void fr5_6_messages() {
    CHECK(!strcmp(didar_strerror(-DIDAR_E_TARGET_UNREACHABLE), "Nothing is listening on the target port on the server."));
    CHECK(!strcmp(didar_strerror(-DIDAR_E_FLOW_TABLE_FULL), "Server has reached its maximum number of flows; try again later."));
    CHECK(!strcmp(didar_strerror(-DIDAR_E_BAD_VERSION), "Server does not support this protocol version."));
    CHECK(!strcmp(didar_strerror(-DIDAR_E_MALFORMED), "Server could not parse the datagram header."));
    CHECK(!strcmp(didar_strerror(-DIDAR_E_PORT_NOT_ALLOWED), "The target port is not open for didar on this server."));
    CHECK(!strcmp(didar_strerror(-EAGAIN), strerror(EAGAIN)));
    CHECK(!strcmp(didar_strerror(-9999), "Unknown didar error."));
}

void fr5_7_agent_logs_errors() {
    uint16_t local = test::free_port();
    test::Process agent = test::start_agent(didar_port, local, blocked_port);
    uint16_t app_port = 0;
    int app = test::udp_socket(&app_port);
    test::send_to(app, local, "n", 1);
    CHECK(test::log_contains(agent, "The target port is not open for didar on this server.", 1000));
    test::expect_quiet(app);
    close(app);
    test::stop(agent);
}

}

int main() {
    handler_a = test::udp_socket(&port_a);
    unreachable_port = test::free_port();
    blocked_port = test::free_port();
    didar_port = test::free_port();
    char allow[16];
    snprintf(allow, sizeof allow, "%u,%u", port_a, unreachable_port);
    test::Process server = test::start_didard(didar_port, allow);
    fr5_1_port_not_allowed();
    fr5_2_target_unreachable();
    fr5_3_bad_version();
    fr5_4_malformed();
    fr5_5_flow_table_full();
    fr5_6_messages();
    fr5_7_agent_logs_errors();
    test::stop(server);
    puts("FR-5: all scenarios passed");
}
